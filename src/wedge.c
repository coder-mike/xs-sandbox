/*

# WARNING

Do not include "wedge.h". It seems that XS has a separation between units that
use xs.h and units that use xsAll.h. Units can't include both because the
declarations are literally inconsistent. `xs.h` is the public interface and
`xsAll.h` is the private interface. `wedge.h` here is intended to be the
"public" interface and `wedge.c` is internal.
*/

#include "xsAll.h"

void fxRunLoop(txMachine* the)
{

	do {
		while (the->promiseJobs) {
			the->promiseJobs = 0;
			fxRunPromiseJobs(the);
		}
		fxEndJob(the);
	} while (the->promiseJobs);
	fxCheckUnhandledRejections(the, 1);
}

txUnsigned fxGetCurrentMeter(txMachine* the)
{
	return the->meterIndex;
}

void fxSetCurrentMeter(txMachine* the, txUnsigned value)
{
	the->meterIndex = value;
}
