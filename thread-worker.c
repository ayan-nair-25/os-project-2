// File: thread-worker.c
// hi
#include "thread-worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdatomic.h>

/* Global counters for total context switches and average turnaround and response time */
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;

/* Initialize all other variables here */
worker_t current_thread_id = 2; // Start from 2, as main thread is 1

tcb *stored_main_thread = NULL;
tcb *scheduler_thread = NULL;
tcb *current_tcb_executing = NULL;

/* Global thread list to keep track of all threads */
typedef struct ThreadNode
{
    tcb *thread_tcb;
    struct ThreadNode *next;
} ThreadNode;

ThreadNode *thread_list_head = NULL;

int timer_initialized = 0;
double elapsed_time_since_refresh = 0;

#ifndef MLFQ
static PriorityQueue *heap;
#else
static MLFQ_t *mlfq;
#endif

/* Priority Queue Functions */
#ifndef MLFQ

void pq_swap(tcb **a, tcb **b)
{
    tcb *temp = *a;
    *a = *b;
    *b = temp;
}

void print_heap()
{
    // //printf("Heap contents:\n");
    for (size_t i = 0; i < heap->length; i++)
    {
        // //printf("Index %zu: TID %u, elapsed_time %f\n", i, heap->threads[i]->thread_id, heap->threads[i]->elapsed_time);
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
    // //printf("Initializing priority queue\n");
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
    if (heap->capacity > PQ_START_LEN)
    {
        heap->capacity /= 2;
        heap->threads = realloc(heap->threads, heap->capacity * sizeof(tcb *));
    }
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
    // //printf("Added thread %u to priority queue\n", thread->thread_id);
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
    heapify_down(0);
    // //printf("Removed thread %u from priority queue\n", thread->thread_id);
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

#endif // MLFQ

#ifdef MLFQ
/* This function gets called only for MLFQ scheduling to set the worker priority. */
int worker_setschedprio(worker_t thread, int prio)
{
    tcb *target_tcb = _find_thread(thread);
    if (target_tcb == NULL)
    {
        return -1;
    }
    target_tcb->priority = prio;
    target_tcb->current_queue_level = prio;
    // Time remaining?
    return 0;
}
#endif

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
        // Handle error
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
        free(blocked_queue->front);
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
    if (ret == NULL)
    {
        // ////printf("Blocked queue is empty\n");
    }
    else
    {
        ret->in_queue = 0;
        // //printf("Removed thread %d from blocked queue\n", ret->thread_id);
    }
    return ret;
}

int unblock_threads(BlockedQueue *blocked_queue)
{
    if (blocked_queue == NULL)
    {
        // Handle error
        return -1;
    }

    Node *ptr = blocked_queue->front;
    // Add to run queue and free blocked queue nodes
    while (ptr)
    {
        Node *temp = ptr->next;
        ptr->data->stat = READY;

#ifndef MLFQ
        pq_add(ptr->data);
#else
        MLFQ_add(ptr->data->current_queue_level, ptr->data);
#endif

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

    context->uc_stack.ss_sp = malloc(STACK_SIZE);
    context->uc_stack.ss_size = STACK_SIZE;
    context->uc_stack.ss_flags = 0;

    // Check that the malloc worked
    if (context->uc_stack.ss_sp == NULL)
    {
        // //printf("Allocation of %ld bytes failed for context\n", STACK_SIZE);
        exit(1);
    }

    // Set uc_link to NULL
    context->uc_link = NULL;
}

/* Create the scheduler thread */
void create_scheduler_thread()
{
    scheduler_thread = malloc(sizeof(tcb));
    scheduler_thread->thread_id = 0;

    create_context(&scheduler_thread->context);

    // Create our context to start at the scheduler
    makecontext(&scheduler_thread->context, (void (*)(void))schedule, 0);

    // //printf("Scheduler thread created\n");
}

/* Create the main thread */
void create_main_thread()
{
    tcb *main_thread = malloc(sizeof(tcb));
    main_thread->thread_id = 1;

    // Get the main context and set it
    if (getcontext(&main_thread->context) == -1)
    {
        perror("getcontext");
        exit(1);
    }

    if (stored_main_thread == NULL)
    {
        main_thread->queue = blocked_queue_init();
        main_thread->stat = RUNNING;
        main_thread->elapsed_time = 0;
        main_thread->priority = DEFAULT_PRIO;
        main_thread->current_queue_level = DEFAULT_PRIO;
        main_thread->time_remaining = 0;
        main_thread->value_ptr = NULL;
        main_thread->context.uc_link = NULL;
        main_thread->in_queue = 0;
        stored_main_thread = main_thread;
        current_tcb_executing = main_thread;

        // Add main thread to the global thread list
        ThreadNode *main_node = malloc(sizeof(ThreadNode));
        main_node->thread_tcb = main_thread;
        main_node->next = thread_list_head;
        thread_list_head = main_node;

        // //printf("Main thread created with TID %d\n", main_thread->thread_id);
    }
}

/* Thread start wrapper function */
void thread_start()
{
    // Call the actual thread function
    // //printf("Thread %d starting\n", current_tcb_executing->thread_id);
    void *result = current_tcb_executing->start_routine(current_tcb_executing->arg);
    // When the thread function returns, call worker_exit
    worker_exit(result);

    // Prevent returning
    setcontext(&scheduler_thread->context);
}

/* Create a new worker thread */
tcb *create_new_worker(worker_t *thread, void *(*function)(void *), void *arg)
{
    tcb *worker_tcb = malloc(sizeof(tcb));

    worker_tcb->thread_id = current_thread_id++;
    *thread = worker_tcb->thread_id; // Assign thread ID to the provided pointer
    worker_tcb->start_routine = function;
    worker_tcb->arg = arg;

    create_context(&worker_tcb->context);

    makecontext(&worker_tcb->context, (void (*)(void))thread_start, 0);

    // Initialize other TCB fields
    worker_tcb->queue = blocked_queue_init();
    worker_tcb->stat = READY;
    worker_tcb->elapsed_time = 0;
    worker_tcb->priority = DEFAULT_PRIO;
    worker_tcb->current_queue_level = DEFAULT_PRIO;
    worker_tcb->time_remaining = 0;
    worker_tcb->value_ptr = NULL;
    worker_tcb->in_queue = 0;

    // Add new thread to the global thread list
    ThreadNode *new_node = malloc(sizeof(ThreadNode));
    new_node->thread_tcb = worker_tcb;
    new_node->next = thread_list_head;
    thread_list_head = new_node;

    // //printf("Created new thread with TID %d\n", worker_tcb->thread_id);
    return worker_tcb;
}

/* Worker Create */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
    // Initialize scheduler and main thread on first call
    if (scheduler_thread == NULL)
    {
        // //printf("Initializing scheduler thread...\n");

        create_scheduler_thread(); // Initialize scheduler thread first

        setup_timer_sjf();
        timer_initialized = 1;

#ifndef MLFQ
        pq_init();
#else
        MLFQ_init();
#endif

        create_main_thread();

#ifndef MLFQ
        pq_add(stored_main_thread);
#else
        MLFQ_add(stored_main_thread->current_queue_level, stored_main_thread);
#endif
    }

    tcb *new_thread = create_new_worker(thread, function, arg);

#ifndef MLFQ
    pq_add(new_thread);
#else
    MLFQ_add(new_thread->current_queue_level, new_thread);
#endif

    // Yield control to allow the new thread to run
    worker_yield();

    return 0;
}

/* Worker Yield */
int worker_yield()
{
    // Change worker thread's state from RUNNING to READY
    // Save context of this thread to its thread control block
    // Switch from thread context to scheduler context

    // //printf("Thread %d yielding\n", current_tcb_executing->thread_id);
    current_tcb_executing->stat = READY;

#ifndef MLFQ
    // Figure out how much time in the timer and add
    current_tcb_executing->elapsed_time += (get_time() / 1000.0);
    stop_timer();
    // Add back to the queue
    pq_add(current_tcb_executing);
#else
    current_tcb_executing->time_remaining -= get_time();
    if (current_tcb_executing->time_remaining <= 0)
    {
        demote_thread(current_tcb_executing);
    }
    MLFQ_add(current_tcb_executing->priority, current_tcb_executing);
#endif

    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
    return 0;
}

/* Worker Exit */
void worker_exit(void *value_ptr)
{
    if (value_ptr != NULL)
    {
        current_tcb_executing->value_ptr = value_ptr;
    }
    current_tcb_executing->stat = TERMINATED;
    // //printf("worker_exit: Thread %d exiting\n", current_tcb_executing->thread_id);

    if (current_tcb_executing->queue != NULL && current_tcb_executing->queue->length > 0)
    {
        // //printf("worker_exit: Unblocking threads waiting on thread %d\n", current_tcb_executing->thread_id);
        unblock_threads(current_tcb_executing->queue);
        current_tcb_executing->queue = NULL;

        // Do not free resources; joining thread will handle it
    }
    else
    {
        // //printf("worker_exit: No threads waiting on thread %d. Cleaning up resources.\n", current_tcb_executing->thread_id);

        // Free resources since no thread will join
        free(current_tcb_executing->context.uc_stack.ss_sp);
        remove_thread_from_list(current_tcb_executing->thread_id);
        free(current_tcb_executing);
    }

    // Switch to scheduler context
    setcontext(&scheduler_thread->context);
}

/* Worker Join */
int worker_join(worker_t thread, void **value_ptr)
{
    // //printf("worker_join: Thread %d attempting to join TID %u\n", current_tcb_executing->thread_id, thread);
    tcb *target_thread = _find_thread(thread);
    if (target_thread == NULL)
    {
        // //printf("worker_join: Target thread TID %u not found\n", thread);
        if (value_ptr != NULL)
        {
            *value_ptr = NULL;
        }
        return -1;
    }

    // //printf("worker_join: Target thread TID %u found with status %d\n", thread, target_thread->stat);

    if (target_thread->stat == TERMINATED)
    {
        if (value_ptr != NULL)
        {
            *value_ptr = target_thread->value_ptr;
        }
        // Remove the thread from the global thread list and free its resources
        remove_thread_from_list(thread);
        free(target_thread->context.uc_stack.ss_sp);
        free(target_thread);
        return 0;
    }

    // Remove current thread from scheduler
#ifndef MLFQ
    pq_remove_thread(current_tcb_executing);
#else
    MLFQ_remove_thread(current_tcb_executing);
#endif

    // Block current thread until target thread terminates
    // //printf("worker_join: Blocking thread %d to wait for TID %u\n", current_tcb_executing->thread_id, thread);
    blocked_queue_add(target_thread->queue, current_tcb_executing);
    current_tcb_executing->stat = BLOCKED;

    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));

    // After being unblocked, check if the target thread has terminated
    if (value_ptr != NULL)
    {
        *value_ptr = target_thread->value_ptr;
    }

    // Remove the thread from the global thread list and free its resources
    remove_thread_from_list(thread);
    free(target_thread->context.uc_stack.ss_sp);
    free(target_thread);

    return 0;
}

