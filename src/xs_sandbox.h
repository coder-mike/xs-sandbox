#pragma once

#include <stdint.h>

#include "xs.h"

uint8_t* process_message(uint8_t* buffer, size_t size, uint32_t* out_size);
