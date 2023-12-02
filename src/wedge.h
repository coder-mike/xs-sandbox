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
