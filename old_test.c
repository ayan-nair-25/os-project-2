#include "thread-worker.h"
#include <stdio.h>

// gcc old_test.c thread-worker.c -o test
void hello() 
{
	printf("now executing hello1 \n");
	//for(int i = 0; i < 1000000; i++)
	for (int i = 0; i < 1000; i++)
//	while(1)
	{
		printf("hello\n");
		for(int i = 0; i < 1000000; i++);
	}

	worker_exit(NULL);
		// add a wait to ensure proper execution
}

void hello2() 
{
	printf("now executing hello2 \n");
	//for(int i = 0; i < 1000000; i++)
	for (int i = 0; i < 1000; i++)
	//while(1)
	{
		printf("hello2\n");
		for(int i = 0; i < 1000000; i++);
	}

	worker_exit(NULL);
		// add a wait to ensure proper execution
}


void hello3() 
{
	printf("now executing hello2 \n");
	//for(int i = 0; i < 1000000; i++)
	for (int i = 0; i < 1000; i++)
	//while(1)
	{
		printf("hello3\n");
		for(int i = 0; i < 1000000; i++);
	}

	worker_exit(NULL);
		// add a wait to ensure proper execution
}

void hello4() 
{
	printf("now executing hello2 \n");
	//for(int i = 0; i < 1000000; i++)
	for (int i = 0; i < 1000; i++)
	//while(1)
	{
		printf("hello4\n");
		for(int i = 0; i < 1000000; i++);
	}

	worker_exit(NULL);
		// add a wait to ensure proper execution
}

int main() {
	printf("hey:D\n");

	worker_t * threadnum;
	worker_t * threadnum2;
	worker_t * threadnum3;
	worker_t * threadnum4;
	

	printf("creating thread 1 for hello...\n");
	int res = worker_create(threadnum, NULL, (void *) &hello, NULL);
	printf("creating thread 2 for hello2...\n");
	int res2 = worker_create(threadnum2, NULL, (void *) &hello2, NULL);
	printf("creating thread 3 for hello3...\n");
	int res3 = worker_create(threadnum3, NULL, (void *) &hello3, NULL);
	printf("creating thread 4 for hello4...\n");
	int res4 = worker_create(threadnum4, NULL, (void *) &hello4, NULL);


	worker_join(*threadnum1, NULL);
	worker_join(*threadnum2, NULL);
	worker_join(*threadnum3, NULL);
	worker_join(*threadnum4, NULL);

	return 0;
}
