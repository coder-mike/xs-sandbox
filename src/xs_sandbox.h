#pragma once

#include <stdint.h>

#include "xs.h"

typedef enum ErrorCode {
  EC_OK_VALUE = 0, // Ok with return value
  EC_OK_UNDEFINED = 1, // Ok with return undefined
  EC_EXCEPTION = 2, // Returned exception message (string)
} ErrorCode;

// Called by host
void initMachine();
ErrorCode sandboxInput(uint8_t* payload, uint32_t** out_buffer, uint32_t* out_size, int action);
ErrorCode receiveMessage(uint8_t* payload, uint32_t** out_buffer, uint32_t* out_size);
