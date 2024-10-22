// File: thread-worker.c

#include "thread-worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

// Global counter for total context switches and
// average turn around and response time
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;

// Initialize all your other variables here
worker_t current_thread_id = 2; // Start from 2, as main thread is 1

tcb *stored_main_thread = NULL;
tcb *scheduler_thread = NULL;
tcb *current_tcb_executing = NULL;

int timer_initialized = 0;
double elapsed_time_since_refresh = 0;

static PriorityQueue *heap;
static MLFQ_t *mlfq;

/* Function Declarations */
void handle_interrupt(int signum);
void setup_timer_sjf();
static void sched_psjf();
static void sched_mlfq();
static void schedule();
void start_timer();
double get_time();
void stop_timer();
void create_context(ucontext_t *context);
void create_scheduler_thread();
void create_main_thread();
tcb *create_new_worker(worker_t *thread, void *(*function)(void *), void *arg);
void block_timer_signal(sigset_t *old_set);
void unblock_timer_signal(sigset_t *old_set);
static tcb *_find_thread(worker_t thread);
void thread_start();
void MLFQ_init();
int MLFQ_add(int priority_level, tcb *thread);
tcb *MLFQ_remove(int priority_level);
int get_time_quantum(int priority_level);
void demote_thread(tcb *thread);
void refresh_all_queues();

/* Priority Queue Functions */

void pq_swap(tcb **a, tcb **b)
{
    tcb *temp = *a;
    *a = *b;
    *b = temp;
}

void print_heap()
{
    printf("Heap contents:\n");
    for (size_t i = 0; i < heap->length; i++)
    {
        printf("Index %zu: TID %lu, elapsed_time %f\n", i, heap->threads[i]->thread_id, heap->threads[i]->elapsed_time);
    }
}

void heapify_up(int index)
{
    int parent = (index - 1) / 2;
    if (index > 0)
    {
        if (heap->threads[index]->elapsed_time < heap->threads[parent]->elapsed_time ||
            (heap->threads[index]->elapsed_time == heap->threads[parent]->elapsed_time &&
             heap->threads[index]->thread_id < heap->threads[parent]->thread_id))
        {
            pq_swap(&heap->threads[index], &heap->threads[parent]);
            heapify_up(parent);
        }
    }
}

void heapify_down(int index)
{
    int left_child = 2 * index + 1;
    int right_child = 2 * index + 2;
    int smallest = index;
    if (left_child < heap->length)
    {
        if (heap->threads[left_child]->elapsed_time < heap->threads[smallest]->elapsed_time ||
            (heap->threads[left_child]->elapsed_time == heap->threads[smallest]->elapsed_time &&
             heap->threads[left_child]->thread_id < heap->threads[smallest]->thread_id))
        {
            smallest = left_child;
        }
    }
    if (right_child < heap->length)
    {
        if (heap->threads[right_child]->elapsed_time < heap->threads[smallest]->elapsed_time ||
            (heap->threads[right_child]->elapsed_time == heap->threads[smallest]->elapsed_time &&
             heap->threads[right_child]->thread_id < heap->threads[smallest]->thread_id))
        {
            smallest = right_child;
        }
    }
    if (smallest != index)
    {
        pq_swap(&heap->threads[index], &heap->threads[smallest]);
        heapify_down(smallest);
    }
}

void pq_init()
{
    printf("Initializing priority queue\n");
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
    printf("Added thread %lu to priority queue\n", thread->thread_id);
    print_heap();
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
    printf("Removed thread %lu from priority queue\n", thread->thread_id);
    print_heap();
    return thread;
}

void pq_remove_thread(tcb *thread)
{
    if (heap == NULL || heap->length == 0)
        return;

    // Find the index of the thread
    size_t index = heap->length;
    for (size_t i = 0; i < heap->length; i++)
    {
        if (heap->threads[i]->thread_id == thread->thread_id)
        {
            index = i;
            break;
        }
    }

    if (index == heap->length)
    {
        // Thread not found
        return;
    }

    // Replace the thread with the last thread in the heap
    heap->threads[index] = heap->threads[heap->length - 1];
    heap->length--;

    // Heapify to maintain the heap property
    heapify_down(index);
    heapify_up(index);
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

/* Blocked Queue Functions */

Node *create_node(tcb *data, Node *prev)
{
    Node *new_node = malloc(sizeof(Node));
    new_node->data = data;
    new_node->next = NULL;
    new_node->prev = prev;
    return new_node;
}

BlockedQueue *blocked_queue_init()
{
    BlockedQueue *blocked_queue = malloc(sizeof(BlockedQueue));
    if (blocked_queue == NULL)
    {
        // handle error
        return NULL;
    }
    blocked_queue->front = blocked_queue->rear = NULL;
    blocked_queue->length = 0;
    return blocked_queue;
}

int blocked_queue_add(BlockedQueue *blocked_queue, tcb *thread)
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
    printf("Added thread %ld to blocked queue\n", thread->thread_id);
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
        ret = blocked_queue->front->data;
        Node *temp = blocked_queue->front;
        blocked_queue->front = blocked_queue->front->next;
        blocked_queue->front->prev = NULL;
        free(temp);
        blocked_queue->length--;
    }
    printf("Removed thread %ld from blocked queue\n", ret->thread_id);
    return ret;
}