/* Find Thread */
static tcb *_find_thread(worker_t thread)
{
    ThreadNode *current = thread_list_head;
    while (current != NULL)
    {
        if (current->thread_tcb->thread_id == thread)
        {
            // printf("Found thread %u\n", current->thread_tcb->thread_id);
            return current->thread_tcb;
        }
        current = current->next;
    }
    return NULL;
}

/* Remove Thread from Global List */
void remove_thread_from_list(worker_t thread_id)
{
    ThreadNode **current = &thread_list_head;
    while (*current != NULL)
    {
        if ((*current)->thread_tcb->thread_id == thread_id)
        {
            ThreadNode *temp = *current;
            *current = (*current)->next;
            free(temp);
            return;
        }
        current = &((*current)->next);
    }
}

/* Worker Mutex Functions */

int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    // //printf("Initializing mutex\n");
    if (mutex == NULL)
    {
        // f////printf(stderr, "Mutex init failed: mutex is NULL\n");
        return -1;
    }

    mutex->status = MUTEX_UNLOCKED;
    atomic_store(&(mutex->owner_thread), NULL);
    mutex->queue = blocked_queue_init();
    if (mutex->queue == NULL)
    {
        perror("Mutex init failed: Queue init failure");
        exit(1);
    }
    // //printf("Mutex initialized successfully\n");
    return 0;
}

/* Acquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
    sigset_t old_set;
    block_timer_signal(&old_set);

    tcb *expected = NULL;
    while (!atomic_compare_exchange_weak(&(mutex->owner_thread), &expected, current_tcb_executing))
    {
        // Failed to acquire mutex, add to waiting queue
        blocked_queue_add(mutex->queue, current_tcb_executing);
        current_tcb_executing->stat = BLOCKED;

        // Remove current thread from priority queue
#ifndef MLFQ
        pq_remove_thread(current_tcb_executing);
#else
        MLFQ_remove_thread(current_tcb_executing);
#endif

        unblock_timer_signal(&old_set);

        // Swap to scheduler
        swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));

        block_timer_signal(&old_set);

        expected = NULL;
    }

    mutex->status = MUTEX_LOCKED;

    unblock_timer_signal(&old_set);

    return 0;
}

/* Release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
    if (atomic_load(&(mutex->owner_thread)) != current_tcb_executing)
    {
        // f////printf(stderr, "Mutex unlock failed: thread %d does not own the mutex\n", current_tcb_executing->thread_id);
        return -1;
    }

    sigset_t old_set;
    block_timer_signal(&old_set);

    atomic_store(&(mutex->owner_thread), NULL);
    mutex->status = MUTEX_UNLOCKED;

    tcb *next_thread = blocked_queue_remove(mutex->queue);
    if (next_thread != NULL)
    {
        next_thread->stat = READY;
#ifndef MLFQ
        pq_add(next_thread);
#else
        MLFQ_add(next_thread->current_queue_level, next_thread);
#endif
    }

    unblock_timer_signal(&old_set);

    return 0;
}

/* Destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
    // //printf("Destroying mutex\n");

    if (mutex->queue->length > 0)
    {
        // f////printf(stderr, "Mutex destroy failed: threads are still waiting on the mutex\n");
        return -1;
    }

    free_blocked_queue(mutex->queue);
    mutex->queue = NULL;
    atomic_store(&(mutex->owner_thread), NULL);
    mutex->status = MUTEX_UNLOCKED;

    // //printf("Mutex destroyed successfully\n");
    return 0;
}

/* Block timer signal */
void block_timer_signal(sigset_t *old_set)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, old_set);
}

