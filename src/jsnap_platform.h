#pragma once

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#undef mxLittleEndian
#define mxLittleEndian 1

#undef mxLinux
#define mxLinux 1

#define mxExport extern
#define mxImport extern
#define XS_FUNCTION_NORETURN __attribute__((noreturn))

typedef int txSocket;
#define mxNoSocket -1
#define mxUseGCCAtomics 1
#define mxUsePOSIXThreads 1

#define mxMachinePlatform \
	int abortStatus; \
	int promiseJobs; \
	void* timerJobs; \
	void* waiterCondition; \
	void* waiterData; \
	void* waiterLink; \
	size_t allocationLimit; \
	size_t allocatedSpace;

#define mxUseDefaultBuildKeys 1
#define mxUseDefaultParseScript 1
#define mxUseDefaultSharedChunks 1

#define mx32bitID 1
#define mxCESU8 1


