// File: test_mutex.c

#include <stdio.h>
#include <stdlib.h>
#include "thread-worker.h"

#define NUM_THREADS 5
#define NUM_ITERATIONS 10

worker_mutex_t mutex;
int shared_counter = 0;

void *increment_counter(void *arg)
{
    int thread_id = *((int *)arg);
    free(arg); // Free the allocated memory

    for (int i = 0; i < NUM_ITERATIONS; i++)
    {
        worker_mutex_lock(&mutex);

        int temp = shared_counter;
        temp++;
        shared_counter = temp;

        worker_mutex_unlock(&mutex);
    }

    printf("Thread %d finished\n", thread_id);
    return NULL;
}

int main()
{
    printf("Main: Starting program\n");

    worker_t threads[NUM_THREADS];

    // Initialize the mutex
    if (worker_mutex_init(&mutex, NULL) != 0)
    {
        fprintf(stderr, "Failed to initialize mutex\n");
        exit(1);
    }

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++)
    {
        int *arg = malloc(sizeof(*arg));
        *arg = i + 1; // Thread ID
        if (worker_create(&threads[i], NULL, increment_counter, arg) != 0)
        {
            fprintf(stderr, "Failed to create thread %d\n", i + 1);
            exit(1);
        }
        printf("Main: Created thread %d\n", i + 1);
    }

    // Wait for threads to finish
    for (int i = 0; i < NUM_THREADS; i++)
    {
        worker_join(threads[i], NULL);
        printf("Main: Joined thread %d\n", i + 1);
    }

    // Destroy the mutex
    if (worker_mutex_destroy(&mutex) != 0)
    {
        fprintf(stderr, "Failed to destroy mutex\n");
        exit(1);
    }

    printf("Main: All threads have finished\n");
    printf("Final value of shared_counter: %d\n", shared_counter);

    return 0;
}
