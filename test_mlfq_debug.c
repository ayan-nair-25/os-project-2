#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "thread-worker.h"

#define NUM_THREADS 5
#define WORKLOAD_FACTOR 1000000

void *thread_function(void *arg) {
    int thread_num = *((int *)arg);
    free(arg);

    printf("Thread %d started\n", thread_num);

    // Each thread has a different workload
    int64_t work_iterations = (thread_num + 1) * WORKLOAD_FACTOR;

    for (volatile int64_t i = 0; i < work_iterations; i++) {
      // printf("Bruh.\n");
    }

    printf("Thread %d finished\n", thread_num);

    return NULL;
}

int main(int argc, char *argv[]) {
    worker_t threads[NUM_THREADS];

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        int *thread_num = malloc(sizeof(int));
        if (thread_num == NULL) {
            perror("Failed to allocate memory for thread_num");
            exit(EXIT_FAILURE);
        }
        *thread_num = i;
        if (worker_create(&threads[i], NULL, thread_function, thread_num) != 0) {
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
