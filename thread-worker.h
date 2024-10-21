// File:	worker_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1
#define STACK_SIZE SIGSTKSZ

/* include lib header files that you need here: */
#include <unistd.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint worker_t;
// enum for tracking thread status
typedef enum
{
	READY,
	RUNNING,
	BLOCKED,
	TERMINATED
} thread_status;

/* LL queue for 'blocked' state */

// this might cause an error or a duplicate definition issue but idk for sure
typedef struct TCB tcb;
typedef struct
{
	tcb *data;
	struct Node *next, *prev;
} Node;

Node *create_node(tcb *data, Node *prev);

typedef struct
{
	Node *front, *rear;
	uint length;
} BlockedQueue;

BlockedQueue *blocked_queue_init();

int blocked_queue_add(BlockedQueue *blocked_queue, tcb *thread);

tcb *blocked_queue_remove(BlockedQueue *blocked_queue);

int unblock_threads(BlockedQueue *blocked_queue);

void free_blocked_queue(BlockedQueue *blocked_queue);

/* Min-PQ for SJF */

#define PQ_START_LEN 100
#define TIME_QUANTA 100
#define REFRESH_QUANTA 10000

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
	// thread pritority
	int priority;
	// current queue level (wow)
	int current_queue_level;
	// add the blocked queue
	BlockedQueue *queue;
	// value pointer
	void *value_ptr;
	// And more ...
	double elapsed_time;
	// time left before needs to be demoted
	double time_remaining;
} tcb;

typedef struct
{
	BlockedQueue *high_prio_queue;
	BlockedQueue *medium_prio_queue;
	BlockedQueue *default_prio_queue;
	BlockedQueue *low_prio_queue;
} MLFQ_t;
/* mutex struct definition */
typedef struct worker_mutex_t
{
	thread_status status;
	tcb *owner_thread;
	BlockedQueue *queue;
} worker_mutex_t;

/* Priority definitions */
#define NUMPRIO 4

#define HIGH_PRIO 3
#define MEDIUM_PRIO 2
#define DEFAULT_PRIO 1
#define LOW_PRIO 0
#define HIGH_PRIO_QUANTA 50
#define MEDIUM_PRIO_QUANTA 100
#define DEFAULT_PRIO_QUANTA 200
#define LOW_PRIO_QUANTA 400
/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE

/*
enum Status
{
	READY,
	RUNNING,
	BLOCKED,
	TERMINATED,
};
*/

enum Mutex_Status
{
	LOCKED,
	UNLOCKED,
};

/* Function Declarations: */

/* find tcb in runqueue */
// will need to handle finding a TCB in a multi-level queue
static tcb *get_tcb(worker_t thread);

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

static void sched_psjf();

static void sched_mlfq();

static void schedule();

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