void unblock_timer_signal(sigset_t *old_set)
{
    sigprocmask(SIG_SETMASK, old_set, NULL);
}

/* Scheduler */
static void schedule()
{
    // //printf("Scheduler: Starting scheduling\n");

#ifndef MLFQ
    // Choose PSJF
    sched_psjf();
#else
    // Choose MLFQ
    sched_mlfq();
#endif

    // //printf("Scheduler: Finished scheduling\n");

    // Stop the timer before returning to the main thread
    stop_timer();

    // If main thread is not terminated, return control to it
    if (stored_main_thread != NULL && stored_main_thread->stat != TERMINATED)
    {
        // //printf("Scheduler: Returning control to main thread\n");
        current_tcb_executing = stored_main_thread; // Set current executing thread to main thread
        setcontext(&(stored_main_thread->context));
    }
    else
    {
        // //printf("Scheduler: Exiting\n");
        exit(0);
    }
}

/* Handle Interrupt */
void handle_interrupt(int signum)
{
    // printf("handle_interrupt: currently handling interrupt\n");
    if (current_tcb_executing == NULL)
    {
        // //printf("Signal handler: No current thread executing\n");
        return;
    }

    // //printf("Signal handler: Interrupting thread %d\n", current_tcb_executing->thread_id);

    current_tcb_executing->elapsed_time += (TIME_QUANTA / 1000.0);
    // //printf("Thread %d has run for %f seconds\n", current_tcb_executing->thread_id, current_tcb_executing->elapsed_time);
    current_tcb_executing->stat = READY;

#ifndef MLFQ
    pq_add(current_tcb_executing);
#else
    current_tcb_executing->time_remaining -= TIME_QUANTA;
    if (current_tcb_executing->time_remaining <= 0)
    {
        demote_thread(current_tcb_executing);
    }
    MLFQ_add(current_tcb_executing->current_queue_level, current_tcb_executing);
#endif

    // Swap context to scheduler
    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
}

