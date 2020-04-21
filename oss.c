#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>

#include "shm.h"
#include "proc.h"

// clock step
#define CLOCK_NS 1000

// total processes to start
#define USERS_GENERATED 100

static int started = 0; //started users
static int exited = 0;  //exited users

static struct memory *mem = NULL;   /* shared memory region pointer */

/* counters for request type - release and request */
static unsigned int released=0, requested=0;
/* counters for request status - available, not available, cancelled */
static unsigned int available=0, not_available=0, cancelled=0;
/* counters for deadlock algorithm - deadlocks, killed procs */
static unsigned int deadlocks=0, kills=0;
/* counter for output lines */
static unsigned int line_count=0;

static int interrupted = 0;
static int verbose = 0;

//Increment array A with B
static void add_array(int *A, const int *B){
	int i;
	for(i=0; i < NUM_DESC; i++){
		A[i] += B[i];
  }
}

//Convert integer number to string
static char * num_arg(const unsigned int number){
	size_t len = snprintf(NULL, 0, "%d", number) + 1;
	char * str = (char*) malloc(len);
	snprintf(str, len, "%d", number);
	return str;
}

static int exec_user(void){

	struct process *pe;

  if((pe = process_new(mem->procs, started)) == NULL){
    return 0; //no free processes
  }
  started++;
  const int pi = pe - mem->procs; //process index

  char * my_id = num_arg(pi);

  const pid_t pid = fork();
  if(pid == -1){
    perror("fork");
    return -1;
  }else if(pid == 0){
    execl("./user", "./user", my_id, NULL);
    perror("execl");
    exit(-1);
  }else{
    pe->pid = pid;
  }
  free(my_id);

  printf("[%i:%i] OSS: Generating process with PID %u\n", mem->clock.s, mem->clock.ns, pe->id);

  return 0;
}

static int forktime(struct clock *forktimer){

  struct clock inc;

  //advance time
  inc.s = 1;
  inc.ns = rand() % CLOCK_NS;

  sem_wait(&mem->lock);
  add_clocks(&mem->clock, &inc);
  sem_post(&mem->lock);

  if(started < USERS_GENERATED){  //if we can fork more

    // if its time to fork
    if(cmp_clocks(&mem->clock, forktimer)){

      //next fork time
      forktimer->s = mem->clock.s + 1;
      forktimer->ns = 0;

      return 1;
    }
  }
  return 0; //not time to fokk
}

//Send mesage to users to quit
static void final_msg(){
  int i;

  for(i=0; i < USERS_COUNT; i++){
    if(mem->procs[i].pid > 0){

      sem_wait(&mem->procs[i].lock);

      mem->procs[i].state = TERMINATE;
      mem->procs[i].request.state = CANCELLED;

      sem_post(&mem->procs[i].lock);
    }
  }
}

static void signal_handler(const int sig){
  printf("[%i:%i] OSS: Signaled with %d\n", mem->clock.s, mem->clock.ns, sig);
  interrupted = 1;
}


static void print_results(){
  printf("Runtime: %u:%u\n", mem->clock.s, mem->clock.ns);
  printf("Started: %i\n", started);
  printf("Exited: %i\n", exited);
  printf("Requested: %u\n", requested);
  printf("Released: %u\n", released);
  printf("Granted: %u\n", available);
  printf("Blocked: %u\n", not_available);
  printf("Denied: %u\n", cancelled);
  printf("Deadlocks: %u\n", deadlocks);
  printf("Kills: %u\n", kills);
}

static int current_requests(){

  printf("Current resource requests at time %i:%i:\n", mem->clock.s, mem->clock.ns);

  int i, count=0;
  for(i=0; i < USERS_COUNT; i++){
    struct process * pe = &mem->procs[i];
    if((pe->pid > 0) && (pe->request.v != 0)){
      char o = (pe->request.t > 0) ? '+' : '-';
      printf("P%d: %cR%d=%d\n", pe->id, o, pe->request.r, pe->request.v);
			count++;
    }
  }

	line_count += count;
	return count;
}

static void current_allocations(){

  printf("Current resource allocations at time %i:%i\n", mem->clock.s, mem->clock.ns);


  printf("    ");
	int i, count=0;
	for(i=0; i < NUM_DESC; i++){
		printf("R%02d ", i);
  }
	printf("\n");

	//show what we have available in system
  printf("SYS ");
  for(i=0; i < NUM_DESC; i++){
    printf("%*d ", 3, mem->available[i]);
  }
  printf("\n");


	//show what the users have
	for(i=0; i < USERS_COUNT; i++){
    struct process * pe = &mem->procs[i];
		if(pe->pid > 0){
  		printf("P%02d ", pe->id);

			int j;
  		for(j=0; j < NUM_DESC; j++)
  			printf("%*d ", 3, pe->allocated[j]);

  		printf("\n");
			count++;
    }
	}

	line_count += 3 + count;
}

