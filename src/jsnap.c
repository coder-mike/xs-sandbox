#include "jsnap.h"
#include "xs.h"
#include "wedge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static const unsigned int meteringLimit = 1000000000; // TODO
static const int meteringInterval = 1; // TODO
static const int parserBufferSize = 1024 * 1024;
static const char SNAPSHOT_SIGNATURE[] = "xssandbox-1";
static char* MACHINE_NAME = "xsSandbox";

typedef enum ErrorCode {
  EC_OK_VALUE = 0, // Ok with return value
  EC_OK_UNDEFINED = 1, // Ok with return undefined
  EC_EXCEPTION = 2, // Returned exception message (string)
} ErrorCode;

static xsMachine* machine;

void initMachine();

// Function callable by the guest to send a command to the host
void host_sendMessage(xsMachine* the);

extern void sendMessage(uint8_t* buffer, size_t size);

#define snapshotCallbackCount 1
xsCallback snapshotCallbacks[snapshotCallbackCount] = {
  host_sendMessage,
};

static xsBooleanValue meteringCallback(xsMachine* the, xsUnsignedValue index)
{
  //printf("Metering callback: %u\n", index);
  if (index > meteringLimit) {
    //printf("Metering limit hit: %u (limit: %u)\n", index, meteringLimit);
    return 0;
  }
  return 1;
}

void populateGlobals(xsMachine* the) {
  xsBeginHost(machine);
	{
    xsVars(1);

    xsResult = xsNewHostFunction(host_sendMessage, 1);
    xsDefine(xsGlobal, xsID("sendMessage"), xsResult, xsDontEnum);
  }
  xsEndHost(machine);
}

typedef struct {
	xsSlot* slot;
	xsSize offset;
	xsSize size;
} txStringStream;

typedef int (*txGetter)(void*);
typedef txS4 txSize;
typedef char* txString;
typedef txU4 txUnsigned;

typedef struct {
	void* callback;
	txS1* symbolsBuffer;
	txSize symbolsSize;
	txS1* codeBuffer;
	txSize codeSize;
	txS1* hostsBuffer;
	txSize hostsSize;
	txString path;
	txS1 version[4];
} txScript;

txScript* fxParseScript(xsMachine* the, void* stream, txGetter getter, txUnsigned flags);
int fxStringGetter(void* theStream);

enum {
	mxCFlag = 1 << 0,
	mxDebugFlag = 1 << 1,
	mxEvalFlag = 1 << 2,
	mxProgramFlag = 1 << 3,
	mxStrictFlag = 1 << 4,
	mxSuperFlag = 1 << 5,
	mxTargetFlag = 1 << 6,
	mxFieldFlag = 1 << 15,
	mxFunctionFlag = 1 << 16,
	mxGeneratorFlag = 1 << 21,
};
#define c_strlen strlen

#define mxStringLength(_STRING) ((txSize)c_strlen(_STRING))

// TODO: Remove this function
/**
 * Process a message from the host. The message contained in `buffer` of length
 * `size` is processed and the result is stored in `out_buffer` of length
 * `out_size`.
 */
uint8_t* process_message(uint8_t* buffer, size_t size, uint32_t* out_size) {
  // Dummy "Hello world" implementation treats the buffer as a name and returns "Hello, <name>!"
  uint8_t* out_buffer = malloc(size + 8);
  if (out_buffer == NULL) {
    return NULL;
  }

  printf("Size of xsSlot: %lu\n", sizeof(xsSlot)); // TODO

  int n = snprintf((char*)out_buffer, size + 8, "Hello, %s!", buffer);
  if (n < 0) {
    free(out_buffer);
    out_buffer = NULL;
    return NULL;
  }

  *out_size = n;

  xsCreation _creation = {
    256 * 1024,       /* initialChunkSize     */
    256 * 1024,       /* incrementalChunkSize */
    32 * 1024,        /* initialHeapCount     */
    8 * 1024,         /* incrementalHeapCount */
    4 * 1024,         /* stackCount           */
    4 * 1024,         /* initialKeyCount      */
    4 * 1024,         /* incrementalKeyCount  */
    1993,             /* nameModulo           */
    127,              /* symbolModulo         */
    parserBufferSize, /* parserBufferSize     */
    1993,             /* parserTableModulo    */
  };
  xsCreation* creation = &_creation;

  printf("creating machine...\n");
  machine = xsCreateMachine(creation, "jsnap", NULL);
  populateGlobals(machine);

  printf("begin metering...\n");
  xsBeginMetering(machine, meteringCallback, meteringInterval);
  {
    xsSetCurrentMeter(machine, 10000);
    printf("begin host...\n");
    xsBeginHost(machine);
    {
      printf("host vars...\n");
      xsVars(3);
      printf("begin try...\n");
      xsTry {
        xsVar(0) = xsString("'Hello' + ', ' + 'from XS!'");

        printf("Parsing script\n");
        txStringStream aStream;
        aStream.slot = &xsVar(0);
        aStream.offset = 0;
        aStream.size = mxStringLength(xsToString(xsVar(0)));
        txScript* script = fxParseScript(the, &aStream, fxStringGetter, mxProgramFlag | mxEvalFlag);

        printf("Script prepared for eval\n");
        xsVar(1) = xsCall1(xsGlobal, xsID("eval"), xsVar(0));
        printf("Eval called\n");

        //const char* result = xsToString(xsVar(1));
        //printf("Result of eval: %s\n", result); // Print the result
      }
      xsCatch {
        xsVar(1) = xsException;
        printf("Caught an exception\n");
        printf("Exception: %s\n", xsToString(xsException));
        if (xsTypeOf(xsException) != xsUndefinedType) {
          xsVar(1) = xsException;
          xsException = xsUndefined;
        }
      }
    }
    xsRunLoop(machine);
    xsEndHost(machine);
  }
  xsEndMetering(machine);
  printf("end metering...\n");


  return out_buffer;
}

