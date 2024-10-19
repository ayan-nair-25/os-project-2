#include <stdio.h>
#include <stdlib.h>
#include "thread-worker.h"

#define NUM_THREADS 5

void *thread_function(void *arg)
{
    int thread_num = *((int *)arg);
    free(arg);

    printf("Thread %d started\n", thread_num);

    int work_iterations = (thread_num + 1) * 10000000;

    for (volatile int i = 0; i < work_iterations; i++)
    {
        if (i % 5000000 == 0)
        {
            printf("Thread %d is working at iteration %d\n", thread_num, i);
        }
    }

    printf("Thread %d finished\n", thread_num);

    return NULL;
}

int main(int argc, char *argv[])
{
    worker_t threads[NUM_THREADS];

    printf("Made it here\n");

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++)
    {
        int *thread_num = malloc(sizeof(int));
        if (thread_num == NULL)
        {
            perror("Failed to allocate memory for thread_num");
            exit(EXIT_FAILURE);
        }
        *thread_num = i;
        if (worker_create(&threads[i], NULL, thread_function, thread_num) != 0)
        {
            perror("Failed to create thread");
            exit(EXIT_FAILURE);
        }
    }

    // Start the scheduler to run the threads
    run_threads();

    // Control will return here after all threads have finished
    printf("All threads have finished execution.\n");

    return 0;
}
