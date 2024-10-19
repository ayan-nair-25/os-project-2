// File:    thread-worker.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "thread-worker.h"

// Global counter for total context switches and
// average turn around and response time
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;

worker_t current_thread_id = 0;

static PriorityQueue *heap;
static MLFQ *mlfq;
tcb *current_running_thread = NULL;
ucontext_t scheduler_context;
static Queue *all_tcb_queue = NULL;

// To keep track of time since last refresh for MLFQ
static atomic_int time_since_last_refresh = 0;

// For timer interrupt handling
struct itimerval timer;
struct sigaction sa;

// ----------------------------- //
// Min-Priority Queue for PSJF (not modified)

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

// ----------------------------- //
// Queue Implementation

Node *create_node(tcb *data, Node *prev)
{
    Node *new_node = malloc(sizeof(Node));
    new_node->data = data;
    new_node->next = NULL;
    new_node->prev = prev;
    return new_node;
}

Queue *queue_init()
{
    Queue *queue = malloc(sizeof(Queue));
    if (queue == NULL)
    {
        // handle error
        return NULL;
    }
    queue->front = queue->rear = NULL;
    queue->length = 0;
    return queue;
}

int queue_add(Queue *queue, tcb *thread)
{
    if (thread == NULL || queue == NULL)
    {
        return -1;
    }
    else if (queue->length == 0)
    {
        queue->front = create_node(thread, NULL);
        queue->rear = queue->front;
    }
    else
    {
        Node *new_node = create_node(thread, queue->rear);
        queue->rear->next = new_node;
        queue->rear = new_node;
    }

    queue->length++;
    return 0;
}

tcb *queue_remove(Queue *queue)
{
    tcb *ret = NULL;
    if (queue == NULL || queue->length == 0)
    {
        ret = NULL;
    }
    else
    {
        ret = queue->front->data;
        Node *temp = queue->front;
        queue->front = queue->front->next;
        if (queue->front != NULL)
        {
            queue->front->prev = NULL;
        }
        else
        {
            queue->rear = NULL;
        }
        free(temp);
        queue->length--;
    }
    return ret;
}

tcb *queue_remove_specific(Queue *queue, worker_t thread_to_remove)
{
    tcb *ret = NULL;
    if (queue == NULL || queue->length == 0)
    {
        ret = NULL;
    }
    else
    {
        Node *ptr = queue->front;
        while (ptr != NULL)
        {
            if (ptr->data->thread_id == thread_to_remove)
            {
                ret = ptr->data;
                if (ptr->prev != NULL)
                {
                    ptr->prev->next = ptr->next;
                }
                else
                {
                    queue->front = ptr->next;
                }
                if (ptr->next != NULL)
                {
                    ptr->next->prev = ptr->prev;
                }
                else
                {
                    queue->rear = ptr->prev;
                }
                free(ptr);
                queue->length--;
                break;
            }
            ptr = ptr->next;
        }
    }
    return ret;
}

void free_queue(Queue *queue)
{
    if (queue == NULL || queue->length == 0)
    {
        return;
    }
    Node *ptr = queue->front;
    while (ptr != NULL)
    {
        Node *temp_node = ptr->next;
        free(ptr);
        ptr = temp_node;
    }
    free(queue);
    queue = NULL;
}

// ----------------------------- //
// Signal Blocking Functions

void block_timer_signal(sigset_t *old_set)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &set, old_set);
}

void unblock_timer_signal(sigset_t *old_set)
{
    sigprocmask(SIG_SETMASK, old_set, NULL);
}

// ----------------------------- //
// Timer and Scheduler Initialization

void init_scheduler()
{
    // Initialize scheduler context
    getcontext(&scheduler_context);
    scheduler_context.uc_link = NULL;
    scheduler_context.uc_stack.ss_sp = malloc(STACK_SIZE);
    scheduler_context.uc_stack.ss_size = STACK_SIZE;
    scheduler_context.uc_stack.ss_flags = 0;
    makecontext(&scheduler_context, schedule, 0);

    // Set up the signal handler for timer interrupts
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGVTALRM, &sa, NULL);

    // Start the timer with an initial value
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TIME_QUANTUM_USEC; // Start with default quantum
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0; // No auto-reload
    setitimer(ITIMER_VIRTUAL, &timer, NULL);
}

