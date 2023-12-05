# Compiler and Output Settings
CC=emcc
OUT_DIR=dist
OBJ_DIR=obj

# Directories
SRC_DIR=src
XS_DIR=moddable/xs
MODULES_DIR=moddable/modules

# Source Files
SOURCES := \
  $(SRC_DIR)/jsnap.c \
  $(SRC_DIR)/wedge.c \
  $(SRC_DIR)/jsnap_platform.c \
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
          -DXSPLATFORM=\"jsnap_platform.h\" \
          -DmxMetering=1 \
          -DmxParse=1 \
          -DmxRun=1 \
          -DmxSloppy=1 \
          -DmxSnapshot=1 \
          -DmxRegExpUnicodePropertyEscapes=1 \
          -DmxStringNormalize=1 \
          -DmxMinusZero=1 \
          -DmxDebug=1 \
          -DWASM_BUILD=1 \
          -I$(XS_DIR)/includes \
          -I$(XS_DIR)/platforms \
          -I$(XS_DIR)/sources \
          -I$(SRC_DIR) \
          -O0

CFLAGS += -g3
CFLAGS += -fdebug-compilation-dir=..
# CFLAGS += -DmxNoConsole=1

# The bounds checking seems to enable `fxCheckCStack` which doesn't work in WASM
CFLAGS += -DmxBoundsCheck=0

# Linker Flags
LDFLAGS := -sINITIAL_MEMORY=1048576 \
           -sALLOW_MEMORY_GROWTH \
           -sSUPPORT_LONGJMP=1 \
           -sMODULARIZE=1 \
           -sEXPORT_ES6=1 \
           -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
           -sEXPORTED_FUNCTIONS='["_process_message", "_malloc", "_free"]' \
           --use-preload-cache

LDFLAGS += -g3
LDFLAGS += -fdebug-compilation-dir=..
LDFLAGS += -sSTACK_OVERFLOW_CHECK=1
LDFLAGS += -sASSERTIONS=2
LDFLAGS += -sSAFE_HEAP=1
# LDFLAGS += -sSINGLE_FILE

# Makefile Rules
all: $(OUT_DIR)/mylib.mjs

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(XS_DIR)/sources/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(MODULES_DIR)/data/text/decoder/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(MODULES_DIR)/data/text/encoder/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(MODULES_DIR)/data/base64/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/mylib.mjs: $(OBJECTS)
	@mkdir -p $(OUT_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

clean:
	rm -rf $(OBJ_DIR) $(OUT_DIR)

.PHONY: all clean
