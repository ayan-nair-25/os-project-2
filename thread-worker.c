// File:	thread-worker.c

// List all group member's name:
// username of iLab:
// iLab Server:

// scheduler pseudocode:

	// queue the current thread, dequeue a thread to get next and then swap from scheduler to the next thread
	// turn timer off while the scheduler is running, and then turn the timer back on after the scheduler is done (right before the swap)
	// assume that the thread will call pthread_exit, we don't need to use uclink. we manually use pthread_exit to swap back into the scheduler

// if we join a ready thread on a different thread:
	// we join into the current thread
	// in the thread we aim to join, we store some variable that keeps a pointer to the tcb of thread on which we must wait.
	// after we join, we want to jump back to the thread that we blocked

// ** the timer could be inaccurate for small time quanta
// ** turn off the timer during a mutex lock operation!!!


// question: if we call getcontext() when creating the main thread, wouldn't we just switch back to worker-create?
#include "thread-worker.h"

// Global counter for total context switches and
// average turn around and response time
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;

// INITAILIZE ALL YOUR OTHER VARIABLES HERE
// YOUR CODE HERE
//worker_t current_thread_id = 2;     // in the other case of using a main thread
worker_t current_thread_id = 1;

tcb * stored_main_thread = NULL;
tcb * scheduler_thread = NULL;
tcb * current_tcb_executing = NULL;

int timerInitialized = 0;

static PriorityQueue *heap;

/* min priority queue */

void pq_swap(tcb **a, tcb **b)
{
	tcb *temp = *a;
	*a = *b;
	*b = temp;
}

void heapify_up(int index)
{
	int parent = (index - 1) / 2;
	if (index > 0 &&
			heap->threads[index]->elapsed_time <= heap->threads[parent]->elapsed_time)
	{
		pq_swap(&heap->threads[index], &heap->threads[parent]);
		heapify_up(parent);
	}
}

void heapify_down(int index)
{
	int left_child = 2 * index + 1;
	int right_child = 2 * index + 2;
	int smallest = index;
	if (left_child < heap->length &&
			heap->threads[left_child]->elapsed_time < heap->threads[smallest]->elapsed_time)
	{
		smallest = left_child;
	}
	if (right_child < heap->length &&
			heap->threads[right_child]->elapsed_time < heap->threads[smallest]->elapsed_time)
	{
		smallest = right_child;
	}
	if (smallest != index)
	{
		pq_swap(&heap->threads[index], &heap->threads[smallest]);
		heapify_down(smallest);
	}
}

void pq_init() 
{
	heap = malloc(sizeof(PriorityQueue));
	heap->length = 0;
	heap->capacity = PQ_START_LEN;
	heap->threads = malloc(PQ_START_LEN * sizeof(tcb *));
}

void pq_expand()
{
	heap->capacity *= 2;
	heap->threads = realloc(heap->threads, heap->capacity * sizeof(tcb *));
}

void pq_shrink()
{
	heap->capacity /= 2;
	heap->threads = realloc(heap->threads, heap->capacity * sizeof(tcb *));
}

void pq_add(tcb *thread)
{
	if (thread == NULL)
	{
		return;
	}
	if (heap->length == heap->capacity)
	{
		pq_expand();
	}
	heap->threads[heap->length++] = thread;
	heapify_up(heap->length - 1);
}

tcb *pq_remove()
{
	if (heap->length == 0)
	{
		return NULL;
	}
	tcb *thread = heap->threads[0];
	heap->threads[0] = heap->threads[--heap->length];
	if (heap->length < heap->capacity / 2)
	{
		pq_shrink();
	}
	heapify_down(0);
	return thread;
}

tcb *pq_peek()
{
	if (heap->length == 0)
	{
		return NULL;
	}
	return heap->threads[0];
}

void free_pq()
{
	free(heap->threads);
	free(heap);
	heap = NULL;
}

/* blocked queue */

Node *create_node(tcb *data, Node *prev)
{
	Node *new_node = malloc(sizeof(Node));
	new_node->data = data;
	new_node->next = NULL;
	new_node->prev = prev;
	return new_node;
}

BlockedQueue* blocked_queue_init()
{
	BlockedQueue* blocked_queue = malloc(sizeof(BlockedQueue));
	if (blocked_queue == NULL)
	{
		// handle error
		return NULL;
	}
	blocked_queue->front = blocked_queue->rear = NULL;
	blocked_queue->length = 0;
	return blocked_queue;
}

