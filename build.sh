SRC_DIR=src
OUT_DIR=out
XS_DIR=moddable/xs
MODULES_DIR=moddable/modules

emcc \
  $SRC_DIR/glue.c \
  $SRC_DIR/jsnap_platform.c \
  $XS_DIR/sources/xsAll.c \
  $XS_DIR/sources/xsAPI.c \
  $XS_DIR/sources/xsArguments.c \
  $XS_DIR/sources/xsArray.c \
  $XS_DIR/sources/xsAtomics.c \
  $XS_DIR/sources/xsBigInt.c \
  $XS_DIR/sources/xsBoolean.c \
  $XS_DIR/sources/xsCode.c \
  $XS_DIR/sources/xsCommon.c \
  $XS_DIR/sources/xsDataView.c \
  $XS_DIR/sources/xsDate.c \
  $XS_DIR/sources/xsDebug.c \
  $XS_DIR/sources/xsDefaults.c \
  $XS_DIR/sources/xsError.c \
  $XS_DIR/sources/xsFunction.c \
  $XS_DIR/sources/xsGenerator.c \
  $XS_DIR/sources/xsGlobal.c \
  $XS_DIR/sources/xsJSON.c \
  $XS_DIR/sources/xsLexical.c \
  $XS_DIR/sources/xsLockdown.c \
  $XS_DIR/sources/xsMapSet.c \
  $XS_DIR/sources/xsMarshall.c \
  $XS_DIR/sources/xsMath.c \
  $XS_DIR/sources/xsMemory.c \
  $XS_DIR/sources/xsModule.c \
  $XS_DIR/sources/xsNumber.c \
  $XS_DIR/sources/xsObject.c \
  $XS_DIR/sources/xsPlatforms.c \
  $XS_DIR/sources/xsProfile.c \
  $XS_DIR/sources/xsPromise.c \
  $XS_DIR/sources/xsProperty.c \
  $XS_DIR/sources/xsProxy.c \
  $XS_DIR/sources/xsRegExp.c \
  $XS_DIR/sources/xsRun.c \
  $XS_DIR/sources/xsScope.c \
  $XS_DIR/sources/xsScript.c \
  $XS_DIR/sources/xsSnapshot.c \
  $XS_DIR/sources/xsSourceMap.c \
  $XS_DIR/sources/xsString.c \
  $XS_DIR/sources/xsSymbol.c \
  $XS_DIR/sources/xsSyntaxical.c \
  $XS_DIR/sources/xsTree.c \
  $XS_DIR/sources/xsType.c \
  $XS_DIR/sources/xsdtoa.c \
  $XS_DIR/sources/xsre.c \
  $XS_DIR/sources/xsmc.c \
  $MODULES_DIR/data/text/decoder/textdecoder.c \
  $MODULES_DIR/data/text/encoder/textencoder.c \
  $MODULES_DIR/data/base64/modBase64.c \
  -fno-common \
	-DINCLUDE_XSPLATFORM \
  -DXSPLATFORM=\"jsnap_platform.h\" \
	-DmxLockdown=1 \
	-DmxMetering=1 \
	-DmxDebug=1 \
	-UmxInstrument \
	-DmxNoConsole=1 \
	-DmxBoundsCheck=1 \
	-DmxParse=1 \
	-DmxRun=1 \
	-DmxSloppy=1 \
	-DmxSnapshot=1 \
	-DmxRegExpUnicodePropertyEscapes=1 \
	-DmxStringNormalize=1 \
	-DmxMinusZero=1 \
	-I$XS_DIR/includes \
	-I$XS_DIR/platforms \
	-I$XS_DIR/sources \
	-I$SRC_DIR \
  -o $OUT_DIR/mylib.mjs \
  -s SUPPORT_LONGJMP=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORTED_RUNTIME_METHODS=ccall,cwrap \
  -s EXPORTED_FUNCTIONS='["_process_message", "_malloc", "_free"]'


