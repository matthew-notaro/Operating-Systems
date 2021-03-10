/* 
 * Group Members Names and NetIDs:
 *   1. Matthew Notaro (myn7)
 *   2. Farrah Rahman (fr258)
 *
 * ILab Machine Tested on:
 *   kill.cs.rutgers.edu
 */
#include "rpthread.h"


// INITIALIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
/* create a new thread */
int create(rpthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg);
void* dequeue(queue *inQueue);
void enqueue(queue *inQueue, void* inElement);
void functionCaller();
void* printTest(void*);//remove with function later
void sigHandler(int);
void schedule();
int init();
void* printTest2(void* input);
void* printTest3(void* input);
static void sched_rr();
static void sched_mlfq();


//GLOBALS
//***************************************************
ucontext_t *schedCon = NULL, *mainCon = NULL;
queue RRqueue = {NULL, NULL, NULL, 0};
queue MLqueue = {NULL, NULL, NULL, 0};
queue blockList = {NULL, NULL}; //not in use
llist threadMap[97] = {NULL}; //not in use, but should be because who knows where terminated threads are going at present
tcb* TCBcurrent;
struct itimerval timer, zeroTimer;
struct sigaction sa;
int tempIds[20] = {0}; //remove later!
int scheduler; //0 if RR, 1 if MLFQ
//***************************************************

int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg) {
	//printf("create\n");
	static int needInit = 0;
	static int maxThreadId = 0; //thread 0 is main
	if(!needInit) {
		needInit = 1;
		init();
	}

	//set up tcb to enqueue
	tcb* TCBtemp = malloc(sizeof(tcb));
	//set input function and args in tcb 
	getcontext(&(TCBtemp->context));
	TCBtemp->context.uc_stack.ss_sp = malloc(STACKSIZE);
	TCBtemp->context.uc_stack.ss_size = STACKSIZE;
	TCBtemp->context.uc_link = schedCon;
	TCBtemp->function=function;
	TCBtemp->input = arg;
	TCBtemp->state = READY;
	TCBtemp->joins = 0;
	makecontext(&TCBtemp->context, functionCaller, 0);
	
	//printf("attempt to disarm timer\n");
	setitimer(ITIMER_REAL, &zeroTimer, NULL); //this line succeeds in disarming
	
	TCBtemp->threadId = ++maxThreadId;
	*thread = TCBtemp->threadId;
	tempIds[TCBtemp->threadId] = 1;
	enqueue(&RRqueue, TCBtemp);
	
	//printf("attempt to arm timer\n");
	setitimer(ITIMER_REAL, &timer, NULL);
	
	//printf("end create\n");
	return 0;
}

/* give CPU possession to other user-level threads voluntarily */
int rpthread_yield() {
	/// change thread state from Running to Ready
	// save context of this thread to its thread control block
	// switch from thread context to scheduler context

	// Enqueue in same level
	enqueue(&RRqueue, TCBcurrent);

	// else {
	// 	//a dequeued thread would get lost here sadly
	// 	//printf("thread terminated: %d\n", TCBcurrent->threadId);
	// 	tempIds[TCBcurrent->threadId] = 0;  
	// 	//^ REMOVE LATER!******************************************************
	// }

	swapcontext(&TCBcurrent->context, schedCon);

	return 0;
};


/* terminate a thread */
void rpthread_exit(void *value_ptr) {
	// Deallocated any dynamic memory created when starting this thread
	// This rpthread_exit function is an explicit call to the rpthread_exit library to end the pthread that called it.
	// If the value_ptr isn’t NULL, any return value from the thread will be saved. Think about what things you
	// should clean up or change in the thread state and scheduler state when a thread is exiting.

	// Set thread's retVal is value_ptr != NULL to be accessed by join
	if(value_ptr != NULL){
		value_ptr = TCBcurrent->retVal;
	}

	// Frees current thread's TCB struct only if number of threads joining running thread is 0
	// Does not free if a thread is joining it - frees later when joins <= 0 (not sure how it would get <0)
	if(TCBcurrent->joins <= 0){
		free(TCBcurrent->context.uc_stack.ss_sp);
		free(TCBcurrent->context);
		free(TCBcurrent);
		TCBcurrent = NULL;
	}
	// Only set state when threads joining on current thread - no need to when thread is getting freed
	else{
		TCBcurrent->state = TERMINATED;
	}
	// Don't have to disconnect from DLL since already dequeued - not sure what else to do here
}; 


