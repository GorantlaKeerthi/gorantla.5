#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>

#include "shm.h"

static int wait_request(struct process * pe){
	int rv = 0;
	while(rv == 0){

		usleep(10);

		if(sem_wait(&pe->lock) < 0){
			return -1;
		}

		if(pe->state == TERMINATE){
			sem_post(&pe->lock);
			return -1;
		}

		switch(pe->request.state){
			case AVAILABLE:
				if(pe->request.t > 0){
					pe->needed[pe->request.r] -= pe->request.v;
				}
				rv = pe->request.v;
				pe->request.v = 0;
				pe->request.t = 0;	//clear request
				break;
			case CANCELLED:	//we get here only when a deadlock has occured
				rv = -1;
				pe->request.t = 0;	//clear request
				break;
			default:
				break;
		}

		if(sem_post(&pe->lock) < 0){
			return -1;
		}
	}
	return rv;
}

static int make_request(struct process * pe, int r, int v, int t){
	if(sem_wait(&pe->lock) < 0){
		return -1;
	}
		pe->request.r 	= r;	//resource
		pe->request.v   = v;	//value
		pe->request.t		= t;	//type
		pe->request.state  = PENDING;

	if(sem_post(&pe->lock) < 0){
		return -1;
	}
	return 0;
}

static int find_request(const int * arr){
	int i, l=0, request[NUM_DESC];
	for(i=0; i < NUM_DESC; i++){
		if(arr[i] > 0){
			request[l++] = i;
		}
	}

	if(l == 0){
		return NUM_DESC;
	}
	int r = rand() % l;

	return request[r];
}

int main(const int argc, char * const argv[]){

	const int my_index = atoi(argv[1]);

	//fprintf(stderr, "INDEX %d\n", my_index);

	struct memory *mem = shm_attach(0);
	if(mem == NULL){
		fprintf(stderr, "Error: %d Can't attach \n", my_index);
		return -1;
	}

	struct process * pe = &mem->procs[my_index];

	srand(getpid());

	//generate the needed for resources
	//no need to lock, total[]/shared[] is read-only
	int i;
	for(i=0; i < NUM_DESC; i++){
		if(mem->shared[i] == 1){
			pe->needed[i] = 1 + (rand() % mem->total[i]);
		}else{
			pe->needed[i] = mem->total[i];
		}
	}

	if(sem_wait(&mem->lock) < 0){
		fprintf(stderr, "Error: A %d\n", my_index);
		return -1;
	}

	struct clock terminator = mem->clock;
	terminator.s += USER_RUNTIME;


	if(sem_post(&mem->lock) < 0){
		fprintf(stderr, "Error: B %d\n", my_index);
		return -1;
	}

	bzero(pe->allocated, sizeof(int)*NUM_DESC);

	int stop = 0, deallocate_flag = 0, nreq=0;
  while(!stop){
		//check if we should terminated
		if(sem_wait(&mem->lock) < 0){
			break;
		}
		//make sure we run at least USER_RUNTIME seconds
		const int terminate = (mem->clock.s <= terminator.s) ? READY : pe->state;

		if(sem_post(&mem->lock) < 0){
			break;
		}

		if(terminate == TERMINATE){
			deallocate_flag = 1;
		}

		int * arr, type;
		const int prob = (nreq > 0) ? 100 : TYPE_B;

		if(	(deallocate_flag == 0) &&
				(rand() % prob) < TYPE_B){	// in range [0;B] process is requesting resource
			arr = pe->needed;
			type = 1;

			nreq++;
		}else{
			arr = pe->allocated;
			type = -1;
			nreq--;
		}

		const int r = find_request(arr);
		if(r == NUM_DESC){
			if(type > 0){	//nothing more to take
				deallocate_flag = 1;	//go into deallocate mode
				continue;
			}else{	//nothing to request/release
				break;
			}
		}

		//make the request
		int val = (deallocate_flag) ? arr[r] : 1 + (rand() % arr[r]);
		make_request(pe, r, val, type);

		if(wait_request(pe) < 0){	//if request is denied, or we have to terminate
				break;
		}
  }

	sem_wait(&mem->lock);
	pe->state = EXITED;
	sem_post(&mem->lock);
	//fprintf(stderr, "Done %d\n", my_index);
	shm_detach(0);

  return 0;
}