/* Setup Timer */
void setup_timer_sjf()
{
    // //printf("Setting up timer\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handle_interrupt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval timer;
    timer.it_interval.tv_usec = TIME_QUANTA * 1000;
    timer.it_interval.tv_sec = 0;

    timer.it_value.tv_usec = TIME_QUANTA * 1000;
    timer.it_value.tv_sec = 0;

    setitimer(ITIMER_REAL, &timer, NULL);
}

/* Start Timer */
void start_timer()
{
    struct itimerval timer;

    timer.it_interval.tv_usec = TIME_QUANTA * 1000;
    timer.it_interval.tv_sec = 0;

    timer.it_value.tv_usec = TIME_QUANTA * 1000;
    timer.it_value.tv_sec = 0;

    setitimer(ITIMER_REAL, &timer, NULL);
}

/* Stop Timer */
void stop_timer()
{
    struct itimerval timer;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &timer, NULL);
}

/* Get Time */
double get_time()
{
    struct itimerval time;
    // Get the time
    getitimer(ITIMER_REAL, &time);
    return TIME_QUANTA * 1000 - time.it_value.tv_usec;
}

/* Sched PSJF */
#ifndef MLFQ
static void sched_psjf()
{
    while (1)
    {
        // //printf("Scheduler: Currently have %u items in the heap\n", heap->length);
        if (heap->length == 0)
        {
            // //printf("Scheduler: No threads to schedule, exiting\n");
            break;
        }

        tcb *thread = pq_remove();
        // //printf("Scheduler: Popped off thread with TID %u\n", thread->thread_id);
        current_tcb_executing = thread;

        current_tcb_executing->stat = RUNNING;
        start_timer();

        // Switch to thread context
        swapcontext(&(scheduler_thread->context), &(current_tcb_executing->context));
    }
}
#endif

/* Preemptive MLFQ scheduling algorithm */
#ifdef MLFQ
static void sched_mlfq()
{
    while (1)
    {
        sigset_t old_set;
        block_timer_signal(&old_set);

        if (elapsed_time_since_refresh >= REFRESH_QUANTA)
        {
            refresh_all_queues();
            elapsed_time_since_refresh = 0;
        }

        tcb *next_thread = NULL;
        if (mlfq->high_prio_queue->length > 0)
        {
            next_thread = blocked_queue_remove(mlfq->high_prio_queue);
        }
        else if (mlfq->medium_prio_queue->length > 0)
        {
            next_thread = blocked_queue_remove(mlfq->medium_prio_queue);
        }
        else if (mlfq->default_prio_queue->length > 0)
        {
            next_thread = blocked_queue_remove(mlfq->default_prio_queue);
        }
        else if (mlfq->low_prio_queue->length > 0)
        {
            next_thread = blocked_queue_remove(mlfq->low_prio_queue);
        }
        else
        {
            // No more threads to schedule; return control
            unblock_timer_signal(&old_set);
            break;
        }

        if (next_thread != NULL)
        {
            current_tcb_executing = next_thread;
            current_tcb_executing->stat = RUNNING;

            // Set time_remaining if not already set
            if (current_tcb_executing->time_remaining <= 0)
            {
                current_tcb_executing->time_remaining = get_time_quantum(current_tcb_executing->current_queue_level);
            }

            unblock_timer_signal(&old_set);
            start_timer();
            // printf("sched_mlfq: swapping to thread %d\n", current_tcb_executing->thread_id);
            swapcontext(&(scheduler_thread->context), &(current_tcb_executing->context));
            elapsed_time_since_refresh += TIME_QUANTA;
        }
    }
}

void MLFQ_init()
{
    mlfq = (MLFQ_t *)malloc(sizeof(MLFQ_t));

    if (mlfq == NULL)
    {
        perror("MLFQ failed to initialize");
        exit(1);
    }

    mlfq->high_prio_queue = blocked_queue_init();
    mlfq->medium_prio_queue = blocked_queue_init();
    mlfq->default_prio_queue = blocked_queue_init();
    mlfq->low_prio_queue = blocked_queue_init();

    if (
        mlfq->high_prio_queue == NULL ||
        mlfq->medium_prio_queue == NULL ||
        mlfq->default_prio_queue == NULL ||
        mlfq->low_prio_queue == NULL)
    {
        perror("MLFQ queues failed to initialize");
        exit(1);
    }
}

