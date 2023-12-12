import wasmWrapper from './wasm-wrapper.mjs';

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
    this.wasm.ccall('evaluateScript', null, ['string'], [script]);
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