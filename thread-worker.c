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
// worker_t current_thread_id = 2;     // in the other case of using a main thread
worker_t current_thread_id = 2;

tcb *stored_main_thread = NULL;
tcb *scheduler_thread = NULL;
tcb *current_tcb_executing = NULL;

int timer_initialized = 0;
double elapsed_time_since_refresh = 0;

static PriorityQueue *heap;
static MLFQ_t *mlfq;

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
        Node *temp = ptr->next;
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

void handle_interrupt(int signum);

void setup_timer_sjf();

static void sched_psjf();
static void sched_mlfq();
static void schedule();

void start_timer();
double get_time();
void stop_timer();
/* create a new thread */
void create_context(ucontext_t *context)
{
    getcontext(context);

    context->uc_link = NULL;
    context->uc_stack.ss_sp = malloc(STACK_SIZE);
    context->uc_stack.ss_size = STACK_SIZE;
    context->uc_stack.ss_flags = NULL;

    // check that the malloc worked
    if (context->uc_stack.ss_sp == NULL)
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
    makecontext(&scheduler_cctx, (void *)&schedule, 0);
    scheduler_thread->context = scheduler_cctx;
}

void create_main_thread()
{
    tcb *main_thread = malloc(sizeof(tcb));
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

tcb *create_new_worker(worker_t *thread, void *(*function)(void *), void *arg)
{
    tcb *worker_tcb = malloc(sizeof(tcb));

    worker_tcb->thread_id = current_thread_id++;

    ucontext_t context;
    create_context(&context);

    makecontext(&context, (void *)function, 0);
    worker_tcb->context = context;

    // after everything is set, push this thread into run queue and
    // initialize pq if not initialized alr
    worker_tcb->queue = blocked_queue_init();
    worker_tcb->stat = READY;
    worker_tcb->elapsed_time = 0;
    worker_tcb->priority = HIGH_PRIO;
    worker_tcb->current_queue_level = HIGH_PRIO;
    worker_tcb->time_remaining = get_time_quantum(HIGH_PRIO);

    return worker_tcb;
}

int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
    // - create Thread Control Block (TCB)

    // - create and initialize the context of this worker thread
    // ucontext_t cctx, scheduler_cctx, main_cctx;

    // only initialize the scheduler context on the first call
    // also init the main context

    if (stored_main_thread != NULL) 
    {
	    stored_main_thread->elapsed_time += (TIME_QUANTA / 1000.0);
	    printf("readding main thread with time quanta %f\n", stored_main_thread->elapsed_time);
	    pq_add(stored_main_thread);
    }
	
    if (scheduler_thread == NULL)
    {
        // printf("initializing scheduler thread...\n");

        // setup_timer_sjf();

        setup_timer_sjf();
        timer_initialized = 1;

        pq_init();
        create_scheduler_thread();
        create_main_thread();
	printf("came here through a context switch\n");

        pq_add(stored_main_thread);
    }
    tcb *new_thread = create_new_worker(thread, function, arg);
    // otherwise we add a new thread
    pq_add(new_thread);
    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
    // YOUR CODE HERE
    return 0;
}

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

#ifdef MLFQ
/* This function gets called only for MLFQ scheduling set the worker priority. */
int worker_setschedprio(worker_t thread, int prio)
{
    tcb *target_tcb = _find_thread(thread);
    if (target_tcb == NULL)
    {
        return -1;
    }
    target_tcb->priority = prio;
    target_tcb->current_queue_level = prio;
    // time remaining?
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
    current_tcb_executing->elapsed_time += (get_time() / 1000.0);
    stop_timer();
    // add back to the queue
    pq_add(current_tcb_executing);
#else
    current_tcb_executing->remaining_time -= get_time();
    if (current_tcb_executing->remaining_time <= 0)
    {
        demote_thread(current_tcb_executing);
    }
    MLFQ_add(current_tcb_executing->priority, current_tcb_executing);
#endif
    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
    return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr)
{
    if (value_ptr != NULL)
    {
        // store result of thread in the value pointer
        current_tcb_executing->value_ptr = value_ptr;
    }
    current_tcb_executing->stat = TERMINATED;
    printf("freeing...!\n");
    free(current_tcb_executing->context.uc_stack.ss_sp);

    printf("worked!\n");
    setcontext(&(scheduler_thread->context));
};

static tcb *_find_thread(worker_t thread)
{
    if (heap == NULL)
    {
   	return; 
    }

    for (size_t i = 0; i < heap->length; i++)
    {
   	if (heap->threads[i]->thread_id == thread) 
	{
		return heap->threads[i];
	}
    }
    return NULL;
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
    tcb *target_thread = _find_thread(thread);
    if (target_thread == NULL)
    {
        return -1;
    }

    if (target_thread->stat == TERMINATED)
    {
        if (value_ptr != NULL)
        {
            *value_ptr = target_thread->value_ptr;
        }
        return 0;
    }

    blocked_queue_add(target_thread->queue, current_tcb_executing);
    current_tcb_executing->stat = BLOCKED;

    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));

    if (value_ptr != NULL)
    {
        *value_ptr = target_thread->value_ptr;
    }

    return 0;
};

/* initialize the mutex lock */
// can assume mutexattr is NULL
int worker_mutex_init(worker_mutex_t *mutex,
                      const pthread_mutexattr_t *mutexattr)
{
    if (mutex == NULL)
    {
        return -1;
    }

    mutex->status = UNLOCKED;
    mutex->owner_thread = NULL;
    mutex->queue = blocked_queue_init();
    if (mutex->queue == NULL)
    {
        perror("Queue init failure");
        exit(1);
    }
    return 0;
};

