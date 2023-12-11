import wasmWrapper from './wasm-wrapper.mjs';

export default class XSSandbox {
  static async create() {
    const module = await wasmWrapper();
  }

  private constructor() {

  }
}