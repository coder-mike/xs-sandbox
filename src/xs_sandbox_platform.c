#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "xsAll.h"
#include "xsScript.h"
#include "xsSnapshot.h"


void fxAbort(txMachine* the, int status)
{
	switch (status) {
	case XS_STACK_OVERFLOW_EXIT:
		fxReport(the, "stack overflow\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_NOT_ENOUGH_MEMORY_EXIT:
		fxReport(the, "memory full\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_NO_MORE_KEYS_EXIT:
		fxReport(the, "not enough keys\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_TOO_MUCH_COMPUTATION_EXIT:
		fxReport(the, "too much computation\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_UNHANDLED_EXCEPTION_EXIT: {
		mxPush(mxException);
		txSlot *exc = the->stack;
		fxReport(the, "unhandled exception: %s\n", fxToString(the, &mxException));
		mxException = mxUndefined;
		mxTry(the) {
			mxOverflow(-8);
			mxPush(mxGlobal);
			fxGetID(the, fxFindName(the, "console"));
			fxCallID(the, fxFindName(the, "error"));
			mxPushStringC("Unhandled exception:");
			mxPushSlot(exc);
			fxRunCount(the, 2);
			mxPop();
		}
		mxCatch(the) {
			fprintf(stderr, "Unhandled exception %s\n", fxToString(the, exc));
		}
		the->abortStatus = status;
		fxExitToHost(the);
		mxPop();
		break;
	}
	case XS_UNHANDLED_REJECTION_EXIT: {
		mxPush(mxException);
		txSlot *exc = the->stack;
		fxReport(the, "unhandled rejection: %s\n", fxToString(the, &mxException));
		mxException = mxUndefined;
		mxTry(the) {
			mxOverflow(-8);
			mxPush(mxGlobal);
			fxGetID(the, fxFindName(the, "console"));
			fxCallID(the, fxFindName(the, "error"));
			mxPushStringC("UnhandledPromiseRejectionWarning:");
			mxPushSlot(exc);
			fxRunCount(the, 2);
			mxPop();
		}
		mxCatch(the) {
			fprintf(stderr, "Unhandled exception %s\n", fxToString(the, exc));
		}
		break;
	}
	default:
		fxReport(the, "fxAbort(%d) - %s\n", status, fxToString(the, &mxException));
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	}
}

void fxCreateMachinePlatform(txMachine* the)
{
	size_t MB = 1024 * 1024;
	the->allocationLimit = 256 * MB;
}

void fxDeleteMachinePlatform(txMachine* the)
{
}

txSlot* fxAllocateSlots(txMachine* the, txSize theCount)
{
	// printf("fxAllocateSlots: %i * %lu\n", theCount, sizeof(txSlot));
	return (txSlot*)c_malloc(theCount * sizeof(txSlot));
}

void fxFreeSlots(txMachine* the, void* theSlots)
{
	c_free(theSlots);
}

void* fxAllocateChunks(txMachine* the, txSize size)
{
	return c_malloc(size);
}

void fxFreeChunks(txMachine* the, void* theChunks)
{
  c_free(theChunks);
}

// TODO: Unused?
txScript* fxLoadScript(txMachine* the, txString path, txUnsigned flags)
{
	txParser _parser;
	txParser* parser = &_parser;
	txParserJump jump;
	FILE* file = NULL;
	txString name = NULL;
	char map[C_PATH_MAX];
	txScript* script = NULL;
	fxInitializeParser(parser, the, the->parserBufferSize, the->parserTableModulo);
	parser->firstJump = &jump;
	file = fopen(path, "r");
	if (c_setjmp(jump.jmp_buf) == 0) {
		mxParserThrowElse(file);
		parser->path = fxNewParserSymbol(parser, path);
		fxParserTree(parser, file, (txGetter)fgetc, flags, &name);
		fclose(file);
		file = NULL;
		if (name) {
			mxParserThrowElse(c_realpath(fxCombinePath(parser, path, name), map));
			parser->path = fxNewParserSymbol(parser, map);
			file = fopen(map, "r");
			mxParserThrowElse(file);
			fxParserSourceMap(parser, file, (txGetter)fgetc, flags, &name);
			fclose(file);
			file = NULL;
			if ((parser->errorCount == 0) && name) {
				mxParserThrowElse(c_realpath(fxCombinePath(parser, map, name), map));
				parser->path = fxNewParserSymbol(parser, map);
			}
		}
		fxParserHoist(parser);
		fxParserBind(parser);
		script = fxParserCode(parser);
	}
	if (file)
		fclose(file);
#ifdef mxInstrument
	if (the->peakParserSize < parser->total)
		the->peakParserSize = parser->total;
#endif
	fxTerminateParser(parser);
	return script;
}

// TODO: Unused?
void fxLoadModule(txMachine* the, txSlot* module, txID moduleID)
{
	char path[C_PATH_MAX];
	char real[C_PATH_MAX];
	txScript* script;
#ifdef mxDebug
	txUnsigned flags = mxDebugFlag;
#else
	txUnsigned flags = 0;
#endif
	c_strncpy(path, fxGetKeyName(the, moduleID), C_PATH_MAX - 1);
	path[C_PATH_MAX - 1] = 0;
	if (c_realpath(path, real)) {
		script = fxLoadScript(the, real, flags);
		if (script)
			fxResolveModule(the, module, moduleID, script, C_NULL, C_NULL);
	}
}

// TODO: Unused?
txID fxFindModule(txMachine* the, txSlot* realm, txID moduleID, txSlot* slot)
{
	char name[C_PATH_MAX];
	char path[C_PATH_MAX];
	txInteger dot = 0;
	txString slash;
	fxToStringBuffer(the, slot, name, sizeof(name));
	if (name[0] == '.') {
		if (name[1] == '/') {
			dot = 1;
		}
		else if ((name[1] == '.') && (name[2] == '/')) {
			dot = 2;
		}
	}
	if (dot) {
		if (moduleID == XS_NO_ID)
			return XS_NO_ID;
		c_strncpy(path, fxGetKeyName(the, moduleID), C_PATH_MAX - 1);
		path[C_PATH_MAX - 1] = 0;
		slash = c_strrchr(path, mxSeparator);
		if (!slash)
			return XS_NO_ID;
		if (dot == 2) {
			*slash = 0;
			slash = c_strrchr(path, mxSeparator);
			if (!slash)
				return XS_NO_ID;
		}
#if mxWindows
		{
			char c;
			char* s = name;
			while ((c = *s)) {
				if (c == '/')
					*s = '\\';
				s++;
			}
		}
#endif
	}
	else
		slash = path;
	*slash = 0;
	c_strcat(path, name + dot);
	return fxNewNameC(the, path);
}

void fxQueuePromiseJobs(txMachine* the)
{
	// TODO: What does this mean? Will this be a problem if there are multiple promise jobs queued?
	the->promiseJobs = 1;
}

void fxConnect(txMachine* the) {

}

void fxDisconnect(txMachine* the) {

}

txBoolean fxIsConnected(txMachine* the) {
	return 0;
}

txBoolean fxIsReadable(txMachine* the) {
	return 0;
}

void fxReceive(txMachine* the) {

}

void fxSend(txMachine* the, txBoolean more) {

}