int blocked_queue_add(BlockedQueue *blocked_queue,tcb *thread)
{
	if (thread == NULL || blocked_queue == NULL)
	{
		return -1;
	}
	// probably need to add a check if already in blocked queue
	else if (blocked_queue->length == 0)
	{
		blocked_queue->front = create_node(thread, NULL);
		blocked_queue->rear = blocked_queue->front;
	}
	else
	{
		Node *new_node = create_node(thread, blocked_queue->rear);
		blocked_queue->rear->next = new_node;
		blocked_queue->rear = new_node;
	}

	blocked_queue->length++;
	return 0;
}

tcb *blocked_queue_remove(BlockedQueue *blocked_queue)
{
	tcb *ret;
	if (blocked_queue == NULL || blocked_queue->length == 0)
	{
		ret = NULL;
	}
	else if (blocked_queue->length == 1)
	{
		ret = blocked_queue->front->data;
		blocked_queue->front = blocked_queue->rear = NULL;
		blocked_queue->length--;
	}
	else
	{
		ret = blocked_queue->rear->data;
		blocked_queue->rear = blocked_queue->rear->prev;
		blocked_queue->rear->next = NULL;
		blocked_queue->length--;
	}
	return ret;
}

// check if this works
int unblock_threads(BlockedQueue *blocked_queue)
{
	if (blocked_queue == NULL || heap == NULL)
	{
		// handle error
		return -1;
	}

	Node *ptr = blocked_queue->front;
	// add to runqueue and free blocked queue nodes
	while (ptr)
	{
		Node* temp = ptr->next;
		pq_add(ptr->data);
		free(ptr);
		ptr = temp;
	}

	free(blocked_queue);
	blocked_queue = NULL;
	return 0;
}

void free_blocked_queue(BlockedQueue *blocked_queue)
{
	if (blocked_queue == NULL || blocked_queue->length == 0)
	{
		return;
	}
	Node *ptr = blocked_queue->front;
	while (ptr != NULL)
	{
		Node *temp_node = ptr->next;
		free(ptr);
		ptr = temp_node;
	}
	free(blocked_queue);
	blocked_queue = NULL;
}
// ----------------------------- //

void handle_sjf_interrupt(int signum);

void setup_timer_sjf();

static void sched_psjf();
static void sched_mlfq();
static void schedule();

void start_timer();
void get_time_elapsed();
void stop_timer();
/* create a new thread */
void create_context(ucontext_t * context)
{
	getcontext(context);

	context->uc_link = NULL;	
	context->uc_stack.ss_sp = malloc(STACK_SIZE);
	context->uc_stack.ss_size = STACK_SIZE;
	context->uc_stack.ss_flags = NULL;

	// check that the malloc worked
	if(context->uc_stack.ss_sp == NULL)
	{
		printf("allocation of %d bytes failed for context\n", STACK_SIZE);
		exit(1);
	}
}

void create_scheduler_thread() 
{

	scheduler_thread = malloc(sizeof(scheduler_thread));
	scheduler_thread->thread_id = 0;

	ucontext_t scheduler_cctx;
	create_context(&scheduler_cctx);


	// we swap the context manually and don't use the link

	// now we create our context to start at the scheduler
	makecontext(&scheduler_cctx, (void *) &schedule, 0);
	scheduler_thread->context = scheduler_cctx;
}

void create_main_thread()
{
	tcb * main_thread = malloc(sizeof(tcb));
	main_thread->thread_id = 1;

	ucontext_t main_cctx;

	// get the main context and set it
	getcontext(&main_cctx);
	if (stored_main_thread == NULL)
	{
		// now we create our context to start at the main
		main_thread->context = main_cctx;
		main_thread->queue = blocked_queue_init();
		stored_main_thread = main_thread;
		current_tcb_executing = main_thread;
	}
}

tcb * create_new_worker(worker_t * thread, void*(*function)(void *), void *arg)
{
	tcb * worker_tcb = malloc(sizeof(tcb));

	worker_tcb->thread_id = current_thread_id++;
	
	ucontext_t context;
	create_context(&context);

	makecontext(&context, (void *) function, 0);
	worker_tcb->context = context;

	// after everything is set, push this thread into run queue and
	// initialize pq if not initialized alr
	worker_tcb->queue = blocked_queue_init();
	worker_tcb->stat = READY;
	worker_tcb->elapsed_time = 0;

	return worker_tcb;

}