static int dead_or_safe(const int done[USERS_COUNT]){

  int i, first_dead = -1;
	for(i=0; i < USERS_COUNT; i++){
		if(!done[i]){
      if(first_dead == -1){
        first_dead = i;     //save first dead id
      }
    }
  }

  if(verbose && (first_dead > 0)){
    printf("Processes ");
    for(i=0; i < USERS_COUNT; i++){
  		if(!done[i]){	//if not done
        printf("P%d ", mem->procs[i].id);
      }
    }
    printf("deadlocked.\nAttempting to resolve deadlock...\n");
  }

	return first_dead;
}

/* Determine system state - deadlock or safe */
static int bankers_algorithm(void){

	int i, work[NUM_DESC], done[USERS_COUNT];

	for(i=0; i < USERS_COUNT; i++)
		done[i] = 0;

	for(i=0; i < USERS_COUNT; i++)
		work[i] = mem->available[i];

  //block requests
  for(i=0; i < USERS_COUNT; i++){
    sem_wait(&mem->procs[i].lock);
  }

  i=0;
	while(i != USERS_COUNT){

		for(i=0; i < USERS_COUNT; i++){
			struct process * pe = &mem->procs[i];

			if(  (pe->pid == 0) ||
					 (pe->state == TERMINATE) ||
				 	 (pe->state == EXITED)){
				done[i] = 1;	/* user is done if process is terminated/exited */
				continue;
			}

      if(	(done[i] == 0) &&
				  ((pe->request.v <= work[pe->request.r]) || (pe->request.t < 0 ))  ){

				add_array(work, pe->allocated);
				done[i] = 1;	/* unfinished users with satisfyable/releasing requests are done */

				/* we have changed work[], recheck */
				break;
			}
		}
	}

  /* unblock requests */
  for(i=0; i < USERS_COUNT; i++){
    sem_post(&mem->procs[i].lock);
  }

	return dead_or_safe(done);
}

static void kill_deadlocked(struct process * pe){
	int i;

	kills++;

	if(verbose){
    printf("Killing P%i deadlocked for +R%d:%d\n", pe->id, pe->request.r, pe->request.v);
    printf("Resources released are as follows:");

    for(i=0; i < NUM_DESC; i++){
      if(pe->allocated[i] > 0){
  		    printf("R%i:%d ", i, pe->allocated[i]);
      }
    }
  	printf("\n");
		line_count += NUM_DESC + 3;

    current_requests();
    current_allocations();
  }

	sem_wait(&pe->lock);
  pe->state = TERMINATE;
  pe->request.state = CANCELLED;

  cancelled++;
  add_array(mem->available, pe->allocated); //add allocated to available
	for(i=0; i < NUM_DESC; i++){
		mem->available[i] += pe->allocated[i];
		pe->allocated[i] = 0;
	}
	sem_post(&pe->lock);
}

static int detect_deadlocked(){
  if(verbose){
    line_count++;
    printf("Master running deadlock detection at time %i:%i\n", mem->clock.s, mem->clock.ns);
  }

  int first_dead = -1, nkills=0;
  while((first_dead = bankers_algorithm()) >= 0){
    kill_deadlocked(&mem->procs[first_dead]);
		nkills++;
  }

  if(nkills > 0){	/* if we had a deadlock */
		deadlocks++;
		if(verbose){
    	line_count++;
    	printf("System is no longer in deadlock\n");
		}
	}
  return nkills;
}

static void request_available(struct process * pe, const enum request_state state){

  if(state == NOT_AVAILABLE){	//if we are in the block "queue"
    printf("Master unblocking P%d and granting it R%d=%d at time %i:%i\n",
      pe->id, pe->request.r, pe->request.v,
      mem->clock.s, mem->clock.ns);

  }else if(state == PENDING){

    if(pe->request.t > 0){
      printf("Master granting P%d request R%d=%d at time %i:%i\n",
        pe->id, pe->request.r, pe->request.v,
        mem->clock.s, mem->clock.ns);

    }else if(pe->request.t < 0){
      printf("Master has acknowledged Process P%d releasing R%d=%d at time %i:%i\n",
        pe->id, pe->request.r, pe->request.v,
        mem->clock.s, mem->clock.ns);
    }
  }
  //pe->request.v = 0; //clear the request
  available++;
}
static void request_not_available(struct process * pe){
  if(pe->request.t > 0){	//process request is blocked
    printf("Master blocking P%d for requesting R%d=%d at time %i:%i\n",
      pe->id, pe->request.r, pe->request.v,
      mem->clock.s, mem->clock.ns);
  }
  not_available++;
}

