// File:	worker_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

/* include lib header files that you need here: */
#include <unistd.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/time.h>

#define STACK_SIZE SIGSTKSZ
typedef uint worker_t;
// enum for tracking thread status
typedef enum
{
	READY,
	RUNNING,
	WAITING,
	DELAYED,
	BLOCKED,
	TERMINATED
} thread_status;

#define TIME_QUANTUM_USEC 10000 // 10ms

#define HIGH_PRIO_QUANTUM_USEC (10 * 1000)		// 10ms
#define MEDIUM_PRIO_QUANTUM_USEC (20 * 1000)	// 20ms
#define DEFAULT_PRIO_QUANTUM_USEC (30 * 1000) // 30ms
#define LOW_PRIO_QUANTUM_USEC (40 * 1000)			// 40ms

#define REFRESH_QUANTUM_USEC (100 * TIME_QUANTUM_USEC) // Adjust as per project requirements

/* LL queue for 'blocked' state */

// this might cause an error or a duplicate definition issue but idk for sure
typedef struct TCB tcb;
typedef struct
{
	tcb *data;
	Node *next, *prev;
} Node;

Node *create_node(tcb *data, Node *prev);

typedef struct
{
	Node *front, *rear;
	uint length;
} Queue;

void init_scheduler();

void timer_handler(int signum);

int get_time_quantum(int priority_level);

void demote_thread(tcb *thread);

void refresh_all_queues();

Queue *queue_init();

int queue_add(Queue *queue, tcb *thread);

tcb *queue_remove(Queue *queue);

int unblock_threads(Queue *queue);

void free_queue(Queue *queue);

/* Min-PQ for SJF */

#define PQ_START_LEN 10

typedef struct
{
	tcb **threads;
	int length;
	int capacity;
} PriorityQueue;

void swap(tcb **a, tcb **b);

void heapify_up(int index);

void heapify_down(int index);

void pq_init();

void pq_expand();

void pq_shrink();

void pq_add(tcb *thread);

tcb *pq_remove();

tcb *pq_peek();

void free_pq();

typedef struct TCB
{
	/* add important states in a thread control block */
	// thread Id
	worker_t thread_id;
	// thread status
	thread_status stat;
	// thread context
	ucontext_t context;
	// thread stack
	char *stack;
	// thread priority
	int priority;
	// current queue level (for MLFQ)
	int current_queue_level;
	// time remaining in current time quantum
	int time_remaining;
	// queue of threads waiting on this thread (for worker_join)
	Queue *waiting_threads;
	// parent thread (for worker_join)
	tcb *parent_thread;
	// And more ...
	uint elapsed_time;
	void *exit_value;
} tcb;

typedef enum
{
	LOCKED,
	UNLOCKED,
} mutex_status;
/* mutex struct definition */
typedef struct worker_mutex_t
{
	mutex_status status;
	tcb *owner_thread;
	Queue *queue;
} worker_mutex_t;

/* Priority definitions */
#define NUMPRIO 4

#define HIGH_PRIO 3
#define MEDIUM_PRIO 2
#define DEFAULT_PRIO 1
#define LOW_PRIO 0

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE

/* Function Declarations: */
/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
																								 *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

// MLFQ IMPL

/*
MLFQ Rules (8.6 OSTEP)

• Rule 1: If Priority(A) > Priority(B), A runs (B doesn’t).

• Rule 2: If Priority(A) = Priority(B), A & B run in round-robin fashion using
	the time slice (quantum length) of the given queue.

• Rule 3: When a job enters the system, it is placed at the highest priority
	(the topmost queue).

• Rule 4: Once a job uses up its time allotment at a given level (regardless
	of how many times it has given up the CPU), its priority is reduced
	(i.e., it moves down one queue).

• Rule 5: After some time period S, move all the jobs in the system to the
	topmost queue.


*/

#define TIME_QUANTUM 10
#define REFRESH_QUANTUM 100

typedef struct
{
	Queue *high_prio_queue;
	Queue *medium_prio_queue;
	Queue *default_prio_queue;
	Queue *low_prio_queue;
} MLFQ;

void MLFQ_init();

int MLFQ_add(int priority_level, tcb *thread);

tcb *MLFQ_remove(int priority_level);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#define pthread_setschedprio worker_setschedprio
#endif

#endif
