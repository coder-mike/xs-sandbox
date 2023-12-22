#include "xs_sandbox.h"
#include "xs.h"
#include "wedge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define INITIAL_SNAPSHOT_CAPACITY 32 * 1024

static uint32_t meteringLimit = 0;
static uint32_t meteringInterval = 0;
static uint32_t lastMeterValue = 0;
static const int parserBufferSize = 1024 * 1024;
static const char SNAPSHOT_SIGNATURE[] = "xs-sandbox-1";
static char* MACHINE_NAME = "xs-sandbox";

typedef struct TsSnapshotStream {
  uint8_t* data;
  size_t offset;
  size_t capacity;
} TsSnapshotStream;

static xsMachine* machine;
static bool active = false;

// Function callable by the guest to send a command to the host
void host_sendMessage(xsMachine* the);
void host_consoleLog(xsMachine* the);
void host_consoleWarn(xsMachine* the);
void host_consoleError(xsMachine* the);
void host_consoleOutput(xsMachine* the, int level);

extern ErrorCode sendMessage(uint8_t* buffer, size_t size, uint8_t** outputPtrPtr, size_t* outputSizePtr);
extern void consoleLog(uint8_t* argsAsJson, size_t len, int level);

#define snapshotCallbackCount 2
xsCallback snapshotCallbacks[snapshotCallbackCount] = {
  host_sendMessage,
  host_consoleLog,
};

static xsBooleanValue meteringCallback(xsMachine* the, xsUnsignedValue index) {
  if (!meteringLimit) return 1;
  lastMeterValue = index;
  return index < meteringLimit;
}

uint32_t getMeteringLimit() {
  return meteringLimit;
}

void setMeteringLimit(uint32_t limit) {
  meteringLimit = limit;
}

uint32_t getMeteringInterval() {
  return meteringInterval;
}

void setMeteringInterval(uint32_t interval) {
  meteringInterval = interval;
}

uint32_t getActive() {
  return active;
}

uint32_t getMeteringCount() {
  if (active) {
    return xsGetCurrentMeter(machine);
  } else {
    return lastMeterValue;
  }
}

void populateGlobals(xsMachine* the) {
  xsBeginHost(machine);
	{
    xsVars(2);

    // Global sendMessage
    xsVar(0) = xsNewHostFunction(host_sendMessage, 1);
    xsDefine(xsGlobal, xsID("sendMessage"), xsVar(0), xsDontEnum);

    // Create global console object
    xsVar(0) = xsNewObject();
    xsDefine(xsGlobal, xsID("console"), xsVar(0), xsDontEnum);

    // Create console.log function
    xsVar(1) = xsNewHostFunction(host_consoleLog, 1);
    xsDefine(xsVar(0), xsID("log"), xsVar(1), xsDontEnum);

    // I decided not to support console.warn because it's less commonly used,
    // and not to support console.error because Error objects don't serialize
    // well (and errors would be a common thing to pass to console.error).
  }
  xsEndHost(machine);
}

int snapshotReadChunk(void* stream, void* address, size_t size) {
  TsSnapshotStream* snapshotStream = (TsSnapshotStream*)stream;

  if (snapshotStream->offset + size > snapshotStream->capacity) {
    return 1;
  }

  memcpy(address, snapshotStream->data + snapshotStream->offset, size);
  snapshotStream->offset += size;

  return 0;
}

