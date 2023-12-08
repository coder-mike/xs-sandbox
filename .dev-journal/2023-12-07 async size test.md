Measuring the size of an async function in XS.

The opcode XS_CODE_START_ASYNC seems to allocate most of the slots for an async function.

It starts by calling `gxDefaults.newAsyncInstance` which allocates 37 slots and then a chunk of 256 which looks to me to be to persist the stack variables.

The await itself then seems to allocate another 6 slots (XS_CODE_AWAIT). And it calls `fxRenewChunk` with a size of 0x110, which adjusts to 0x118. The "delta" there is 0x10 which is a slot size. I honestly don't know what it's doing or if it's even using more memory.

Anyway, `(37+6)*16 + 256` is 944 bytes in total. Possibly the fxRenewChunk adds more bytes but not sure.

This is in the same order of magnitude as the other test I did, which is to measure the snapshot size before and after instantiating various async function calls and I measured the snapshot to grow by 952 bytes per async function, plus some number for local variables.



