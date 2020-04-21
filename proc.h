#ifndef PBTL_H
#define PBTL_H

#include <semaphore.h>

#include "clock.h"
#include "oss.h"

enum process_state { READY=1, IOBLK, TERMINATE, EXITED};
enum request_state { AVAILABLE=0, NOT_AVAILABLE, PENDING, CANCELLED};

struct request{
	//type is 0 - no request, >0 take, <0 release
	int r,v,t;	//resource, value, type
	enum request_state state;
};

// entry in the process control table
struct process {
	int	pid;
	int id;
	int state;
	sem_t lock; /* for locking process data only */

	struct request request;

	/* what we have, and what we need */
	int allocated[NUM_DESC];
	int needed[NUM_DESC];
};

struct process * process_new(struct process * procs, const int id);
void 						 process_free(struct process * procs, const unsigned int i);

#endif
