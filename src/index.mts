import wasmWrapper from './wasm-wrapper.mjs';

const EC_OK_VALUE = 0; // Ok with return value
const EC_OK_UNDEFINED = 1; // Ok with return undefined
const EC_EXCEPTION = 2; // Returned exception message (string)
const EC_METERING_LIMIT_REACHED = 3; // Hit metering limit

export interface XSSandboxOptions {
  /**
   * The interval (in milliseconds) at which the metering counter is incremented.
   * The default is 1000.
   */
  meteringInterval?: number;

  /**
   * The maximum value of the metering counter. Once this value is reached, the
   * sandbox will halt. The default is none.
   */
  meteringLimit?: number;
}

export class XSSandboxError extends Error {
  constructor(message: string) {
    super(message);
  }
}

export async function create(opts?: XSSandboxOptions) {
  const [wasm, sandbox] = await createWasmSandbox(opts ?? {});
  wasm.ccall('initMachine', null, [], []);
  return sandbox;
}

export async function restore(snapshot: Uint8Array, opts?: XSSandboxOptions) {
  const [wasm, sandbox] = await createWasmSandbox(opts ?? {});

  const result = wasm.ccall('restoreSnapshot', null, ['array', 'number'], [snapshot, snapshot.length]);

  if (result === 0) {
    return sandbox;
  } else {
    throw new Error('Error restoring snapshot');
  }
}

async function createWasmSandbox(opts: XSSandboxOptions): Promise<[any, XSSandbox]> {
  const wasm = await wasmWrapper({
    sendMessage: (ptr: number, len: number, outputPtrPtr: number, outputSizePtr: number) => {
      wasm.HEAPU32[outputPtrPtr / 4] = 0;
      wasm.HEAPU32[outputSizePtr / 4] = 0;
      try {
        const bytes = new Uint8Array(wasm.HEAPU8.buffer, ptr, len);
        const str = new TextDecoder().decode(bytes);
        const message = JSON.parse(str);

        const result = sandbox.receiveMessage?.(message);

        if (result === undefined) {
          return EC_OK_UNDEFINED;
        } else {
          const strResult = JSON.stringify(result ?? null);
          const bytesResult = new TextEncoder().encode(strResult + '\0');
          const outputPtr = wasm._malloc(bytesResult.length);
          wasm.HEAPU8.set(bytesResult, outputPtr);
          wasm.HEAPU32[outputPtrPtr / 4] = outputPtr;
          wasm.HEAPU32[outputSizePtr / 4] = bytesResult.length - 1;
          return EC_OK_VALUE;
        }
      } catch (e) {
        const errStr = e instanceof Error ? { message: e.message } : { message: e.toString() };
        const strResult = JSON.stringify(errStr);
        const bytesResult = new TextEncoder().encode(strResult + '\0');
        const outputPtr = wasm._malloc(bytesResult.length);
        wasm.HEAPU8.set(bytesResult, outputPtr);
        wasm.HEAPU32[outputPtrPtr / 4] = outputPtr;
        wasm.HEAPU32[outputSizePtr / 4] = bytesResult.length - 1;
        return EC_EXCEPTION;
      }
    },
    consoleLog: (argsPtr: number, argsSize: number, level: number) => {
      const bytes = new Uint8Array(wasm.HEAPU8.buffer, argsPtr, argsSize);
      const str = new TextDecoder().decode(bytes);
      const args = JSON.parse(str);
      switch (level) {
        case 0: console.log(...args); break;
        case 1: console.warn(...args); break;
        case 2: console.error(...args); break;
      }
    }
  });
  const sandbox = new XSSandbox(wasm, opts);
  return [wasm, sandbox];
}


class XSSandbox {
  /**
   * A client of the library should set this to receive messages from the sandbox.
   */
  receiveMessage?: (message: any) => void;

  constructor(private wasm: any, opts: XSSandboxOptions) {
    this.meteringInterval = opts.meteringInterval ?? 1000;
    this.meteringLimit = opts.meteringLimit;
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
    if (this.active) {
      throw new Error('Cannot take snapshot while sandbox is active');
    }
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

  get active() {
    return this.wasm.ccall('getActive', 'number', [], []) !== 0;
  }

  get meteringInterval() {
    return this.wasm.ccall('getMeteringInterval', 'number', [], []);
  }

  set meteringInterval(value: number) {
    if (this.active) {
      throw new Error('Cannot set metering interval while active');
    }
    if (value < 1) {
      throw new Error('Metering interval must be at least 1');
    }
    this.wasm.ccall('setMeteringInterval', null, ['number'], [value]);
  }

  get meteringLimit(): number | undefined {
    const value = this.wasm.ccall('getMeteringLimit', 'number', [], []);
    return value ? value : undefined;
  }

  set meteringLimit(value: number | undefined) {
    if (this.active) {
      throw new Error('Cannot set metering limit while active');
    }
    this.wasm.ccall('setMeteringLimit', null, ['number'], [value ?? 0]);
  }

  get meter() {
    return this.wasm.ccall('getMeteringCount', 'number', [], []);
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
        const value = JSON.parse(str);
        const error = new XSSandboxError(value.message);
        error.name = value.name;
        error.stack = value.stack;
        throw error;
      } finally {
        // Free returned memory
        wasm._free(outputPtr);
      }
    } else  if (code === EC_METERING_LIMIT_REACHED) {
      throw new Error('Metering limit reached');
    } else {
      throw new Error(`Unexpected return code ${code}`);
    }
  } finally {
    wasm._free(outputPtrPtr);
    wasm._free(outputSizePtr);
  }
}

export default { create, restore }