int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
	// - create Thread Control Block (TCB)

	// - create and initialize the context of this worker thread
	// ucontext_t cctx, scheduler_cctx, main_cctx;

	// only initialize the scheduler context on the first call
	// also init the main context
	if (scheduler_thread == NULL) 
	{
		//printf("initializing scheduler thread...\n");


		// setup_timer_sjf();

		setup_timer_sjf();
		timerInitialized = 1;

		pq_init();
		create_scheduler_thread();
		create_main_thread();

		pq_add(stored_main_thread);
	}
	tcb * new_thread = create_new_worker(thread, function, arg);
	// otherwise we add a new thread
	pq_add(new_thread);
	swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
	// YOUR CODE HERE
	return 0;
}

#ifdef MLFQ
/* This function gets called only for MLFQ scheduling set the worker priority. */
int worker_setschedprio(worker_t thread, int prio)
{

	// Set the priority value to your thread's TCB
	// YOUR CODE HERE

	return 0;
}
#endif

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{

	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	// YOUR CODE HERE

	// block the timer
	// send a signal to modify tcb
	// do other work
	current_tcb_executing->stat = READY;
#ifndef MLFQ
	// figure out how much time in the timer and add
#endif
	swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	if (value_ptr)	
	{
		// store result of thread in the value pointer	
		current_tcb_executing->value_ptr = value_ptr;
	}
	current_tcb_executing->stat = TERMINATED;
	free(current_tcb_executing->context.uc_stack.ss_sp);

	setcontext(&(scheduler_thread->context));
};

static tcb _find_thread(worker_t thread)
{
	// implement function here
	return;
};

/* Wait for thread termination */

/*
General algo:
sem is located in each TCB
on thread creation init sem
find thread in worker join and wait on TCB sem if needed
wait for sem to be signaled somehow, prob in yield/exit, after join?
have to then destroy sem when thread is taken off queue somehow
*/
int worker_join(worker_t thread, void **value_ptr)
{
	// // - wait for a specific thread to terminate
	// // - de-allocate any dynamic memory created by the joining thread
	// tcb *ref_thread = get_tcb(thread);
	// if (ref_thread == NULL)
	// {
	// 	// thread is not found
	// 	return -1;
	// }
	// if (ref_thread->status == TERMINATED)
	// {
	// 	// thread already terminated
	// 	if (value_ptr != NULL)
	// 	{
	// 		;
	// 		// set return value of thread
	// 	}
	// 	return 0;
	// }

	// // else move thread to blocked queue

	// return 0;
	/*
	each TCB will have a list of threads waiting to join on it

	GENERAL OUTLINE:
	check if ref thread is null
	if it is:
		early return + possible error set

	check if ref thread is terminated
	if it is:
		check if value ptr is null
			if it is not:
				value ptr = ref_thread->exit_value
	else:
		set calling thread to BLOCKED and move calling thread to blocked queue
		add calling thread to list of threads wanting to join TCB
		yield control (scheduler will switch context)


	What does the scheduler do next?
	Scheduler will select the next READY thread to run

	When does unblocking happen?
	During worker_exit() of the ref thread:
		change state to terminated
		iterate over waiting list of threads and set their status to ready
		move them from the blocked queue to the ready queue
		yield control


	IMPORTANT: MULTIPLE BLOCKED QUEUES BASED ON EVENT CAUSING BLOCKING

	Each resource has its own blocked queue. 
	This blocked queue lists threads waiting for that resource to 
	either become available or to terminate, etc.


	When the resource DOES terminate/become available, 
	it iterates through its separate blocked queue and 
	adds it to a global run queue.
	*/
};

