#include <stdio.h>
#include <stdlib.h>
#include "thread-worker.h"

#define NUM_THREADS 8

void thread_function(void *arg)
{
  int thread_num = *((int *)arg);
  free(arg);

  printf("Thread %d started\n", thread_num);

  int work_iterations = (thread_num + 1) * 10000000;

  for (volatile int i = 0; i < work_iterations; i++)
  {
    // simulate something idk
  }

  printf("Thread %d finished\n", thread_num);
}

int main(int argc, char *argv[])
{
  worker_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++)
  {
    int *thread_num = malloc(sizeof(int));
    *thread_num = i;
    if (worker_create(&threads[i], NULL, thread_function, thread_num) != 0)
    {
      perror("HAHAHAHA");
      exit(1);
    }
  }

  ucontext_t main_context;
  getcontext(&main_context);

  scheduler_context.uc_link = &main_context;
  setcontext(&scheduler_context);

  printf("ALL THREADS DONE!\n");

  return 0;
}
