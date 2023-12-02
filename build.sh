emcc test/glue.c \
  -o out/mylib.mjs \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORTED_RUNTIME_METHODS=ccall,cwrap \
  -s EXPORTED_FUNCTIONS='["_process_message", "_malloc", "_free"]'