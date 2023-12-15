addToLibrary({
  sendMessage: function(ptr, len, outputPtrPtr, outputSizePtr) {
    return Module.sendMessage(ptr, len, outputPtrPtr, outputSizePtr);
  },
  consoleLog: function(args, len, level) {
    return Module.consoleLog(args, len, level);
  },
});