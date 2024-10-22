
#include "thread-worker.h"
#include <stdio.h>

void *hello(void *arg)
{
    printf("Thread function hello is executing\n");
    for (int i = 0; i < 1000; i++)
    {
        printf("hello\n");
        for (int j = 0; j < 1000000; j++);
    }

    worker_exit(NULL);
    return NULL;
}

void *hello2(void *arg)
{
    printf("Thread function hello2 is executing\n");
    for (int i = 0; i < 1000; i++)
    {
        printf("hello2\n");
        for (int j = 0; j < 1000000; j++);
    }

    worker_exit(NULL);
    return NULL;
}

void *hello3(void *arg)
{
    printf("Thread function hello3 is executing\n");
    for (int i = 0; i < 1000; i++)
    {
        printf("hello3\n");
        for (int j = 0; j < 1000000; j++);
    }

    worker_exit(NULL);
    return NULL;
}

void *hello4(void *arg)
{
    printf("Thread function hello4 is executing\n");
    for (int i = 0; i < 1000; i++)
    {
        printf("hello4\n");
        for (int j = 0; j < 1000000; j++);
    }

    worker_exit(NULL);
    return NULL;
}

int main()
{
    printf("Main: Starting program\n");

    worker_t threadnum;
    worker_t threadnum2;
    worker_t threadnum3;
    worker_t threadnum4;

    printf("Main: Creating thread 1 for hello...\n");
    int res = worker_create(&threadnum, NULL, hello, NULL);
    printf("Main: Creating thread 2 for hello2...\n");
    int res2 = worker_create(&threadnum2, NULL, hello2, NULL);
    printf("Main: Creating thread 3 for hello3...\n");
    int res3 = worker_create(&threadnum3, NULL, hello3, NULL);
    printf("Main: Creating thread 4 for hello4...\n");
    int res4 = worker_create(&threadnum4, NULL, hello4, NULL);

    printf("Main: Waiting for threads to finish\n");
    worker_join(threadnum, NULL);
    worker_join(threadnum2, NULL);
    worker_join(threadnum3, NULL);
    worker_join(threadnum4, NULL);

    printf("Main: All threads have finished\n");
    return 0;
}