void timer_handler(int signum)
{
    sigset_t old_set;
    block_timer_signal(&old_set);

    time_since_last_refresh += TIME_QUANTUM_USEC;

    current_running_thread->time_remaining -= TIME_QUANTUM_USEC;

    if (current_running_thread->time_remaining <= 0)
    {
        // Time quantum used up, demote thread
        demote_thread(current_running_thread);

        // Set status to READY and add back to MLFQ
        current_running_thread->stat = READY;
        MLFQ_add(current_running_thread->current_queue_level, current_running_thread);

        // Switch to scheduler
        swapcontext(&(current_running_thread->context), &scheduler_context);
    }
    else
    {
        // Reset the timer with the remaining time
        timer.it_value.tv_sec = 0;
        timer.it_value.tv_usec = TIME_QUANTUM_USEC;
        setitimer(ITIMER_VIRTUAL, &timer, NULL);
    }

    // Check if it's time to refresh the queues
    if (time_since_last_refresh >= REFRESH_QUANTUM)
    {
        refresh_all_queues();
        time_since_last_refresh = 0;
    }

    unblock_timer_signal(&old_set);
}

// ----------------------------- //
// Thread Creation (Not Modified)

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
    if (all_tcb_queue == NULL)
    {
        all_tcb_queue = queue_init();
    }

    if (mlfq == NULL)
    {
        MLFQ_init();
        init_scheduler();
    }

    // - create Thread Control Block (TCB)
    tcb *worker_tcb = malloc(sizeof(tcb));
    worker_tcb->thread_id = current_thread_id++;
    // - allocate space of stack for this thread to run
    worker_tcb->stack = malloc(STACK_SIZE);
    // error check
    if (worker_tcb->stack == NULL)
    {
        perror("Failed to allocate stack");
        exit(1);
    }
    // - create and initialize the context of this worker thread
    getcontext(&(worker_tcb->context));
    worker_tcb->context.uc_link = NULL;
    worker_tcb->context.uc_stack.ss_sp = worker_tcb->stack;
    worker_tcb->context.uc_stack.ss_size = STACK_SIZE;
    worker_tcb->context.uc_stack.ss_flags = 0;
    makecontext(&(worker_tcb->context), (void (*)(void))function, 1, arg);

    // Initialize thread fields
    worker_tcb->stat = READY;
    worker_tcb->priority = HIGH_PRIO; // Default priority
    worker_tcb->current_queue_level = HIGH_PRIO;
    worker_tcb->time_remaining = get_time_quantum(HIGH_PRIO);
    worker_tcb->waiting_threads = queue_init();
    worker_tcb->elapsed_time = 0;
    worker_tcb->exit_value = NULL;
    worker_tcb->parent_thread = NULL;

    // Add to all TCBs
    sigset_t old_set;
    block_timer_signal(&old_set);

    queue_add(all_tcb_queue, worker_tcb);

    // Add to MLFQ
    MLFQ_add(worker_tcb->current_queue_level, worker_tcb);

    unblock_timer_signal(&old_set);

    *thread = worker_tcb->thread_id;

    return 0;
}

// ----------------------------- //
// Worker Yield (Not Modified)

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{
    // - change worker thread's state from Running to Ready
    // - save context of this thread to its thread control block
    // - switch from thread context to scheduler context

    // YOUR CODE HERE

    return 0;
}

// ----------------------------- //
// Worker Exit (Not Modified)

/* terminate a thread */
void worker_exit(void *value_ptr)
{
    // YOUR CODE HERE
}

// ----------------------------- //
// Worker Join

static tcb *_find_thread(worker_t thread)
{
    if (all_tcb_queue == NULL || all_tcb_queue->length == 0)
    {
        return NULL;
    }
    Node *current = all_tcb_queue->front;
    while (current != NULL)
    {
        if (current->data->thread_id == thread)
        {
            return current->data;
        }
        current = current->next;
    }
    return NULL;
}

int worker_join(worker_t thread, void **value_ptr)
{
    tcb *target_thread = _find_thread(thread);
    if (target_thread == NULL)
    {
        return -1;
    }

    if (target_thread->stat == TERMINATED)
    {
        if (value_ptr != NULL)
        {
            *value_ptr = target_thread->exit_value;
        }
        return 0;
    }

    sigset_t old_set;
    block_timer_signal(&old_set);

    tcb *current_thread = current_running_thread;

    // Add current thread to target's waiting_threads queue
    queue_add(target_thread->waiting_threads, current_thread);
    current_thread->stat = BLOCKED;

    unblock_timer_signal(&old_set);

    swapcontext(&(current_thread->context), &scheduler_context);

    // When resumed, the target thread has terminated
    if (value_ptr != NULL)
    {
        *value_ptr = target_thread->exit_value;
    }

    return 0;
}

