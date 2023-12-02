#include "jsnap.h"
#include "xs.h"
#include "wedge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static const int meteringLimit = 1000000000; // TODO
static const int meteringInterval = 1000; // TODO
static const int parserBufferSize = 8192 * 1024;

void initMachine();

static xsBooleanValue xsMeteringCallback(xsMachine* the, xsUnsignedValue index)
{
 if (index > meteringLimit) {
  return 0;
 }
 return 1;
}

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

  int n = snprintf((char*)out_buffer, size + 8, "Hello, %s!", buffer);
  if (n < 0) {
    free(out_buffer);
    out_buffer = NULL;
    return NULL;
  }

  initMachine();

  *out_size = n;

  return out_buffer;
}

static xsMachine* machine;

void initMachine() {
  xsInitializeSharedCluster();


  xsCreation _creation = {
    32 * 1024 * 1024, /* initialChunkSize     */
    4 * 1024 * 1024,  /* incrementalChunkSize */
    256 * 1024,       /* initialHeapCount     */
    128 * 1024,       /* incrementalHeapCount */
    4096,             /* stackCount           */
    32000,            /* initialKeyCount      */
    8000,             /* incrementalKeyCount  */
    1993,             /* nameModulo           */
    127,              /* symbolModulo         */
    parserBufferSize, /* parserBufferSize     */
    1993,             /* parserTableModulo    */
  };
  xsCreation* creation = &_creation;

  machine = xsCreateMachine(creation, "jsnap", NULL);

 xsBeginMetering(machine, xsMeteringCallback, meteringInterval);
  {
    xsBeginHost(machine);
    {

    }
    xsRunLoop(machine);
    xsEndHost(machine);
  }
 xsEndMetering(machine);
}
