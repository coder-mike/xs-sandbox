addToLibrary({
  sendMessage: function(ptr, len, outputPtrPtr, outputSizePtr) {
    return Module.sendMessage(ptr, len, outputPtrPtr, outputSizePtr);
  },
});