// Unblocks threads waiting in the blocked queue
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
        Node *temp = ptr->next;
        ptr->data->stat = READY;
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

/* Create a new thread context */
void create_context(ucontext_t *context)
{
    if (getcontext(context) == -1)
    {
        perror("getcontext");
        exit(1);
    }

    context->uc_link = NULL;
    context->uc_stack.ss_sp = malloc(STACK_SIZE);
    context->uc_stack.ss_size = STACK_SIZE;
    context->uc_stack.ss_flags = 0;

    // check that the malloc worked
    if (context->uc_stack.ss_sp == NULL)
    {
        printf("allocation of %d bytes failed for context\n", STACK_SIZE);
        exit(1);
    }
}

/* Create the scheduler thread */
void create_scheduler_thread()
{
    scheduler_thread = malloc(sizeof(tcb));
    scheduler_thread->thread_id = 0;

    ucontext_t scheduler_cctx;
    create_context(&scheduler_cctx);

    // now we create our context to start at the scheduler
    makecontext(&scheduler_cctx, (void (*)(void))schedule, 0);
    scheduler_thread->context = scheduler_cctx;
    printf("Scheduler thread created\n");
}

/* Create the main thread */
void create_main_thread()
{
    tcb *main_thread = malloc(sizeof(tcb));
    main_thread->thread_id = 1;

    ucontext_t main_cctx;

    // get the main context and set it
    if (getcontext(&main_cctx) == -1)
    {
        perror("getcontext");
        exit(1);
    }
    if (stored_main_thread == NULL)
    {
        // now we create our context to start at the main
        main_thread->context = main_cctx;
        main_thread->queue = blocked_queue_init();
        main_thread->stat = RUNNING;
        main_thread->elapsed_time = 0;
        main_thread->priority = DEFAULT_PRIO;
        main_thread->current_queue_level = DEFAULT_PRIO;
        main_thread->time_remaining = 0;
        stored_main_thread = main_thread;
        current_tcb_executing = main_thread;
        printf("Main thread created with TID %ld\n", main_thread->thread_id);
    }
}

/* Thread start wrapper function */
void thread_start()
{
    // Call the actual thread function
    printf("Thread %ld starting\n", current_tcb_executing->thread_id);
    void *result = current_tcb_executing->start_routine(current_tcb_executing->arg);
    // When the thread function returns, call worker_exit
    worker_exit(result);
}

/* Create a new worker thread */
tcb *create_new_worker(worker_t *thread, void *(*function)(void *), void *arg)
{
    tcb *worker_tcb = malloc(sizeof(tcb));

    worker_tcb->thread_id = current_thread_id++;
    *thread = worker_tcb->thread_id; // Assign thread ID to the provided pointer
    worker_tcb->start_routine = function;
    worker_tcb->arg = arg;

    ucontext_t context;
    create_context(&context);

    makecontext(&context, (void (*)(void))thread_start, 0);
    worker_tcb->context = context;

    // Initialize other TCB fields
    worker_tcb->queue = blocked_queue_init();
    worker_tcb->stat = READY;
    worker_tcb->elapsed_time = 0;
    worker_tcb->priority = DEFAULT_PRIO;
    worker_tcb->current_queue_level = DEFAULT_PRIO;
    worker_tcb->time_remaining = 0;
    worker_tcb->value_ptr = NULL;

    printf("Created new thread with TID %ld\n", worker_tcb->thread_id);
    return worker_tcb;
}

/* Block timer signal */
void block_timer_signal(sigset_t *old_set)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPROF);
    sigprocmask(SIG_BLOCK, &set, old_set);
}

/* Unblock timer signal */
void unblock_timer_signal(sigset_t *old_set)
{
    sigprocmask(SIG_SETMASK, old_set, NULL);
}

/* Worker Create */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
    // Initialize scheduler and main thread on first call
    if (scheduler_thread == NULL)
    {
        printf("Initializing scheduler thread...\n");

        setup_timer_sjf();
        timer_initialized = 1;

        pq_init();
        create_scheduler_thread();
        create_main_thread();

        pq_add(stored_main_thread);
    }

    tcb *new_thread = create_new_worker(thread, function, arg);
    pq_add(new_thread);

    // Only swap context if not in the main thread
    if (current_tcb_executing != stored_main_thread)
    {
        // Swap to the scheduler
        swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
    }

    return 0;
}

/* Worker Exit */
void worker_exit(void *value_ptr)
{
    printf("Thread %ld exiting\n", current_tcb_executing->thread_id);
    if (value_ptr != NULL)
    {
        // store result of thread in the value pointer
        current_tcb_executing->value_ptr = value_ptr;
    }
    current_tcb_executing->stat = TERMINATED;

    // Wake up any threads waiting on this thread
    if (current_tcb_executing->queue != NULL)
    {
        unblock_threads(current_tcb_executing->queue);
    }

    // Do not free the stack here; cleanup will be handled in the scheduler

    // Switch to scheduler
    setcontext(&(scheduler_thread->context));
}