#define INITIAL_SNAPSHOT_CAPACITY 32 * 1024

typedef struct TsSnapshotStream {
  uint8_t* data;
  size_t offset;
  size_t capacity;
} TsSnapshotStream;

int snapshotReadChunk(void* stream, void* address, size_t size) {
  // printf("snapshotReadChunk %p %p %lu\n", stream, address, size);
  TsSnapshotStream* snapshotStream = (TsSnapshotStream*)stream;

  if (snapshotStream->offset + size > snapshotStream->capacity) {
    // printf("snapshotReadChunk: offset %lu + size %lu > capacity %lu\n", snapshotStream->offset, size, snapshotStream->capacity);
    return 1;
  }

  memcpy(address, snapshotStream->data + snapshotStream->offset, size);
  snapshotStream->offset += size;

  return 0;
}

int snapshotWriteChunk(void* stream, void* address, size_t size) {
  // printf("snapshotWriteChunk %p %p %lu\n", stream, address, size);
  TsSnapshotStream* snapshotStream = (TsSnapshotStream*)stream;
  size_t newSize = snapshotStream->offset + size;

  if (newSize > snapshotStream->capacity) {
    size_t newCapacity = snapshotStream->capacity * 2;
    // printf("Reallocating snapshot stream from %lu to %lu\n", snapshotStream->capacity, newCapacity);
    if (newCapacity < newSize) {
      newCapacity = newSize;
    }
    uint8_t* newData = realloc(snapshotStream->data, newCapacity);
    if (newData == NULL) {
      return 1;
    }
    snapshotStream->data = newData;
    snapshotStream->capacity = newCapacity;
  }

  memcpy(snapshotStream->data + snapshotStream->offset, address, size);
  snapshotStream->offset = newSize;

  return 0;
}

int restoreSnapshot(uint8_t* buffer, size_t size) {
  TsSnapshotStream stream = {
    .data = buffer,
    .offset = 0,
    .capacity = size,
  };

  txSnapshot snapshotOpts = {
		(char*)SNAPSHOT_SIGNATURE,
		sizeof(SNAPSHOT_SIGNATURE) - 1,
		snapshotCallbacks,
		snapshotCallbackCount,
		snapshotReadChunk,
		NULL,
		&stream,
		0,
		NULL,
		NULL,
		NULL,
		0,
		NULL
	};

  machine = fxReadSnapshot(&snapshotOpts, MACHINE_NAME, NULL);

  if (machine) {
    return 0;
  } else {
    return 1;
  }
}

int takeSnapshot(uint8_t** out_buffer, size_t* out_size) {
  *out_size = 0;

  TsSnapshotStream stream = {
    .data = malloc(INITIAL_SNAPSHOT_CAPACITY),
    .offset = 0,
    .capacity = INITIAL_SNAPSHOT_CAPACITY,
  };

  txSnapshot snapshotOpts = {
		(char*)SNAPSHOT_SIGNATURE,
		sizeof(SNAPSHOT_SIGNATURE) - 1,
		snapshotCallbacks,
		snapshotCallbackCount,
		NULL,
		snapshotWriteChunk,
		&stream,
		0,
		NULL,
		NULL,
		NULL,
		0,
		NULL
	};

  int result = fxWriteSnapshot(machine, &snapshotOpts);

  *out_buffer = stream.data;
  *out_size = stream.offset;

  return result;
}

void initMachine() {
  xsInitializeSharedCluster();

  xsCreation _creation = {
    256 * 1024,       /* initialChunkSize     */
    256 * 1024,       /* incrementalChunkSize */
    32 * 1024,        /* initialHeapCount     */
    8 * 1024,         /* incrementalHeapCount */
    4 * 1024,         /* stackCount           */
    4 * 1024,         /* initialKeyCount      */
    4 * 1024,         /* incrementalKeyCount  */
    1993,             /* nameModulo           */
    127,              /* symbolModulo         */
    parserBufferSize, /* parserBufferSize     */
    1993,             /* parserTableModulo    */
  };
  xsCreation* creation = &_creation;

  machine = xsCreateMachine(creation, MACHINE_NAME, NULL);
  populateGlobals(machine);
}