int snapshotWriteChunk(void* stream, void* address, size_t size) {
  TsSnapshotStream* snapshotStream = (TsSnapshotStream*)stream;
  size_t newSize = snapshotStream->offset + size;

  if (newSize > snapshotStream->capacity) {
    size_t newCapacity = snapshotStream->capacity * 2;
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

/**
 * Handle input from host when reentering the sandbox
 */
static ErrorCode sandboxInputReenter(uint8_t* payload, uint32_t** out_buffer, uint32_t* out_size, int action) {
  *out_buffer = NULL;
  *out_size = 0;
  ErrorCode code = EC_OK_UNDEFINED;

  xsMachine* the = machine;
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
    code = EC_EXCEPTION;
    xsSetCurrentMeter(machine, 1000000000);
    // New object to send serialized error
    xsVar(0) = xsNewObject();
    if (xsTypeOf(xsException) != xsUndefinedType) {
      if (xsIsInstanceOf(xsException, xsErrorPrototype)) {
        // Copy message property
        xsVar(1) = xsGet(xsException, xsID("message"));
        xsSet(xsVar(0), xsID("message"), xsVar(1));
        // Copy stack property
        xsVar(1) = xsGet(xsException, xsID("stack"));
        xsSet(xsVar(0), xsID("stack"), xsVar(1));
        // Copy name property
        xsVar(1) = xsGet(xsException, xsID("name"));
        xsSet(xsVar(0), xsID("name"), xsVar(1));
      } else {
        // Just do `exception.toString()` to infer a message
        xsVar(1) = xsCall0(xsException, xsID("toString"));
        xsSet(xsVar(0), xsID("message"), xsVar(1));
      }
    } else {
      xsVar(1) = xsString("Unknown error");
      xsSet(xsVar(0), xsID("message"), xsVar(1));
    }
    // Serialize error
    xsVar(1) = xsGet(xsGlobal, xsID("JSON"));
    xsVar(0) = xsCall1(xsVar(1), xsID("stringify"), xsVar(0));
    char* message = xsToString(xsVar(0));
    *out_buffer = malloc(strlen(message) + 1);
    *out_size = strlen(message);
    strcpy((char*)*out_buffer, message);
    xsException = xsUndefined;
  }
  return code;
}

/**
 * Handle input from host (evaluate or sendMessage)
 */
ErrorCode sandboxInput(uint8_t* payload, uint32_t** out_buffer, uint32_t* out_size, int action) {
  if (active) {
    return sandboxInputReenter(payload, out_buffer, out_size, action);
  }
  active = true;
  *out_buffer = NULL;
  *out_size = 0;
  ErrorCode code = EC_OK_UNDEFINED;
  xsBeginMetering(machine, meteringCallback, meteringInterval);
  {
    xsBeginHost(machine);
    {
      code = sandboxInputReenter(payload, out_buffer, out_size, action);
    }
    // Note: the run loop can't throw an exception because the only way to
    // enqueue jobs is as promise continuations, which are specified to just
    // reject the outer promise.
    //
    // Note: if the meter expires while in the run loop, it will jump to
    // xsEndMetering and sandboxInput will return EC_METERING_LIMIT_REACHED.
    xsRunLoop(machine);
    xsEndHost(machine);
    lastMeterValue = xsGetCurrentMeter(machine);
  }
  xsEndMetering(machine);

  if (meteringLimit && (lastMeterValue >= meteringLimit)) {
    code = EC_METERING_LIMIT_REACHED;
    // It's possible that we already had a return value. E.g. if
    // sandboxInputReenter populated a return value and then the meter was
    // reached in the run loop.
    if (*out_buffer) {
      free(*out_buffer);
      *out_buffer = NULL;
      *out_size = 0;
    }
  }

  active = false;
  return code;
}

void host_sendMessage(xsMachine* the) {
  xsVars(2);
  xsVar(0) = xsGet(xsGlobal, xsID("JSON"));
  xsVar(0) = xsCall1(xsVar(0), xsID("stringify"), xsArg(0));
  const char* message = xsToString(xsVar(0));
  size_t messageSize = strlen(message);
  size_t outputSize = 0;
  uint8_t* outputPtr = NULL;
  ErrorCode code = sendMessage((uint8_t*)message, messageSize, &outputPtr, &outputSize);

  if (code == EC_OK_UNDEFINED) {
    xsResult = xsUndefined;
    return;
  }

  xsTry {
    xsVar(0) = xsString((char*)outputPtr);
    xsVar(1) = xsGet(xsGlobal, xsID("JSON"));
    xsVar(0) = xsCall1(xsVar(1), xsID("parse"), xsVar(0));
    if (code == EC_OK_VALUE) {
      xsResult = xsVar(0);
    } else if (code == EC_EXCEPTION) {
      xsVar(0) = xsGet(xsVar(0), xsID("message"));
      xsVar(0) = xsNew1(xsGlobal, xsID("Error"), xsVar(0));
      xsThrow(xsVar(0));
    } else {
      // Should not get here
      xsResult = xsUndefined;
    }
    free(outputPtr);
    outputPtr = NULL;
  }
  xsCatch {
    // Catch JSON parse error or other exception
    free(outputPtr);
		xsThrow(xsException);
  }
}


void host_consoleLog(xsMachine* the) {
  host_consoleOutput(the, 0);
}

void host_consoleWarn(xsMachine* the) {
  host_consoleOutput(the, 1);
}

void host_consoleError(xsMachine* the) {
  host_consoleOutput(the, 2);
}

void host_consoleOutput(xsMachine* the, int level) {
  // Stringify the args into a JSON array
  xsVars(3);
	xsIntegerValue c = xsToInteger(xsArgc);
  xsVar(0) = xsNewArray(c);
  for (int i = 0; i < c; i++) {
    xsVar(1) = xsInteger(i);
    xsSetAt(xsVar(0), xsVar(1), xsArg(i));
  }
  xsVar(1) = xsGet(xsGlobal, xsID("JSON"));
  xsVar(0) = xsCall1(xsVar(1), xsID("stringify"), xsVar(0));

  char* str = xsToString(xsVar(0));
  size_t len = strlen(str);
  char* buffer = malloc(len + 1);
  strcpy(buffer, str);

  consoleLog((uint8_t*)buffer, len, level);

  free(buffer);
}