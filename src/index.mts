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
  const wasm = await wasmWrapper({
    sendMessage: (ptr: number, len: number) => {
      const bytes = new Uint8Array(wasm.HEAPU8.buffer, ptr, len);
      const str = new TextDecoder().decode(bytes);
      const message = JSON.parse(str);
      console.log('sendMessage from guest', message);
      sandbox.receiveMessage?.(message);
    }
  });
  const sandbox = new XSSandbox(wasm);
  return sandbox;
}

export async function restore() {
  throw new Error('Not implemented');
}

class XSSandbox {
  /**
   * A client of the library should set this to receive messages from the sandbox.
   */
  receiveMessage?: (message: any) => void;

  constructor(private wasm: any) {
    // this.receiveMessageInternal = this.receiveMessageInternal.bind(this);
    this.wasm.ccall('initMachine', null, [], []);
  }

  evaluate(script: string) {
    // Memory slot to receive output size
    const outputSizePtr = this.wasm._malloc(4);
    // Memory slot to receive pointer to output buffer
    const outputPtrPtr = this.wasm._malloc(4);

    console.assert(outputPtrPtr !== 0, 'outputPtrPtr should not be null');
    console.assert(outputSizePtr !== 0, 'outputSizePtr should not be null');

    try {
      const code = this.wasm.ccall('evaluateScript', 'number', ['string', 'number', 'number'], [script, outputPtrPtr, outputSizePtr]);

      if (code === EC_OK_VALUE) {
        const outputPtr = this.wasm.HEAPU32[outputPtrPtr / 4];
        try {
          const outputSize = this.wasm.HEAPU32[outputSizePtr / 4];
          console.assert(outputSize >= 0, 'outputSize should be non-negative');
          console.assert(outputPtr !== 0, 'outputPtr should not be null');
          const bytes = new Uint8Array(this.wasm.HEAPU8.buffer, outputPtr, outputSize);
          const str = new TextDecoder().decode(bytes);
          return JSON.parse(str);
        } finally {
          // Free returned memory
          this.wasm._free(outputPtr);
        }
      } else if (code === EC_OK_UNDEFINED) {
        return undefined;
      } else if (code === EC_EXCEPTION) {
        console.assert(outputPtrPtr !== 0, 'outputPtrPtr should not be null');
        console.assert(outputSizePtr !== 0, 'outputSizePtr should not be null');
        // TODO: Test this path
        const outputPtr = this.wasm.getValue(outputPtrPtr, 'i32');
        try {
          const outputSize = this.wasm.getValue(outputSizePtr, 'i32');
          const bytes = new Uint8Array(this.wasm.HEAPU8.buffer, outputPtr, outputSize);
          const str = new TextDecoder().decode(bytes);
          throw new XSSandboxError(str);
        } finally {
          // Free returned memory
          this.wasm._free(outputPtr);
        }
      } else {
        throw new Error(`Unexpected return code ${code}`);
      }
    } finally {
      this.wasm._free(outputPtrPtr);
      this.wasm._free(outputSizePtr);
    }
  }

  // private call(func: string, returnType: string | null, argTypes: string[], ...args: any[]) {
  //   // I couldn't find a better way to do this
  //   const prevSendMessage = (globalThis as any)._xsSandBoxSendMessage;
  //   (globalThis as any)._xsSandBoxSendMessage = this.receiveMessageInternal;
  //   try {
  //     return this.wasm.ccall(func, returnType, argTypes, args);
  //   } finally {
  //     (globalThis as any)._xsSandBoxSendMessage = prevSendMessage;
  //   }
  // }

  // private receiveMessageInternal(ptr: number, len: number) {
  // }

  sendMessage(message: any) {
    const str = JSON.stringify(message);
    const bytes = new TextEncoder().encode(str);
    this.wasm.ccall('receiveMessage', null, ['array', 'number'], [bytes, bytes.length]);
  }
}

export default { create, restore }