ErrorCode sandboxInput(uint8_t* payload, uint32_t** out_buffer, uint32_t* out_size, int action) {
  *out_buffer = NULL;
  *out_size = 0;
  ErrorCode code = EC_OK_UNDEFINED;
  xsBeginMetering(machine, meteringCallback, meteringInterval);
  {
    xsSetCurrentMeter(machine, 10000);
    xsBeginHost(machine);
    {
      xsVars(3);
      xsTry {
        xsVar(0) = xsString(payload);

        if (action == 0) {
          xsVar(1) = xsCall1(xsGlobal, xsID("eval"), xsVar(0));
        } else {
          xsVar(1) = xsGet(xsGlobal, xsID("JSON"));
          xsVar(1) = xsCall1(xsVar(1), xsID("parse"), xsVar(0));
          xsVar(0) = xsGet(xsGlobal, xsID("receiveMessage"));
          if (xsTypeOf(xsVar(0)) != xsUndefinedType) {
            xsVar(1) = xsCall1(xsGlobal, xsID("receiveMessage"), xsVar(1));
          }
        }

        char* result;
        if (xsTypeOf(xsVar(1)) != xsUndefinedType) {
          xsVar(0) = xsGet(xsGlobal, xsID("JSON"));
          xsVar(0) = xsCall1(xsVar(0), xsID("stringify"), xsVar(1));
          result = xsToString(xsVar(0));
          *out_buffer = malloc(strlen(result) + 1);
          strcpy((char*)*out_buffer, result);
          *out_size = strlen(result);
          code = EC_OK_VALUE;
        } else {
          *out_buffer = NULL;
          *out_size = 0;
          code = EC_OK_UNDEFINED;
        }
      }
      xsCatch {
        // printf("xsCatch\n");
        code = EC_EXCEPTION;
        xsSetCurrentMeter(machine, 1000000000);
        char* message;
        if (xsTypeOf(xsException) != xsUndefinedType) {
          message = xsToString(xsException);
        } else {
          message = "Unknown exception";
        }
        *out_buffer = malloc(strlen(message) + 1);
        *out_size = strlen(message);
        strcpy((char*)*out_buffer, message);
        xsException = xsUndefined;
      }
    }
    // TODO: What happens when there's an exception and we try run the loop? And
    // is it possible for the loop to throw more exceptions?
    // TODO: What happens if the meter ran out before we process the loop?
    xsRunLoop(machine);
    xsEndHost(machine);
  }
  xsEndMetering(machine);
  return code;
}

void host_sendMessage(xsMachine* the) {
  xsVars(1);
  xsVar(0) = xsGet(xsGlobal, xsID("JSON"));
  xsVar(0) = xsCall1(xsVar(0), xsID("stringify"), xsArg(0));
  const char* message = xsToString(xsVar(0));
  size_t messageSize = strlen(message);
  sendMessage((uint8_t*)message, messageSize);
}

ErrorCode receiveMessage(uint8_t* payload, uint32_t** out_buffer, uint32_t* out_size) {
  *out_buffer = NULL;
  *out_size = 0;
  ErrorCode code = EC_OK_UNDEFINED;
  xsBeginMetering(machine, meteringCallback, meteringInterval);
  {
    xsSetCurrentMeter(machine, 10000);
    xsBeginHost(machine);
    {
      xsVars(3);
      xsTry {
        xsVar(0) = xsString(payload);

        xsVar(1) = xsGet(xsGlobal, xsID("JSON"));
        xsVar(0) = xsCall1(xsVar(0), xsID("parse"), xsVar(0));

        xsVar(1) = xsGet(xsGlobal, xsID("receiveMessage"));
        if (xsTypeOf(xsVar(1)) != xsUndefinedType) {
          xsVar(1) = xsCall1(xsGlobal, xsID("receiveMessage"), xsUndefined);
        }

        char* result;
        if (xsTypeOf(xsVar(1)) != xsUndefinedType) {
          xsVar(0) = xsGet(xsGlobal, xsID("JSON"));
          xsVar(0) = xsCall1(xsVar(0), xsID("stringify"), xsVar(1));
          result = xsToString(xsVar(0));
          *out_buffer = malloc(strlen(result) + 1);
          strcpy((char*)*out_buffer, result);
          *out_size = strlen(result);
          code = EC_OK_VALUE;
        } else {
          *out_buffer = NULL;
          *out_size = 0;
          code = EC_OK_UNDEFINED;
        }
      }
      xsCatch {
        code = EC_EXCEPTION;
        xsSetCurrentMeter(machine, 1000000000);
        // TODO: Test returning exceptions
        char* message;
        if (xsTypeOf(xsException) != xsUndefinedType) {
          message = xsToString(xsException);
        } else {
          message = "Unknown exception";
        }
        *out_buffer = malloc(strlen(message) + 1);
        *out_size = strlen(message);
        strcpy((char*)*out_buffer, message);
        xsException = xsUndefined;
      }
    }
    // TODO: What happens when there's an exception and we try run the loop? And
    // is it possible for the loop to throw more exceptions?
    // TODO: What happens if the meter ran out before we process the loop?
    xsRunLoop(machine);
    xsEndHost(machine);
  }
  xsEndMetering(machine);
  return code;
}