/* initialize the mutex lock */
// can assume mutexattr is NULL
int worker_mutex_init(worker_mutex_t *mutex,
											const pthread_mutexattr_t *mutexattr)
{
	//- initialize data structures for this mutex

	mutex = (worker_mutex_t *)malloc(sizeof(worker_mutex_t));
	if (mutex == NULL){
		// handle error
		return -1;
	}
	mutex->status = UNLOCKED;
	mutex->owner_thread = NULL;
	mutex->queue = blocked_queue_init();
	if (mutex->queue == NULL)
	{
		// handle error
		return -1;
	}
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{

	// - use the built-in test-and-set atomic function to test the mutex
	// - if the mutex is acquired successfully, enter the critical section
	// - if acquiring mutex fails, push current thread into block list and
	// context switch to the scheduler thread

	// thread is unowned and calling thread can enter crit sect
	// pretend this is current thread
	tcb *curr_thread = NULL;
	if (mutex->status == UNLOCKED)
	{
		mutex->status = LOCKED;
		mutex->owner_thread = curr_thread;
		return 0;
	}
	
	// add current thread to mutex waiting list
	if (blocked_queue_add(mutex->queue, curr_thread) == -1)
	{
		// handle error
		return -1;
	}
	worker_yield();
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
	// - release mutex and make it available again.
	// - put threads in block list to run queue
	// so that they could compete for mutex later.

	tcb *current_thread = NULL;
	mutex->owner_thread = NULL;
	mutex->status = UNLOCKED;
	unblock_threads(mutex->queue);
	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
	// - de-allocate dynamic memory created in worker_mutex_init
	free_blocked_queue(mutex->queue);
	mutex->owner_thread = NULL;
	free(mutex);
	mutex = NULL;
	return 0;
};


/* scheduler */
static void schedule()
{
	// - every time a timer interrupt occurs, your worker thread library
	// should be contexted switched from a thread context to this
	// schedule() function

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ)

	// if (sched == PSJF)
	//		sched_psjf();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

	// YOUR CODE HERE

// - schedule policy
	//printf("once again back in da scheduler \n");
	stop_timer();
#ifndef MLFQ
	// Choose PSJF
	sched_psjf();
#else
	// Choose MLFQ
	sched_mlfq();
#endif
	//printf("done scheduling\n");
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */


// what we have to do here:
// we have the general algorithm but now the problem becomes how to signal timer interrupts
// 1. create the timer
// 2. upon timer interrupt:
//	- switch back to the scheduler context
// 3. if we finish the thread before the timer interrupt then swap back to the scheduler context
//	- we then repeat the process


void handle_sjf_interrupt(int signum)
{
	// modify this to get the exact amount of time using the struct itimer
	//printf("in signal handler :D\n");
	
	printf("interrupting...\n");
	current_tcb_executing->elapsed_time += TIME_QUANTA;
	printf("current tcb executing has ran for: %f seconds \n\n", current_tcb_executing->elapsed_time);

	current_tcb_executing->stat = READY;
	pq_add(current_tcb_executing);

	//printf("currently have %d\n items in the queue\n", heap->length);
	//setcontext(&(scheduler_thread->context));
	//swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
	swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
	//swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
	// change the above to setcontext?
	//swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
}

void get_time_elapsed()
{
	struct itimerval time;
	// get the time
	getitimer(ITIMER_PROF, &time);

	

}
void setup_timer_sjf()
{
	printf("initialized timer! \n");
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &handle_sjf_interrupt;
	sigaction(SIGPROF, &sa, NULL);

	struct itimerval timer;
	timer.it_interval.tv_usec = TIME_QUANTA * 100;
	timer.it_interval.tv_sec = 0;

	timer.it_value.tv_usec = TIME_QUANTA * 100;
	timer.it_value.tv_sec = 0;

	setitimer(ITIMER_PROF, &timer, NULL);
}

void start_timer()
{
	printf("started timer\n");
	struct itimerval timer;

	timer.it_interval.tv_usec = TIME_QUANTA * 100;
	timer.it_interval.tv_sec = 0;

	timer.it_value.tv_usec = TIME_QUANTA * 100;
	timer.it_value.tv_sec = 0;

	setitimer(ITIMER_PROF, &timer, NULL);
}

void stop_timer()
{
	printf("stopped timer\n");
	struct itimerval timer;

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 0;

	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;

	setitimer(ITIMER_PROF, &timer, NULL);
}

static void sched_psjf()
{

	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	// we are given a run queue with all of the tcb times on there
		
	// first pop from the heap	
	tcb * thread = pq_remove();
	//printf("removed from heap\n");
	current_tcb_executing = thread;
	//printf("stored thread with id %d\n", current_tcb_executing->thread_id);

	// do the context switching here so that we can move our context to the wanted function that we want to execute
	// we want to swap between the scheduler context and the function context
	//printf("obtained context for current thread\n");
	// change this to setcontext?
	current_tcb_executing->stat = RUNNING;
	start_timer();
	setcontext(&(current_tcb_executing->context));
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
{
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

// DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void)
{

	fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
	fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
	fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}