/* Wait for thread termination */
// Returns int value of 0 on success, -1 if thread not found
int rpthread_join(rpthread_t thread, void **value_ptr) {
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
	while(tempIds[thread]!=0); //testing only! tempIds won't exist later
	// de-allocate any dynamic memory created by the joining thread - not sure how to do this

	// Invalid thread id
	if(thread < 1){
		return -1;
	}

	// Linearly search thru linked list of all created threads
	// Can also just search thru all threads in scheduler - don't have to wait for a thread that's not running
	// Assume 2d linked list scheduler since join has to work for both RR and MLFQ.

	// Which queue to use - probably unnecessary if we decide to only use 1 queue for both RR And ML
	queue level = (scheduler) ? MLqueue : RRqueue;
	node* temp = NULL;
	tcb* foundTCB = NULL;
	int found = 1;

	// For each level in the queue, go thru each thread on that level
	while(level != NULL && found){
		// For each thread on current level
		temp = level.head;
		while(temp != NULL){
			// Found thread with desired tid
			if((temp->element != NULL) && ((tcb*)(temp->element)->threadId == thread)){
				foundTCB = temp->tcblock;

				// Incr number of joins called on desired thread
				foundTCB->joins++;

				// Use found to break out of outer while loop
				found = 0;
				break;
			}
			temp = temp->next;
		}
		level = level.next;
	}

	// Fail to find a thread with the desired tid.
	if(foundTCB == NULL){
		if(value_ptr != NULL)
			**value_ptr = 0;
		return -1;
	}

	// If tid found, loop until desired thread is terminated, at which point we are free to return
	// think this works as a simplified version but not as readable
	// while(foundTCB != NULL && foundTCB->state != TERMINATED)
	while(1){
		if(foundTCB == NULL || foundTCB->state == TERMINATED){
			break;
		}
		// yielding might be needed here since not very efficient to waste time slice
		// pthread_yield();
	}

	// Decr number of threads joining desired thread after joined thread finishes/is freed (shouldn't be freed)
	foundTCB->joins--;

	// Free thread stuff if no more threads are joining it
	if(foundTCB->joins == 0){
		free(foundTCB->context.uc_stack.ss_sp);
		free(foundTCB->context);
		free(foundTCB);
		foundTCB = NULL;
	}

	// Pass on exit value
	if(value_ptr != NULL){
		*value_ptr = foundTCB->retVal;
	}
	return 0;
};

/* initialize the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, 
                         const pthread_mutexattr_t *mutexattr) {
	/*  //initialize data structures for this mutex
	*(mutex->lock) = 0;
	mutex->list = malloc(sizeof(LinkedList));
	mutex->list.head = NULL;
	// YOUR CODE HERE*/
	return 0;
};

/* aquire the mutex lock */
int rpthread_mutex_lock(rpthread_mutex_t *mutex) {
	// use the built-in test-and-set atomic function to test the mutex
	// if the mutex is acquired successfully, enter the critical section
	// if acquisigHandler mutex fails, push current thread into block list and //  
	// context switch to the scheduler thread
	
	/*  // YOUR CODE HERE
	if(__sync_lock_test_and_set(mutex->lock, 1)==1) { //lock not acquired
		TCBcurrent.state = BLOCKED;
		//add(mutex->list, TCBcurrent);
		swapcontext(TCBcurrent.context, &schedCon);
	}
	*/
	
	return 0;
};


/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex) {
	// Release mutex and make it available again. 
	// Put threads in block list to run queue 
	// so that they could compete for mutex later.

	__sync_lock_test_and_set(mutex->lock, 0); //release lock
	
	return 0;
};


