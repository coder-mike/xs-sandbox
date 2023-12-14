# Compiler and Output Settings
CC=emcc
BUILD_DIR=build
OBJ_DIR=obj
DIST_DIR=dist

# Directories
SRC_DIR=src
XS_DIR=moddable/xs
MODULES_DIR=moddable/modules

# Source Files
SOURCES := \
  $(SRC_DIR)/xs_sandbox.c \
  $(SRC_DIR)/wedge.c \
  $(SRC_DIR)/xs_sandbox_platform.c \
  $(XS_DIR)/sources/xsAll.c \
  $(XS_DIR)/sources/xsAPI.c \
  $(XS_DIR)/sources/xsArguments.c \
  $(XS_DIR)/sources/xsArray.c \
  $(XS_DIR)/sources/xsAtomics.c \
  $(XS_DIR)/sources/xsBigInt.c \
  $(XS_DIR)/sources/xsBoolean.c \
  $(XS_DIR)/sources/xsCode.c \
  $(XS_DIR)/sources/xsCommon.c \
  $(XS_DIR)/sources/xsDataView.c \
  $(XS_DIR)/sources/xsDate.c \
  $(XS_DIR)/sources/xsDebug.c \
  $(XS_DIR)/sources/xsDefaults.c \
  $(XS_DIR)/sources/xsdtoa.c \
  $(XS_DIR)/sources/xsError.c \
  $(XS_DIR)/sources/xsFunction.c \
  $(XS_DIR)/sources/xsGenerator.c \
  $(XS_DIR)/sources/xsGlobal.c \
  $(XS_DIR)/sources/xsJSON.c \
  $(XS_DIR)/sources/xsLexical.c \
  $(XS_DIR)/sources/xsLockdown.c \
  $(XS_DIR)/sources/xsMapSet.c \
  $(XS_DIR)/sources/xsMarshall.c \
  $(XS_DIR)/sources/xsMath.c \
  $(XS_DIR)/sources/xsmc.c \
  $(XS_DIR)/sources/xsMemory.c \
  $(XS_DIR)/sources/xsModule.c \
  $(XS_DIR)/sources/xsNumber.c \
  $(XS_DIR)/sources/xsObject.c \
  $(XS_DIR)/sources/xsPlatforms.c \
  $(XS_DIR)/sources/xsProfile.c \
  $(XS_DIR)/sources/xsPromise.c \
  $(XS_DIR)/sources/xsProperty.c \
  $(XS_DIR)/sources/xsProxy.c \
  $(XS_DIR)/sources/xsre.c \
  $(XS_DIR)/sources/xsRegExp.c \
  $(XS_DIR)/sources/xsRun.c \
  $(XS_DIR)/sources/xsScope.c \
  $(XS_DIR)/sources/xsScript.c \
  $(XS_DIR)/sources/xsSnapshot.c \
  $(XS_DIR)/sources/xsSourceMap.c \
  $(XS_DIR)/sources/xsString.c \
  $(XS_DIR)/sources/xsSymbol.c \
  $(XS_DIR)/sources/xsSyntaxical.c \
  $(XS_DIR)/sources/xsTree.c \
  $(XS_DIR)/sources/xsType.c

# Object Files
OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(SOURCES)))

# Compilation Flags
CFLAGS := -fno-common \
          -DINCLUDE_XSPLATFORM \
          -DXSPLATFORM=\"xs_sandbox_platform.h\" \
          -DmxMetering=1 \
          -DmxParse=1 \
          -DmxRun=1 \
          -DmxSloppy=1 \
          -DmxSnapshot=1 \
          -DmxRegExpUnicodePropertyEscapes=1 \
          -DmxStringNormalize=1 \
          -DmxMinusZero=1 \
          -DmxDebug=1 \
          -DmxUnalignedAccess=0 \
          -DWASM_BUILD=1 \
          -I$(XS_DIR)/includes \
          -I$(XS_DIR)/platforms \
          -I$(XS_DIR)/sources \
          -I$(SRC_DIR) \
          -O2

CFLAGS += -DmxNoConsole=1
# CFLAGS += -g3
# CFLAGS += -fdebug-compilation-dir=..

