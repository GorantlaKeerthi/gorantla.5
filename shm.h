
#include "proc.h"

#define USERS_COUNT 20

struct memory {
	struct clock clock;
	struct process procs[USERS_COUNT];
	sem_t lock;	/* for locking whole shared region */

	/* resource descriptors */
	int total[NUM_DESC];
	int available[NUM_DESC];
	int shared[NUM_DESC];
};

struct memory * shm_attach(const int flags);
						int shm_detach(const int clear);