/* destroy the mutex */
// Deallocate dynamic memory created in rpthread_mutex_init
int rpthread_mutex_destroy(rpthread_mutex_t *mutex) {
	return 0;
};


/* scheduler */
static void schedule() {
	// Every time when timer interrupt happens, your thread library 
	// should be contexted switched from thread context to this 
	// schedule function

	// Invoke different actual scheduling algorithms
	// according to policy (RR or MLFQ)

	// if (sched == RR)
	//		sched_rr();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

	// YOUR CODE HERE
	//printf("in schedule\n");

	// schedule policy
	#ifndef MLFQ
		//printf("round robin in schedule\n");
		sched_rr();
		//TCBcurrent = dequeue(RRqueue
	#else 
		// Choose MLFQ
		// CODE 2
	#endif
		//printf("in schedule\n");

}

/* Round Robin (RR) scheduling algorithm */
static void sched_rr() {
	tcb* temp = (tcb*)dequeue(&RRqueue);
	if(temp == NULL) {
		return; //see if this breaks anything?
	}
	//printf("wasn't null\n");
	TCBcurrent = temp;
	timer.it_value.tv_usec = 1000;
	//printf("val is %ld\n", timer.it_value.tv_usec);
	setitimer(ITIMER_REAL, &timer, NULL);
//	printf("swapping to %d \n", TCBcurrent->threadId);
	setcontext(&TCBcurrent->context);
	
	//printf("made it here\n");
}

/* Preemptive MLFQ scheduling algorithm */
// 4 level
// stuff that changes going from RR to ML:
// 		upon using the whole time slice without yielding, enqueue on level priority-1 - happens in signal handler

// stuff that doesn't have to:
// 		dequeueing from highest priority level - keep checking each level's queue until find non-empty queue
// 		searching for a specific thread id - same as above
static void sched_mlfq() {

	return;
}

// Feel free to add any other functions you need

void enqueue(queue* inQueue, void* inElement) {
	//is queue empty?
	if(inQueue->head != NULL) {
		//printf("enqueue with old head\n");
		node* temp = malloc(sizeof(node));
		temp->element = inElement;
		temp->next = NULL;
		temp->prev = inQueue->tail;
		inQueue->tail->next = temp;
		inQueue->tail = temp;
	}
	//at least one element exists in queue
	else {
		//printf("enqueue with new head\n");
		inQueue->head = malloc(sizeof(node));
		inQueue->tail = inQueue->head;
		inQueue->head->element = inElement;
		inQueue->head->next = NULL;
		inQueue->head->prev = NULL;
		//remove below later
		/*if(inQueue->head == RRqueue.head)
			printf("successfully modified rr queue\n");
		else {
			printf("inqueue head is %p but rr head is %p\n", inQueue->head, RRqueue.head);
		}*/
	}
	
	
	/* node* temp = inQueue->head;
	printf("queue: ");
	while(temp != NULL) {
		printf("%d ",(int)(((tcb*)(temp->element))->threadId));
		temp = temp->next;
	}
	printf("\n");*/
	
	//printf("successfully enqueued\n");
}

void* dequeue(queue *inQueue) {
	//printf("in dequeue\n");
	if(inQueue->head != NULL) {
		void* temp = inQueue->head->element;
		inQueue->head = inQueue->head->next;
		if(inQueue->head != NULL)
			inQueue->head->prev = NULL;
		return temp;
	}
	//printf("dequeue head is null\n");
	return NULL;
}

void functionCaller() {
	//call thread input function
	//printf("caller: %d\n", TCBcurrent->threadId);
	TCBcurrent->retVal = TCBcurrent->function(TCBcurrent->input);
	TCBcurrent->state = TERMINATED;
}

