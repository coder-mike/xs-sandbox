/*
This "wedge" unit comprises all the crap I had to bring in to get XS to work.

There seem to be lots of signature incompatibilities between xs.h and xsAll.h,
so a fair amount of this unit is just bringing in the few pieces of xsAll that I
need. There's also cases where xs implements something but makes it static, such
as fxRunLoop, so I can't actually call it and need to just copy the
implementation in here.

*/
#pragma once

#include "xs.h"

typedef int32_t xsSize;

#define xsInitializeSharedCluster() \
	fxInitializeSharedCluster()
#define xsTerminateSharedCluster() \
	fxTerminateSharedCluster()

extern void fxInitializeSharedCluster();
extern void fxTerminateSharedCluster();

extern void fxBeginMetering(xsMachine* the, xsBooleanValue (*callback)(xsMachine*, txU4), txU4 interval);
extern void fxCheckMetering(xsMachine* the);
extern void fxEndMetering(xsMachine* the);

#define xsRunLoop(_THE) \
	fxRunLoop(_THE)

mxImport void fxRunLoop(xsMachine* the);

#define xsGetCurrentMeter(_THE) \
	fxGetCurrentMeter(_THE)
#define xsSetCurrentMeter(_THE, _VALUE) \
	fxSetCurrentMeter(_THE, _VALUE)

xsUnsignedValue fxGetCurrentMeter(xsMachine* the);
void fxSetCurrentMeter(xsMachine* the, xsUnsignedValue value);

typedef struct sxProjection txProjection;
typedef struct sxSnapshot txSnapshot;

struct sxProjection {
	txProjection* nextProjection;
	xsSlot* heap;
	xsSlot* limit;
	size_t indexes[1];
};

struct sxSnapshot {
	char* signature;
	int signatureLength;
	xsCallback* callbacks;
	int callbacksLength;
	int (*read)(void* stream, void* address, size_t size);
	int (*write)(void* stream, void* address, size_t size);
	void* stream;
	int error;
	xsByte* firstChunk;
	txProjection* firstProjection;
	xsSlot* firstSlot;
	xsSize slotSize;
	xsSlot** slots;
};

// Defined in xsSnapshot.c
extern xsMachine* fxReadSnapshot(txSnapshot* snapshot, char* theName, void* theContext);
extern int fxWriteSnapshot(xsMachine* the, txSnapshot* snapshot);

