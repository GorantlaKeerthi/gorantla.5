#include <string.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "shm.h"

static struct memory * mem = NULL;
static int shmid = -1;

struct memory * shm_attach(const int flags){

  /* Create or get a shared memory */
	shmid = shmget(ftok("shm.c", 1), sizeof(struct memory), flags);
	if (shmid == -1) {
		perror("shmget");
		return NULL;
	}

	/* Attach to the shared region */
	mem = (struct memory*)shmat(shmid, (void *)0, 0);
	if (mem == (void *)-1) {
		perror("shmat");
		return NULL;
	}

	if(flags){ /* if we create the memory */
		memset(mem, 0, sizeof(struct memory));

		if(sem_init(&mem->lock, 1, 1) == -1){
			perror("sem_init");
			return NULL;
		}

		int i;
		for(i=0; i < USERS_COUNT; i++){
			if(sem_init(&mem->procs[i].lock, 1, 1) == -1){
	  		perror("sem_init");
	  		return NULL;
	  	}
		}
  }

	return mem;
}

int shm_detach(const int clear){

  if(mem == NULL){
    fprintf(stderr, "Error: shm is already cleared\n");
    return -1;
  }

  if(shmdt(mem) == -1){
    perror("shmdt");
  }

  if(clear){
	  if(shmctl(shmid, IPC_RMID,NULL) == -1){
      perror("shmctl");
    }

		int i;
		for(i=0; i < USERS_COUNT; i++){
			sem_destroy(&mem->procs[i].lock);
		}
  }

  mem = NULL;
  shmid = -1;

  return 0;
}