static void request_cancelled(struct process * pe){
  printf("Master denied P%d invalid request R%d=%d at time %i:%i\n",
        pe->id, pe->request.r, pe->request.v,
        mem->clock.s, mem->clock.ns);
  pe->request.v = 0; //clear the request
  cancelled++;
}

static int new_request(struct process * pe, enum request_state state){
	int rv = 0;

	struct request * request = &pe->request;
	if(request->t < 0){
			released++;
			pe->allocated[request->r] -= request->v;
			mem->available[request->r]  += request->v;
			pe->request.state = AVAILABLE;
			request_available(pe, state);
			rv = 1;

	}else if(request->t > 0){
			requested++;
			if(mem->available[request->r] >= request->v) {  //if we have that amount of resource
				pe->allocated[request->r] += request->v;
				mem->available[request->r] -= request->v;
				pe->request.state = AVAILABLE;	//no deadlock, grant
				request_available(pe, state);
			}else if(pe->request.state != NOT_AVAILABLE){	//if request was not blocked already
				pe->request.state = NOT_AVAILABLE;
				request_not_available(pe);
			}


	}else{
			fprintf(stderr, "Error: Invalid type in process request\n");
			pe->request.state = CANCELLED;
			request_cancelled(pe);
	}
	return rv;
}

static int dispatching(enum request_state state){
	int i, count=0;
  struct clock tdisp;

  tdisp.s = 0;

	for(i=0; i < USERS_COUNT; i++){

		struct process * pe = &mem->procs[i];

		sem_wait(&mem->procs[i].lock);

		if(pe->state == EXITED){
			exited++;
			process_free(mem->procs, i);

		}else if((pe->pid > 0) &&
					(pe->request.state == state)){

			if((state != NOT_AVAILABLE) && (pe->request.t > 0)){
				if(verbose){
					line_count++;
							printf("Master has detected P%d requesting R%d=%d at time %i:%i\n",
						pe->id, pe->request.r, pe->request.v, mem->clock.s, mem->clock.ns);
				}
			}

			if(new_request(pe, state) == 1){
				count++;
			}

    	if((verbose == 1) && ((requested % 20) == 0))
    		current_allocations();

		}
    sem_post(&mem->procs[i].lock);

    //add request processing to clock
    tdisp.ns = rand() % 100;
    add_clocks(&mem->clock, &tdisp);
	}

	return count;	//return number of dispatched procs
}

static int alloc_resources(){
  /* 20% +- 5 are shared */
  int nshared = 20 + ((rand() % 10) - 5);

  nshared = NUM_DESC / (100 / nshared);
  printf("%d shared resources:", nshared);

  while(nshared > 0){
    const int ri = rand() % NUM_DESC;
    if(mem->shared[ri] == 0){
      printf("R%d ", ri);

      mem->shared[ri] = 1;
      --nshared;
    }
  }
  printf("\n");

	int i;
	for(i=0; i < NUM_DESC; i++){
		mem->total[i]     =
		mem->available[i] = 1 + (rand() % MAX_DESC);
  }

	current_allocations();

	return 0;
}

int main(const int argc, char * argv[]){

  signal(SIGINT,  signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGALRM, signal_handler);
  signal(SIGCHLD, SIG_IGN);

  mem = shm_attach(0600 | IPC_CREAT);
  if(mem == NULL){
    return -1;
  }

	if(argc == 2){
		verbose = 1;
	}

  stdout = freopen("output.txt", "w", stdout);
  if(stdout == NULL){
		perror("freopen");
		return -1;
	}

  alloc_resources();

  srand(getpid());

  alarm(3);

  struct clock forktimer = {0,0};
	int null_grants = 0;

  while((exited < USERS_GENERATED) &&
        (!interrupted)){

    if(forktime(&forktimer) && (exec_user() < 0)){
      fprintf(stderr, "exec_user failed\n");
      break;
    }

		/* process blocked and pending requests */
    const int grants = dispatching(NOT_AVAILABLE) + dispatching(PENDING);

    if(grants == 0){  //if different is one second
			null_grants++;

			/* if we did dispatch any process in last 2 iterations, run deadlock check */
			if(null_grants == 2){
      	detect_deadlocked();
				null_grants = 0;
				/* give some time to proces to take their locks, since we lock everything in deadlock detection */
				usleep(10);
			}
    }

  	if(line_count >= MAX_OUTPUT_LINES){
  		printf("OSS: Log full. Stopping ...\n");
  		stdout = freopen("/dev/null", "w", stdout);
  	}
  }

  print_results();

  final_msg();
  shm_detach(1);

  return 0;
}
