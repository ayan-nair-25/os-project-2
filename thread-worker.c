// File:	thread-worker.c

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

static atomic_int time_since_last_refresh = 0;

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
	// probably need to add a check if already in blocked queue
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
	tcb *ret;
	if (queue == NULL || queue->length == 0)
	{
		ret = NULL;
	}
	else if (queue->length == 1)
	{
		ret = queue->front->data;
		queue->front = queue->rear = NULL;
		queue->length--;
	}
	else
	{
		ret = queue->rear->data;
		queue->rear = queue->rear->prev;
		queue->rear->next = NULL;
		queue->length--;
	}
	return ret;
}

tcb *queue_remove_specific(Queue *queue, worker_t thread_to_remove)
{
	tcb *ret;
	if (queue == NULL || queue->length == 0 ||
			(queue->length == 1 && queue->front->data->thread_id != thread_to_remove))
	{
		ret = NULL;
	}
	else if (queue->length == 1)
	{
		ret = queue->front->data;
		queue->front = queue->rear = NULL;
		queue->length--;
	}
	else
	{
		Node *ptr = queue->front;
		while (ptr != NULL)
		{
			if (ptr->data->thread_id = thread_to_remove)
			{
				ret = ptr->data;
				Node *prev = ptr->prev;
				prev->next = ptr->next;
				break;
			}
		}
	}
	return ret;
}

// check if this works
int unblock_threads(Queue *queue)
{
	if (queue == NULL || heap == NULL)
	{
		// handle error
		return -1;
	}

	Node *ptr = queue->front;
	// add to runqueue and free blocked queue nodes
	while (ptr)
	{
		Node *temp = ptr->next;
		pq_add(ptr->data);
		free(ptr);
		ptr = temp;
	}

	free(queue);
	queue = NULL;
	return 0;
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
	ucontext_t cctx;
	cctx.uc_link = NULL;
	cctx.uc_stack.ss_sp = worker_tcb->stack;
	cctx.uc_stack.ss_size = STACK_SIZE;
	cctx.uc_stack.ss_flags = 0;
	// where to initialize the context to start?
	makecontext(&cctx, function, 0);
	worker_tcb->context = cctx;
	// after everything is set, push this thread into run queue and
	// initialize pq if not initialized alr
	worker_tcb->waiting_threads = queue_init();
	if (heap == NULL)
	{
		pq_init();
	}
	pq_add(worker_tcb);
	// - make it ready for the execution.

	sigset_t old_set;
	block_timer_signal(&old_set);

	queue_add(all_tcb_queue, worker_tcb);

	MLFQ_add(HIGH_PRIO, worker_tcb);

	// YOUR CODE HERE

	return 0
};

#ifdef MLFQ
/* This function gets called only for MLFQ scheduling set the worker priority. */
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

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{

	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	// YOUR CODE HERE

	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {

};

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
};

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
	if (target_thread->waiting_threads == NULL)
	{
		target_thread->waiting_threads = queue_init();
	}
	queue_add(target_thread->waiting_threads, current_thread);
	current_thread->stat = BLOCKED;

	unblock_timer_signal(&old_set);
	swapcontext(&(current_thread->context), &(scheduler_context));

	return 0;
};

/* initialize the mutex lock */
// can assume mutexattr is NULL
int worker_mutex_init(worker_mutex_t *mutex,
											const pthread_mutexattr_t *mutexattr)
{
	//- initialize data structures for this mutex

	mutex = (worker_mutex_t *)malloc(sizeof(worker_mutex_t));
	if (mutex == NULL)
	{
		// handle error
		return -1;
	}
	mutex->status = UNLOCKED;
	mutex->owner_thread = NULL;
	mutex->queue = queue_init();
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
	tcb *curr_thread = current_running_thread;
	int expected = UNLOCKED;
	while (!__atomic_compare_exchange_n(
			&(mutex->status),
			&expected,
			LOCKED,
			0,
			__ATOMIC_ACQUIRE,
			__ATOMIC_RELAXED))
	{
		// Block signals during critical section
		sigset_t old_set;
		block_timer_signal(&old_set);

		// Add current thread to mutex waiting list
		queue_add(mutex->queue, curr_thread);
		curr_thread->stat = BLOCKED;

		// Unblock signals
		unblock_timer_signal(&old_set);

		// Swap to scheduler
		swapcontext(&(curr_thread->context), &scheduler_context);
		expected = UNLOCKED;
	}

	mutex->owner_thread = curr_thread;
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
	// - release mutex and make it available again.
	// - put threads in block list to run queue
	// so that they could compete for mutex later.

	tcb *current_thread = NULL;
	if (mutex->owner_thread != current_thread)
	{
		return -1;
	}

	mutex->owner_thread = NULL;
	__atomic_store_n(&(mutex->status), UNLOCKED, __ATOMIC_RELEASE);
	tcb *next_thread;
	if (queue_remove(mutex->queue) == 0)
	{
		queue_add(mutex->queue, next_thread);
	}
	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
	// - de-allocate dynamic memory created in worker_mutex_init
	free_queue(mutex->queue);
	mutex->owner_thread = NULL;
	free(mutex);
	mutex = NULL;
	return 0;
};

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
	int res;
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
	}
	return res;
}

tcb *MLFQ_remove(int priority_level)
{
	tcb *res;
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
	}
}

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

	current_running_thread = NULL;

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
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

static void refresh_all_queues()
{
	sigset_t old_set;
	block_timer_signal(&old_set);

	while (mlfq->low_prio_queue->length > 0)
	{
		tcb *thread = queue_remove(mlfq->low_prio_queue);
		queue_add(mlfq->high_prio_queue, thread);
	}
	while (mlfq->default_prio_queue->length > 0)
	{
		tcb *thread = queue_remove(mlfq->default_prio_queue);
		queue_add(mlfq->high_prio_queue, thread);
	}
	while (mlfq->medium_prio_queue->length > 0)
	{
		tcb *thread = queue_remove(mlfq->medium_prio_queue);
		queue_add(mlfq->high_prio_queue, thread);
	}

	unblock_timer_signal(&old_set);
}

static void demote_thread(tcb *thread)
{
	// already fucked, spare its life
	if (thread->stat != LOW_PRIO)
	{
		return;
	}

	sigset_t old_set;
	block_timer_signal(&old_set);

	Queue *queue_to_search;
	Queue *queue_to_add_to;
	if (thread->stat == HIGH_PRIO)
	{
		queue_to_search = mlfq->high_prio_queue;
		queue_to_add_to = mlfq->medium_prio_queue;
	}
	else if (thread->stat == MEDIUM_PRIO)
	{
		queue_to_search = mlfq->medium_prio_queue;
		queue_to_add_to = mlfq->default_prio_queue;
	}
	else
	{
		queue_to_search = mlfq->default_prio_queue;
		queue_to_add_to = mlfq->low_prio_queue;
	}

	Node *ptr = queue_to_search->front;
	while (ptr != NULL)
	{
		if (ptr->data->thread_id == thread->thread_id)
		{
			tcb *rem_thread = queue_remove_specific(queue_to_search, thread->thread_id);
			queue_add(queue_to_add_to, rem_thread);
		}
		ptr = ptr->next;
	}
}

/* Preemptive MLFQ scheduling algorithm */
/* Need to track a thread's elapsed time and figure out when to demote a thread */
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

	if (next_thread != NULL)
	{
		current_running_thread = next_thread;

		// update benchmarks
		// context switch type beat
		swapcontext(&(scheduler_context), &(current_running_thread->context));
	}

	unblock_timer_signal(&old_set);
}