/* aquire the mutex lock */
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
        blocked_queue_add(mutex->queue, current_tcb_executing);
        current_tcb_executing->stat = BLOCKED;

        unblock_timer_signal(&old_set);

        // Swap to scheduler
        swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));

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
            swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
            block_timer_signal(&old_set);
            expected = UNLOCKED;
        }
    }

    mutex->owner_thread = current_tcb_executing;

    unblock_timer_signal(&old_set);

    return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
    if (mutex->owner_thread != current_tcb_executing)
    {
        return -1;
    }

    sigset_t old_set;
    block_timer_signal(&old_set);

    mutex->owner_thread = NULL;
    __atomic_store_n(&(mutex->status), UNLOCKED, __ATOMIC_RELEASE);

    tcb *next_thread = blocked_queue_remove(mutex->queue);
    if (next_thread != NULL)
    {
        next_thread->stat = READY;
        MLFQ_add(next_thread->current_queue_level, next_thread);
    }

    unblock_timer_signal(&old_set);

    return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
    free_blocked_queue(mutex->queue);
    mutex->queue = NULL;
    mutex->owner_thread = NULL;
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
    // printf("once again back in da scheduler \n");
    stop_timer();
#ifndef MLFQ
    // Choose PSJF
    sched_psjf();
#else
    // Choose MLFQ
    sched_mlfq();
#endif
    // printf("done scheduling\n");
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */

// what we have to do here:
// we have the general algorithm but now the problem becomes how to signal timer interrupts
// 1. create the timer
// 2. upon timer interrupt:
//	- switch back to the scheduler context
// 3. if we finish the thread before the timer interrupt then swap back to the scheduler context
//	- we then repeat the process

void handle_interrupt(int signum)
{
    // modify this to get the exact amount of time using the struct itimer
    // printf("in signal handler :D\n");
    printf("interrupting...\n");
    current_tcb_executing->elapsed_time += (TIME_QUANTA / 1000.0);
    printf("current tcb executing has ran for: %f seconds \n\n", current_tcb_executing->elapsed_time);
    current_tcb_executing->stat = READY;
#ifdef MLFQ
    current_tcb_executing->remaining_time -= TIME_QUANTA;
    if (current_tcb_executing->remaining_time <= 0)
    {
        demote_thread(current_tcb_executing);
    }
    MLFQ_add(current_tcb_executing->priority, current_tcb_executing);
#else
    printf("readding tcb with tid %d\n", current_tcb_executing->thread_id);
    pq_add(current_tcb_executing);

    // printf("currently have %d\n items in the queue\n", heap->length);
    // setcontext(&(scheduler_thread->context));
    // swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
    // swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
    //  change the above to setcontext?
    // swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
#endif
    swapcontext(&(current_tcb_executing->context), &(scheduler_thread->context));
}

double get_time()
{
    struct itimerval time;
    // get the time
    getitimer(ITIMER_PROF, &time);
    return TIME_QUANTA * 100 - time.it_value.tv_usec;
}

void setup_timer_sjf()
{
    printf("initialized timer! \n");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handle_interrupt;
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
    printf("currently have %d items on the heap\n", heap->length);
    // first pop from the heap
    // printf("removed from heap\n");
    if (heap->length == 0)
    {
    	    printf("going to main\n");
	    current_tcb_executing = stored_main_thread;
    }
    else 
    {
	    tcb *thread = pq_remove();
	    current_tcb_executing = thread;
	    printf("popped off thread with tid %d\n", thread->thread_id);
    }
    // printf("stored thread with id %d\n", current_tcb_executing->thread_id);

    // do the context switching here so that we can move our context to the wanted function that we want to execute
    // we want to swap between the scheduler context and the function context
    // printf("obtained context for current thread\n");
    // change this to setcontext?
    current_tcb_executing->stat = RUNNING;
    start_timer();
    setcontext(&(current_tcb_executing->context));
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
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

    if (next_thread != NULL)
    {
        current_tcb_executing = next_thread;
        current_tcb_executing->stat = RUNNING;

        // Set time_remaining if not already set
        if (current_tcb_executing->time_remaining <= 0)
        {
            current_tcb_executing->time_remaining = get_time_quantum(current_tcb_executing->current_queue_level);
        }

        start_timer();
        setcontext(&(current_tcb_executing->context));

        unblock_timer_signal(&old_set);
        sched_mlfq();
    }
    else
    {
        // No more threads to schedule; return control
        unblock_timer_signal(&old_set);
        return;
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
    return res;
}

tcb *MLFQ_remove(int priority_level)
{
    tcb *res = NULL;
    switch (priority_level)
    {
    case HIGH_PRIO:
        res = blocked_queue_remove(mlfq->high_prio_queue);
        break;
    case MEDIUM_PRIO:
        res = blocked_queue_remove(mlfq->medium_prio_queue);
        break;
    case DEFAULT_PRIO:
        res = blocked_queue_remove(mlfq->default_prio_queue);
        break;
    case LOW_PRIO:
        res = blocked_queue_remove(mlfq->low_prio_queue);
        break;
    default:
        res = blocked_queue_remove(mlfq->default_prio_queue);
        break;
    }
    return res;
}

int get_time_quantum(int priority_level)
{
    // still need to define these macros
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
    if (thread->current_queue_level == LOW_PRIO)
    {
        thread->time_remaining = get_time_quantum(LOW_PRIO);
        return;
    }

    int old_level = thread->current_queue_level;
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
// DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void)
{

    fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
    fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
    fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}