// ----------------------------- //
// Mutex Implementation

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    if (mutex == NULL)
    {
        return -1;
    }

    mutex->status = UNLOCKED;
    mutex->owner_thread = NULL;
    mutex->queue = queue_init();
    if (mutex->queue == NULL)
    {
        perror("Queue init failure");
        exit(1);
    }
    return 0;
}

/* acquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
    sigset_t old_set;
    block_timer_signal(&old_set);

    int expected = UNLOCKED;
    if (!__atomic_compare_exchange_n(
            &(mutex->status),
            &expected,
            LOCKED,
            0,
            __ATOMIC_ACQUIRE,
            __ATOMIC_RELAXED))
    {
        // Failed to acquire mutex, add to waiting queue
        queue_add(mutex->queue, current_running_thread);
        current_running_thread->stat = BLOCKED;

        unblock_timer_signal(&old_set);

        // Swap to scheduler
        swapcontext(&(current_running_thread->context), &scheduler_context);

        // Upon return, try to acquire the mutex again
        block_timer_signal(&old_set);

        expected = UNLOCKED;
        while (!__atomic_compare_exchange_n(
                   &(mutex->status),
                   &expected,
                   LOCKED,
                   0,
                   __ATOMIC_ACQUIRE,
                   __ATOMIC_RELAXED))
        {
            unblock_timer_signal(&old_set);
            swapcontext(&(current_running_thread->context), &scheduler_context);
            block_timer_signal(&old_set);
            expected = UNLOCKED;
        }
    }

    mutex->owner_thread = current_running_thread;

    unblock_timer_signal(&old_set);

    return 0;
}

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
    if (mutex->owner_thread != current_running_thread)
    {
        return -1;
    }

    sigset_t old_set;
    block_timer_signal(&old_set);

    mutex->owner_thread = NULL;
    __atomic_store_n(&(mutex->status), UNLOCKED, __ATOMIC_RELEASE);

    tcb *next_thread = queue_remove(mutex->queue);
    if (next_thread != NULL)
    {
        next_thread->stat = READY;
        MLFQ_add(next_thread->current_queue_level, next_thread);
    }

    unblock_timer_signal(&old_set);

    return 0;
}

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
    free_queue(mutex->queue);
    mutex->queue = NULL;
    mutex->owner_thread = NULL;
    return 0;
}

// ----------------------------- //
// MLFQ Implementation

void MLFQ_init()
{
    mlfq = (MLFQ *)malloc(sizeof(MLFQ));

    if (mlfq == NULL)
    {
        perror("MLFQ failed to initialize");
        exit(1);
    }

    mlfq->high_prio_queue = queue_init();
    mlfq->medium_prio_queue = queue_init();
    mlfq->default_prio_queue = queue_init();
    mlfq->low_prio_queue = queue_init();

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
    switch (priority_level)
    {
    case HIGH_PRIO:
        res = queue_add(mlfq->high_prio_queue, thread);
        break;
    case MEDIUM_PRIO:
        res = queue_add(mlfq->medium_prio_queue, thread);
        break;
    case DEFAULT_PRIO:
        res = queue_add(mlfq->default_prio_queue, thread);
        break;
    case LOW_PRIO:
        res = queue_add(mlfq->low_prio_queue, thread);
        break;
    default:
        res = queue_add(mlfq->default_prio_queue, thread);
        break;
    }
    return res;
}

tcb *MLFQ_remove(int priority_level)
{
    tcb *res = NULL;
    switch (priority_level)
    {
    case HIGH_PRIO:
        res = queue_remove(mlfq->high_prio_queue);
        break;
    case MEDIUM_PRIO:
        res = queue_remove(mlfq->medium_prio_queue);
        break;
    case DEFAULT_PRIO:
        res = queue_remove(mlfq->default_prio_queue);
        break;
    case LOW_PRIO:
        res = queue_remove(mlfq->low_prio_queue);
        break;
    default:
        res = queue_remove(mlfq->default_prio_queue);
        break;
    }
    return res;
}

int get_time_quantum(int priority_level)
{
    switch (priority_level)
    {
    case HIGH_PRIO:
        return HIGH_PRIO_QUANTUM_USEC;
    case MEDIUM_PRIO:
        return MEDIUM_PRIO_QUANTUM_USEC;
    case DEFAULT_PRIO:
        return DEFAULT_PRIO_QUANTUM_USEC;
    case LOW_PRIO:
        return LOW_PRIO_QUANTUM_USEC;
    default:
        return DEFAULT_PRIO_QUANTUM_USEC;
    }
}

void demote_thread(tcb *thread)
{
    if (thread->current_queue_level == LOW_PRIO)
    {
        // Already at lowest priority
        thread->time_remaining = get_time_quantum(LOW_PRIO);
        return;
    }

    sigset_t old_set;
    block_timer_signal(&old_set);

    // Demote to next lower priority
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

    // Reset time_remaining
    thread->time_remaining = get_time_quantum(thread->current_queue_level);

    unblock_timer_signal(&old_set);
}

void refresh_all_queues()
{
    sigset_t old_set;
    block_timer_signal(&old_set);

    Queue *queues[] = {mlfq->medium_prio_queue, mlfq->default_prio_queue, mlfq->low_prio_queue};
    for (int i = 0; i < 3; i++)
    {
        Queue *q = queues[i];
        while (q->length > 0)
        {
            tcb *thread = queue_remove(q);
            thread->current_queue_level = HIGH_PRIO;
            thread->time_remaining = get_time_quantum(HIGH_PRIO);
            queue_add(mlfq->high_prio_queue, thread);
        }
    }

    unblock_timer_signal(&old_set);
}

// ----------------------------- //
// Scheduler

/* scheduler */
static void schedule()
{
    // - every time a timer interrupt occurs, your worker thread library
    // should be context-switched from a thread context to this
    // schedule() function

    // - invoke scheduling algorithms according to the policy (PSJF or MLFQ)

    // if (sched == PSJF)
    //      sched_psjf();
    // else if (sched == MLFQ)
    //      sched_mlfq();

    // YOUR CODE HERE

    // - schedule policy
#ifndef MLFQ
    sched_psjf();
#else
    sched_mlfq();
#endif
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf()
{
    // Not modified
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
{
    sigset_t old_set;
    block_timer_signal(&old_set);

    if (time_since_last_refresh >= REFRESH_QUANTUM)
    {
        refresh_all_queues();
        time_since_last_refresh = 0;
    }

    tcb *next_thread = NULL;
    if (mlfq->high_prio_queue->length > 0)
    {
        next_thread = queue_remove(mlfq->high_prio_queue);
    }
    else if (mlfq->medium_prio_queue->length > 0)
    {
        next_thread = queue_remove(mlfq->medium_prio_queue);
    }
    else if (mlfq->default_prio_queue->length > 0)
    {
        next_thread = queue_remove(mlfq->default_prio_queue);
    }
    else if (mlfq->low_prio_queue->length > 0)
    {
        next_thread = queue_remove(mlfq->low_prio_queue);
    }
    else
    {
        // No threads to schedule
        exit(0);
    }

    if (next_thread != NULL)
    {
        current_running_thread = next_thread;
        current_running_thread->stat = RUNNING;

        // Set time_remaining if not already set
        if (current_running_thread->time_remaining <= 0)
        {
            current_running_thread->time_remaining = get_time_quantum(current_running_thread->current_queue_level);
        }

        // Set the timer with the thread's time quantum
        timer.it_value.tv_sec = 0;
        timer.it_value.tv_usec = TIME_QUANTUM_USEC;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        setitimer(ITIMER_VIRTUAL, &timer, NULL);

        // Switch to the next thread
        swapcontext(&scheduler_context, &(current_running_thread->context));
    }

    unblock_timer_signal(&old_set);
}

// ----------------------------- //
// Worker Set Scheduling Priority

#ifdef MLFQ
/* This function gets called only for MLFQ scheduling to set the worker priority. */
int worker_setschedprio(worker_t thread, int prio)
{
    tcb *target_tcb = _find_thread(thread);
    if (target_tcb == NULL)
    {
        return -1;
    }

    sigset_t old_set;
    block_timer_signal(&old_set);

    target_tcb->priority = prio;
    target_tcb->current_queue_level = prio;
    target_tcb->time_remaining = get_time_quantum(prio);

    unblock_timer_signal(&old_set);
    return 0;
}
#endif

// ----------------------------- //
// End of thread-worker.c