# The bounds checking seems to enable `fxCheckCStack` which doesn't work in WASM
CFLAGS += -DmxBoundsCheck=0

# Linker Flags
LDFLAGS := -sINITIAL_MEMORY=4194304 \
           -sSTACK_SIZE=262144 \
           -sALLOW_MEMORY_GROWTH \
           -sMEMORY_GROWTH_GEOMETRIC_STEP=1.0 \
           -sSUPPORT_LONGJMP=1 \
           -sMODULARIZE=1 \
           -sEXPORT_ES6=1 \
           -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
           -sEXPORTED_FUNCTIONS='["_initMachine", "_restoreSnapshot", "_sandboxInput", "_takeSnapshot", "_malloc", "_free", "_getMeteringLimit", "_setMeteringLimit", "_getMeteringInterval", "_setMeteringInterval", "_getActive", "_getMeteringCount"]' \
           --js-library=$(SRC_DIR)/lib.js \
           --use-preload-cache

# LDFLAGS += -sSINGLE_FILE
LDFLAGS += -sSTACK_OVERFLOW_CHECK=1
LDFLAGS += -sASSERTIONS=2
LDFLAGS += -sSAFE_HEAP=1
LDFLAGS += -sAUTO_JS_LIBRARIES=0
LDFLAGS += -sAUTO_NATIVE_LIBRARIES=0
LDFLAGS += -sALLOW_UNIMPLEMENTED_SYSCALLS=0
LDFLAGS += -sFETCH=0
LDFLAGS += -sWASMFS=0
LDFLAGS += -sPROXY_POSIX_SOCKETS=0
LDFLAGS += -sFILESYSTEM=0
LDFLAGS += -sUSE_SDL=0
LDFLAGS += -sWASM_WORKERS=0

# LDFLAGS += -g3
# LDFLAGS += -fdebug-compilation-dir=..
# LDFLAGS += -sSYSCALL_DEBUG=0
# LDFLAGS += -sRUNTIME_DEBUG=1

# LDFLAGS += -sDETERMINISTIC=1  # Doesn't seem to work

# LDFLAGS += -sDYLINK_DEBUG=1
# LDFLAGS += -sSOCKET_DEBUG=1
# LDFLAGS += -sFS_DEBUG=1
# LDFLAGS += -sWEBSOCKET_DEBUG=1
# LDFLAGS += -sASYNCIFY_DEBUG=2
# LDFLAGS += -sPTHREADS_DEBUG=1
# LDFLAGS += -sFETCH_DEBUG=1

# Makefile Rules
all: $(DIST_DIR)/index.mjs $(DIST_DIR)/index.d.ts

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(XS_DIR)/sources/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(MODULES_DIR)/data/text/decoder/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(MODULES_DIR)/data/text/encoder/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(MODULES_DIR)/data/base64/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wasm-wrapper.mjs: $(OBJECTS) $(SRC_DIR)/lib.js | $(BUILD_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(SRC_DIR)/wasm-wrapper.mjs: $(BUILD_DIR)/wasm-wrapper.mjs
	cp $< $@

$(SRC_DIR)/wasm-wrapper.wasm: $(BUILD_DIR)/wasm-wrapper.wasm
	cp $< $@

$(DIST_DIR)/wasm-wrapper.wasm: $(BUILD_DIR)/wasm-wrapper.wasm | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/index.mjs $(DIST_DIR)/index.js $(DIST_DIR)/index.d.mts: $(SRC_DIR)/*.mts $(SRC_DIR)/wasm-wrapper.mjs tsconfig.json $(SRC_DIR)/wasm-wrapper.wasm $(DIST_DIR)/wasm-wrapper.wasm | $(DIST_DIR)
	npx rollup --config rollup.config.mjs

$(DIST_DIR)/index.d.ts: $(DIST_DIR)/index.d.mts
	cp $< $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(DIST_DIR):
	mkdir -p $(DIST_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BUILD_DIR) $(DIST_DIR)
	rm -f $(SRC_DIR)/wasm-wrapper.mjs $(SRC_DIR)/wasm-wrapper.wasm

.PHONY: all clean
