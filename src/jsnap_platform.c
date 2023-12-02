#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "xsAll.h"
#include "xsScript.h"
#include "xsSnapshot.h"

// TODO
#ifndef mxReserveChunkSize
	#define mxReserveChunkSize 1024 * 1024 * 1024
#endif


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
#ifdef mxDebug
#ifdef mxInstrument
#else
	the->connection = mxNoSocket;
#endif
#endif
  // TODO
	size_t GB = 1024 * 1024 * 1024;
	the->allocationLimit = 2 * GB;
}

static void adjustSpaceMeter(txMachine* the, txSize theSize)
{
	size_t previous = the->allocatedSpace;
	the->allocatedSpace += theSize;
	if (the->allocatedSpace > the->allocationLimit ||
		// overflow?
		the->allocatedSpace < previous) {
		fxAbort(the, XS_NOT_ENOUGH_MEMORY_EXIT);
	}
}

txSlot* fxAllocateSlots(txMachine* the, txSize theCount)
{
	adjustSpaceMeter(the, theCount * sizeof(txSlot));
	return (txSlot*)c_malloc(theCount * sizeof(txSlot));
}

void fxFreeSlots(txMachine* the, void* theSlots)
{
	c_free(theSlots);
}

static txSize gxPageSize = 0;

static txSize fxRoundToPageSize(txMachine* the, txSize size)
{
	txSize modulo;
	if (!gxPageSize) {
#if mxWindows
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		gxPageSize = (txSize)info.dwAllocationGranularity;
#else
		gxPageSize = getpagesize();
#endif
	}
	modulo = size & (gxPageSize - 1);
	if (modulo)
		size = fxAddChunkSizes(the, size, gxPageSize - modulo);
	return size;
}

void* fxAllocateChunks(txMachine* the, txSize size)
{
	txByte* base;
	txByte* result;
	adjustSpaceMeter(the, size);
	if (the->firstBlock) {
		base = (txByte*)(the->firstBlock);
		result = (txByte*)(the->firstBlock->limit);
	}
	else
#if mxWindows
		base = result = VirtualAlloc(NULL, mxReserveChunkSize, MEM_RESERVE, PAGE_READWRITE);
#else
		base = result = mmap(NULL, mxReserveChunkSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
	if (result) {
		txSize current = (txSize)(result - base);
		size = fxAddChunkSizes(the, current, size);
		current = fxRoundToPageSize(the, current);
		size = fxRoundToPageSize(the, size);
#if mxWindows
		if (!VirtualAlloc(base + current, size - current, MEM_COMMIT, PAGE_READWRITE))
#else
		if (size > mxReserveChunkSize)
			result = NULL;
		else if (mprotect(base + current, size - current, PROT_READ | PROT_WRITE))
#endif
			result = NULL;
	}
	return result;
}

void fxFreeChunks(txMachine* the, void* theChunks)
{
#if mxWindows
	VirtualFree(theChunks, 0, MEM_RELEASE);
#else
	munmap(theChunks, mxReserveChunkSize);
#endif
}


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
	the->promiseJobs = 1;
}
