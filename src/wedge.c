/*

# WARNING

Do not include "wedge.h". It seems that XS has a separation between units that
use xs.h and units that use xsAll.h. Units can't include both because the
declarations are literally inconsistent. `xs.h` is the public interface and
`xsAll.h` is the private interface. `wedge.h` here is intended to be the
"public" interface and `wedge.c` is internal.
*/

#include "xsAll.h"

typedef struct sxJob txJob;

struct sxJob {
	txJob* next;
	txMachine* the;
	txNumber when;
	txSlot self;
	txSlot function;
	txSlot argument;
	txNumber interval;
};

void fxRunLoop(txMachine* the)
{
	c_timeval tv;
	txNumber when;
	txJob* job;
	txJob** address;
	for (;;) {
		while (the->promiseJobs) {
			the->promiseJobs = 0;
			fxRunPromiseJobs(the);
		}
		fxEndJob(the);
		if (the->promiseJobs) {
			continue;
		}
		c_gettimeofday(&tv, NULL);
		when = ((txNumber)(tv.tv_sec) * 1000.0) + ((txNumber)(tv.tv_usec) / 1000.0);
		address = (txJob**)&(the->timerJobs);
		if (!*address)
			break;
		while ((job = *address)) {
			txMachine* the = job->the;
			if (the) {
				if (job->when <= when) {
					fxBeginHost(the);
					mxTry(the) {
						mxPushUndefined();
						mxPush(job->function);
						mxCall();
						mxPush(job->argument);
						mxRunCount(1);
						mxPop();
						if (job->the) {
							if (job->interval) {
								job->when += job->interval;
							}
							else {
								fxAccess(the, &job->self);
								*mxResult = the->scratch;
								fxForget(the, &job->self);
								fxSetHostData(the, mxResult, NULL);
								job->the = NULL;
							}
						}
					}
					mxCatch(the) {
						fxAccess(the, &job->self);
						*mxResult = the->scratch;
						fxForget(the, &job->self);
						fxSetHostData(the, mxResult, NULL);
						job->the = NULL;
						fxAbort(the, XS_UNHANDLED_EXCEPTION_EXIT);
					}
					fxEndHost(the);
					break; // to run promise jobs queued by the timer in the same "tick"
				}
				address = &(job->next);
			}
			else {
				*address = job->next;
				c_free(job);
			}
		}
	}
	fxCheckUnhandledRejections(the, 1);
}