/* Worker Join */
int worker_join(worker_t thread, void **value_ptr)
{
    printf("worker_join: Main thread attempting to join TID %lu\n", thread);
    tcb *target_thread = _find_thread(thread);
    if (target_thread == NULL)
    {
        printf("worker_join: Target thread TID %lu not found\n", thread);
        if (value_ptr != NULL)
        {
            *value_ptr = NULL;
        }
        return 0;
    }

    printf("worker_join: Target thread TID %lu found with status %d\n", thread, target_thread->stat);

    if (target_thread->stat == TERMINATED)
    {
        if (value_ptr != NULL)
        {
            *value_ptr = target_thread->value_ptr;
        }
        return 0;
    }

    // Remove current thread from priority queue
    pq_remove_thread(current_tcb_executing);

    // Block current thread until target thread terminates
    printf("worker_join: Blocking main thread to wait for TID %lu\n", thread);
    blocked_queue_add(target_thread->queue, current_tcb_executing);
    current_tcb_executing->stat = BLOCKED;

    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));

    if (value_ptr != NULL)
    {
        *value_ptr = target_thread->value_ptr;
    }

    return 0;
}


/* Find Thread */
static tcb *_find_thread(worker_t thread)
{
    if (heap == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < heap->length; i++)
    {
        if (heap->threads[i]->thread_id == thread)
        {
            return heap->threads[i];
        }
    }
    // Check if the thread is currently executing
    if (current_tcb_executing != NULL && current_tcb_executing->thread_id == thread)
    {
        return current_tcb_executing;
    }
    return NULL;
}

/* Scheduler */
static void schedule()
{
    stop_timer();

    printf("Scheduler: Starting scheduling\n");
#ifndef MLFQ
    // Choose PSJF
    sched_psjf();
#else
    // Choose MLFQ
    sched_mlfq();
#endif
    printf("Scheduler: Finished scheduling\n");
}

/* Handle Interrupt */
void handle_interrupt(int signum)
{
    printf("Signal handler: Interrupting thread %ld\n", current_tcb_executing->thread_id);

    current_tcb_executing->elapsed_time += (TIME_QUANTA / 1000.0);
    printf("Thread %ld has run for %f seconds\n", current_tcb_executing->thread_id, current_tcb_executing->elapsed_time);
    current_tcb_executing->stat = READY;

#ifndef MLFQ
    pq_add(current_tcb_executing);
#else
    current_tcb_executing->time_remaining -= TIME_QUANTA;
    if (current_tcb_executing->time_remaining <= 0)
    {
        demote_thread(current_tcb_executing);
    }
    MLFQ_add(current_tcb_executing->priority, current_tcb_executing);
#endif

    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
}

/* Setup Timer */
void setup_timer_sjf()
{
    printf("Setting up timer\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handle_interrupt;
    sigaction(SIGPROF, &sa, NULL);

    struct itimerval timer;
    timer.it_interval.tv_usec = TIME_QUANTA * 1000;
    timer.it_interval.tv_sec = 0;

    timer.it_value.tv_usec = TIME_QUANTA * 1000;
    timer.it_value.tv_sec = 0;

    setitimer(ITIMER_PROF, &timer, NULL);
}

/* Start Timer */
void start_timer()
{
    printf("Starting timer\n");
    struct itimerval timer;

    timer.it_interval.tv_usec = TIME_QUANTA * 1000;
    timer.it_interval.tv_sec = 0;

    timer.it_value.tv_usec = TIME_QUANTA * 1000;
    timer.it_value.tv_sec = 0;

    setitimer(ITIMER_PROF, &timer, NULL);
}

/* Stop Timer */
void stop_timer()
{
    printf("Stopping timer\n");
    struct itimerval timer;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_PROF, &timer, NULL);
}

/* Get Time */
double get_time()
{
    struct itimerval time;
    // get the time
    getitimer(ITIMER_PROF, &time);
    return TIME_QUANTA * 1000 - time.it_value.tv_usec;
}

/* Sched PSJF */
static void sched_psjf()
{
    while (1)
    {
        printf("Scheduler: Currently have %lu items in the heap\n", heap->length);
        if (heap->length == 0)
        {
            printf("Scheduler: No threads to schedule, exiting\n");
            break;
        }

        tcb *thread = pq_remove();
        printf("Scheduler: Popped off thread with TID %lu\n", thread->thread_id);
        current_tcb_executing = thread;

        current_tcb_executing->stat = RUNNING;
        start_timer();
        setcontext(&(current_tcb_executing->context));

        // After returning from the thread
        if (current_tcb_executing->stat == TERMINATED)
        {
            // Cleanup
            free(current_tcb_executing->context.uc_stack.ss_sp);
            free(current_tcb_executing);
            current_tcb_executing = NULL;
        }
    }
}

/* The rest of the code remains the same, including MLFQ functions if needed */

void print_app_stats(void)
{
    fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
    fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
    fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}
