#include "thread-worker.h"
#include <stdio.h>

void hello() 
{
	printf("now executing hello1 \n");
	 while(1) {
	 
		printf("hello\n");
		// add a wait to ensure proper execution
		for (int i = 0; i < 10000000; i++);
	 }
}

void hello2() 
{
	printf("now executing hello2 \n");
	 while(1) {
	 
		printf("hello2\n");
		// add a wait to ensure proper execution
		for (int i = 0; i < 10000000; i++);
	 }
}

int main() {
	printf("hey:D\n");

	worker_t * threadnum;
	worker_t * threadnum2;

	int res = worker_create(threadnum, NULL, (void *) &hello, NULL);
	int res2 = worker_create(threadnum2, NULL, (void *) &hello2, NULL);

	return 0;
}
