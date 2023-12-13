import wasmWrapper from './wasm-wrapper.mjs';

const EC_OK_VALUE = 0; // Ok with return value
const EC_OK_UNDEFINED = 1; // Ok with return undefined
const EC_EXCEPTION = 2; // Returned exception message (string)

export class XSSandboxError extends Error {
  constructor(message: string) {
    super(message);
  }
}

export async function create() {
  const [wasm, sandbox] = await createWasmSandbox();
  wasm.ccall('initMachine', null, [], []);
  return sandbox;
}

export async function restore(snapshot: Uint8Array) {
  const [wasm, sandbox] = await createWasmSandbox();

  const result = wasm.ccall('restoreSnapshot', null, ['array', 'number'], [snapshot, snapshot.length]);

  if (result === 0) {
    return sandbox;
  } else {
    throw new Error('Error restoring snapshot');
  }
}

async function createWasmSandbox() {
  const wasm = await wasmWrapper({
    sendMessage: (ptr: number, len: number) => {
      try {
        const bytes = new Uint8Array(wasm.HEAPU8.buffer, ptr, len);
        const str = new TextDecoder().decode(bytes);
        const message = JSON.parse(str);
        sandbox.receiveMessage?.(message);
      } catch (e) {
        console.error(e);
      }
    }
  });
  const sandbox = new XSSandbox(wasm);
  return [wasm, sandbox];
}


class XSSandbox {
  /**
   * A client of the library should set this to receive messages from the sandbox.
   */
  receiveMessage?: (message: any) => void;

  constructor(private wasm: any) {
  }

  /**
   * Evaluate a script in the sandbox.
   * @param script ECMAScript source text to evaluate
   * @returns The result of the script, passed through JSON.stringify
   */
  evaluate(script: string) {
    return sandboxInput(this.wasm, script, 0);
  }

  /**
   * Send a message to the sandbox. The message is passed through
   * JSON.stringify. This invokes the globalThis.receiveMessage of the script,
   * it defined (if not defined then this has no effect).
   *
   * @param message The message to send to the sandbox
   * @returns The result returned by receiveMessage, passed through JSON.stringify
   */
  sendMessage(message: any) {
    const str = JSON.stringify(message ?? null);
    return sandboxInput(this.wasm, str, 1);
  }

  /**
   * Create a snapshot of the current state of the sandbox.
   * @returns A snapshot of the current state of the sandbox.
   */
  snapshot() {
    // Memory slot to receive output size
    const outputSizePtr = this.wasm._malloc(4);
    // Memory slot to receive pointer to output buffer
    const outputPtrPtr = this.wasm._malloc(4);

    try {
      const success = this.wasm.ccall('takeSnapshot', 'number', ['number', 'number'], [outputPtrPtr, outputSizePtr])

      if (success) {
        const outputPtr = this.wasm.HEAPU32[outputPtrPtr / 4];
        try {
          const outputSize = this.wasm.HEAPU32[outputSizePtr / 4];
          const bytes = this.wasm.HEAPU8.slice(outputPtr, outputPtr + outputSize);
          return bytes;
        } finally {
          // Free returned memory
          this.wasm._free(outputPtr);
        }
      } else {
        throw new Error('Error capturing snapshot');
      }
    } finally {
      this.wasm._free(outputPtrPtr);
      this.wasm._free(outputSizePtr);
    }
  }
}

// Shared logic for evaluate and sendMessage
function sandboxInput(wasm: any, payload: string, action: 0 | 1) {
  // Memory slot to receive output size
  const outputSizePtr = wasm._malloc(4);
  // Memory slot to receive pointer to output buffer
  const outputPtrPtr = wasm._malloc(4);

  console.assert(outputPtrPtr !== 0, 'outputPtrPtr should not be null');
  console.assert(outputSizePtr !== 0, 'outputSizePtr should not be null');

  try {
    const code = wasm.ccall('sandboxInput', 'number', ['string', 'number', 'number', 'number'], [payload, outputPtrPtr, outputSizePtr, action])

    if (code === EC_OK_VALUE) {
      const outputPtr = wasm.HEAPU32[outputPtrPtr / 4];
      try {
        const outputSize = wasm.HEAPU32[outputSizePtr / 4];
        console.assert(outputSize >= 0, 'outputSize should be non-negative');
        console.assert(outputPtr !== 0, 'outputPtr should not be null');
        const bytes = new Uint8Array(wasm.HEAPU8.buffer, outputPtr, outputSize);
        const str = new TextDecoder().decode(bytes);
        if (str === 'undefined') {
          return undefined;
        }
        return JSON.parse(str);
      } finally {
        // Free returned memory
        wasm._free(outputPtr);
      }
    } else if (code === EC_OK_UNDEFINED) {
      return undefined;
    } else if (code === EC_EXCEPTION) {
      console.assert(outputPtrPtr !== 0, 'outputPtrPtr should not be null');
      console.assert(outputSizePtr !== 0, 'outputSizePtr should not be null');
      const outputPtr = wasm.HEAPU32[outputPtrPtr / 4];
      try {
        const outputSize = wasm.HEAPU32[outputSizePtr / 4];
        const bytes = new Uint8Array(wasm.HEAPU8.buffer, outputPtr, outputSize);
        const str = new TextDecoder().decode(bytes);
        throw new XSSandboxError(str);
      } finally {
        // Free returned memory
        wasm._free(outputPtr);
      }
    } else {
      throw new Error(`Unexpected return code ${code}`);
    }
  } finally {
    wasm._free(outputPtrPtr);
    wasm._free(outputSizePtr);
  }
}

export default { create, restore }