void* printTest(void* input) {
	
	printf("printing thread %d\n", TCBcurrent->threadId);
	int *x = malloc(sizeof(int));
	while(*x<100000) (*x)++;
	printf("printing thread %d again\n", TCBcurrent->threadId);
	rpthread_t thread;
	rpthread_create(&thread, NULL, printTest2, x);
	rpthread_join(thread, NULL);
	return NULL;
}

void* printTest2(void* input) {
	printf("in printTest2: %d called by %d\n", *(int*)input, TCBcurrent->threadId);
	int test = 17;
	rpthread_t thread;
	rpthread_create(&thread, NULL, printTest3, &test);
	rpthread_join(thread, NULL);
	return NULL;
}

void* printTest3(void* input) {
	printf("in printTest3: %d called by %d\n",  *(int*)input, TCBcurrent->threadId);
	return NULL;
}

void sigHandler(int signum) {
	// Enqueue in same level
	if(TCBcurrent->state != TERMINATED) {
	//	printf("about to enqueue\n");
		enqueue(&RRqueue, TCBcurrent);
	}
	// else {
	// 	//a dequeued thread would get lost here sadly
	// 	//printf("thread terminated: %d\n", TCBcurrent->threadId);
	// 	tempIds[TCBcurrent->threadId] = 0;  
	// 	//^ REMOVE LATER!******************************************************
	// }

	//printf("caught signal\n");
	swapcontext(&TCBcurrent->context, schedCon);
}




int init() {
	//printf("init\n");
	//register signal handler
	TCBcurrent = malloc(sizeof(tcb));
	TCBcurrent->threadId = 0;
	memset (&sa, 0, sizeof(sigaction));
	sa.sa_handler = &sigHandler;
	sigaction (SIGALRM, &sa, NULL);
	
	//initialize timers
	timer.it_value.tv_usec = 1000; 
	timer.it_value.tv_sec = 0;
	timer.it_interval.tv_usec = 0; 
	timer.it_interval.tv_sec = 0;
	
	zeroTimer.it_value.tv_usec = 0; 
	zeroTimer.it_value.tv_sec = 0;
	zeroTimer.it_interval.tv_usec = 0; 
	zeroTimer.it_interval.tv_sec = 0;
	
	//create main context
	mainCon = malloc(sizeof(ucontext_t));
	getcontext(mainCon);
	TCBcurrent->context = *mainCon;

	//create scheduling context
	schedCon = malloc(sizeof(ucontext_t));
	getcontext(schedCon);
	schedCon->uc_stack.ss_sp = malloc(STACKSIZE);
	schedCon->uc_stack.ss_size = STACKSIZE;
	//schedCon->uc_link = mainCon;
	makecontext(schedCon, schedule, 0);
	setitimer(ITIMER_REAL, &timer, NULL);
	
	return 0;
}

int main(int argc, char** argv) {
	//0 if RR, 1 if MLFQ
	// assuming: gcc -pthread -g -c rpthread.c -DTIMESLICE=$(TSLICE)		for RR
	//			 gcc -pthread -g -c rpthread.c -DMLFQ -DTIMESLICE=$(TSLICE)	for MLFQ
	scheduler = (argc == 6) ? 0 : 1;

	rpthread_t thread1 = 0,	thread2 =0, thread3 =0, thread4=0, thread5=0;
	int a = 3;
	int b = 5;
	int c = 10;
	int d = 15;
	int e = 20;
	int x = 0;
	rpthread_create(&thread1, NULL, printTest, &a);
	rpthread_create(&thread2, NULL, printTest2, &b);
	rpthread_create(&thread3, NULL, printTest2, &c);
	rpthread_create(&thread4, NULL, printTest2, &d);
	rpthread_create(&thread5, NULL, printTest2, &e);
	//printf("%d %d %d %d %d\n", thread1, thread2, thread3, thread4, thread5);
	while(x<1000000) x++; //kill some time
	rpthread_join(thread1, NULL);
	rpthread_join(thread2, NULL);
	rpthread_join(thread3, NULL);
	rpthread_join(thread4, NULL);
	rpthread_join(thread5, NULL);
	printf("MAIN RETURNED\n");

	return 0;
}

