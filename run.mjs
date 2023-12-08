import my_lib from './dist/mylib.mjs';

console.log('process ID', process.pid);
console.log("Current Node.js Version:", process.version);

// console.log('Waiting 10 seconds for the profiler to attach...');
// await new Promise(resolve => setTimeout(resolve, 10_000));

console.log('Initializing wasm module...');
const moduleInstance = await my_lib();

console.log('Calling exported function...');


// Assuming Module is the Emscripten module
const process_message = moduleInstance.cwrap('process_message', 'number', ['number', 'number', 'number']);
const take_snapshot = moduleInstance.cwrap('take_snapshot', 'number', ['number']);

/**
 * @param {Uint8Array} inputBuffer
 * @returns {Uint8Array}
 */
function processMessage(inputBuffer) {
    // Allocate memory and copy the input buffer to it
    let inputPtr = moduleInstance._malloc(inputBuffer.length);
    moduleInstance.HEAPU8.set(inputBuffer, inputPtr);

    // Allocate memory for the output size
    let outputSizePtr = moduleInstance._malloc(4);

    // Call the C function
    let outputPtr = process_message(inputPtr, inputBuffer.length, outputSizePtr);

    // Check for errors
    if (outputPtr === 0) {
        moduleInstance._free(inputPtr);
        moduleInstance._free(outputSizePtr);
        throw new Error("Error in process_message");
    }

    console.log('reading output')

    // Read the output size
    let outputSize = moduleInstance.HEAPU32[outputSizePtr / 4];

    // Create a new Uint8Array from the output data
    let resultBuffer = moduleInstance.HEAPU8.slice(outputPtr, outputPtr + outputSize);

    console.log('freeing memory')

    // Free the allocated memory
    moduleInstance._free(inputPtr);
    moduleInstance._free(outputPtr);
    moduleInstance._free(outputSizePtr);

    return resultBuffer;
}

function takeSnapshot() {
  // Allocate memory for the output size
  let outputSizePtr = moduleInstance._malloc(4);

  const outputPtr = take_snapshot(outputSizePtr);

}

// Helper function to encode a string to a Uint8Array
function stringToUint8Array(str) {
  // This is a simple way to convert a string to a Uint8Array using TextEncoder
  return new TextEncoder().encode(str);
}

// Helper function to decode a Uint8Array to a string
function uint8ArrayToString(uint8array) {
  // This is a simple way to convert a Uint8Array to a string using TextDecoder
  return new TextDecoder().decode(uint8array);
}

// Test function that uses processMessageWrapper
function testProcessMessage() {
  // Convert the string "World" to a Uint8Array
  let inputBuffer = stringToUint8Array("World");

  // Call the processMessageWrapper function
  let resultBuffer = processMessage(inputBuffer);

  // Decode the result Uint8Array back to a string
  let resultString = uint8ArrayToString(resultBuffer);

  // Log the result
  console.log(resultString);
}

// Run the test
testProcessMessage();

takeSnapshot();

console.log('done');
