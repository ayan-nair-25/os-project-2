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
#include <stdatomic.h>

typedef uint worker_t;

// Enum for tracking thread status
typedef enum
{
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
} thread_status;

typedef enum
{
    MUTEX_UNLOCKED,
    MUTEX_LOCKED,
} mutex_status_t;

/* LL queue for 'blocked' state */
typedef struct TCB tcb;
typedef struct Node
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

void pq_swap(tcb **a, tcb **b);
void heapify_up(int index);
void heapify_down(int index);
void pq_init();
void pq_expand();
void pq_shrink();
void pq_add(tcb *thread);
tcb *pq_remove();
tcb *pq_peek();
void pq_remove_thread(tcb *thread);
void free_pq();
void print_heap();

/* Thread Control Block (TCB) */
typedef struct TCB
{
    worker_t thread_id;             // Thread ID
    thread_status stat;             // Thread status
    ucontext_t * context;             // Thread context
    void *(*start_routine)(void *); // Function to execute
    void *arg;                      // Argument to function
    char *stack;                    // Thread stack
    int priority;                   // Thread priority
    int current_queue_level;        // Current queue level
    BlockedQueue *queue;            // Blocked queue
    void *value_ptr;                // Pointer to return value
    double elapsed_time;            // Time elapsed
    double time_remaining;          // Time remaining for MLFQ
    int in_queue;                   // Is thread in queue
    double start_time;		    // Time at which process arrives in thread
    double end_time;		    // time at which thread terminates (set in worker_exit)
} tcb;

/* Multi-Level Feedback Queue (MLFQ) */
typedef struct
{
    BlockedQueue *high_prio_queue;
    BlockedQueue *medium_prio_queue;
    BlockedQueue *default_prio_queue;
    BlockedQueue *low_prio_queue;
} MLFQ_t;

/* Mutex struct definition */
typedef struct worker_mutex_t
{

    mutex_status_t status;
    _Atomic(tcb *) owner_thread;
    BlockedQueue *queue;

} worker_mutex_t;

enum Mutex_Status
{
    LOCKED,
    UNLOCKED,
};

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

/* Function Declarations */

// Worker thread functions
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg);
void worker_exit(void *value_ptr);
int worker_join(worker_t thread, void **value_ptr);
int worker_yield();

// Mutex functions
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);
int worker_mutex_lock(worker_mutex_t *mutex);
int worker_mutex_unlock(worker_mutex_t *mutex);
int worker_mutex_destroy(worker_mutex_t *mutex);

// Scheduler functions
void handle_interrupt(int signum);
void start_timer();
void stop_timer();
double get_time();
static void schedule();
static void sched_psjf();
static void sched_mlfq();
void create_scheduler_thread();
void create_main_thread();
void thread_start();
tcb *create_new_worker(worker_t *thread, void *(*function)(void *), void *arg);
static tcb *_find_thread(worker_t thread);

// Priority Queue functions
void pq_swap(tcb **a, tcb **b);
void heapify_up(int index);
void heapify_down(int index);
void pq_init();
void pq_expand();
void pq_shrink();
void pq_add(tcb *thread);
tcb *pq_remove();
tcb *pq_peek();
void pq_remove_thread(tcb *thread);
void free_pq();
void print_heap();

// Blocked Queue functions
Node *create_node(tcb *data, Node *prev);
BlockedQueue *blocked_queue_init();
int blocked_queue_add(BlockedQueue *blocked_queue, tcb *thread);
tcb *blocked_queue_remove(BlockedQueue *blocked_queue);
int unblock_threads(BlockedQueue *blocked_queue);
void free_blocked_queue(BlockedQueue *blocked_queue);

// Utility functions
void block_timer_signal(sigset_t *old_set);
void unblock_timer_signal(sigset_t *old_set);
void create_context(ucontext_t *context);

// Print statistics
void print_app_stats(void);

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

#endif // WORKER_T_H