int MLFQ_add(int priority_level, tcb *thread)
{
    int res = -1;
    if (thread->in_queue)
    {
        return res;
    }
    thread->in_queue = 1;
    switch (priority_level)
    {
    case HIGH_PRIO:
        res = blocked_queue_add(mlfq->high_prio_queue, thread);
        break;
    case MEDIUM_PRIO:
        res = blocked_queue_add(mlfq->medium_prio_queue, thread);
        break;
    case DEFAULT_PRIO:
        res = blocked_queue_add(mlfq->default_prio_queue, thread);
        break;
    case LOW_PRIO:
        res = blocked_queue_add(mlfq->low_prio_queue, thread);
        break;
    default:
        res = blocked_queue_add(mlfq->default_prio_queue, thread);
        break;
    }
    // printf("MLFQ_add: added thread to blocked queue with thread id %d\n", thread->thread_id);
    return res;
}

void MLFQ_remove_thread(tcb *thread)
{
    thread->in_queue = 0;
    BlockedQueue *queue = NULL;
    switch (thread->current_queue_level)
    {
    case HIGH_PRIO:
        queue = mlfq->high_prio_queue;
        break;
    case MEDIUM_PRIO:
        queue = mlfq->medium_prio_queue;
        break;
    case DEFAULT_PRIO:
        queue = mlfq->default_prio_queue;
        break;
    case LOW_PRIO:
        queue = mlfq->low_prio_queue;
        break;
    default:
        queue = mlfq->default_prio_queue;
        break;
    }

    // Remove thread from the queue
    Node *current = queue->front;
    Node *prev = NULL;

    while (current != NULL)
    {
        if (current->data->thread_id == thread->thread_id)
        {
            if (prev == NULL)
            {
                queue->front = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            if (current->next == NULL)
            {
                queue->rear = prev;
            }
            free(current);
            queue->length--;
            return;
        }
        prev = current;
        current = current->next;
    }
}

int get_time_quantum(int priority_level)
{
    // Define these macros appropriately
    switch (priority_level)
    {
    case HIGH_PRIO:
        return HIGH_PRIO_QUANTA;
    case MEDIUM_PRIO:
        return MEDIUM_PRIO_QUANTA;
    case DEFAULT_PRIO:
        return DEFAULT_PRIO_QUANTA;
    case LOW_PRIO:
        return LOW_PRIO_QUANTA;
    default:
        return DEFAULT_PRIO_QUANTA;
    }
}

void demote_thread(tcb *thread)
{
    if (!thread)
    {
        return;
    }

    if (thread->current_queue_level == LOW_PRIO)
    {
        thread->time_remaining = get_time_quantum(LOW_PRIO);
        return;
    }

    if (thread->current_queue_level == HIGH_PRIO)
    {
        thread->current_queue_level = MEDIUM_PRIO;
    }
    else if (thread->current_queue_level == MEDIUM_PRIO)
    {
        thread->current_queue_level = DEFAULT_PRIO;
    }
    else if (thread->current_queue_level == DEFAULT_PRIO)
    {
        thread->current_queue_level = LOW_PRIO;
    }

    thread->time_remaining = get_time_quantum(thread->current_queue_level);
    // printf("demote_thread: demoted thread with thread id %d\n", thread->thread_id);
}

void refresh_all_queues()
{
    BlockedQueue *queues[] = {mlfq->medium_prio_queue, mlfq->default_prio_queue, mlfq->low_prio_queue};
    for (int i = 0; i < 3; i++)
    {
        BlockedQueue *q = queues[i];
        while (q->length > 0)
        {
            tcb *thread = blocked_queue_remove(q);
            thread->current_queue_level = HIGH_PRIO;
            thread->time_remaining = get_time_quantum(HIGH_PRIO);
            blocked_queue_add(mlfq->high_prio_queue, thread);
        }
    }
}
#endif // MLFQ

void print_app_stats(void)
{
    fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
    fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
    fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}
