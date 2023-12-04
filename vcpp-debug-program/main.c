#include <stdio.h>

#include "jsnap.h"

int main(int argc, char** argv) {
  const char* message = "World";
  uint32_t size = 0;
  uint8_t* result = process_message((uint8_t*)message, strlen(message), &size);
  if (result) {
    printf("Result: %s\n", (char*)result);
  }
  else {
    printf("No result\n");
  }
  return 0;
}