#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define STACK_SIZE SIGSTKSZ

ucontext_t foo_context;
ucontext_t bar_context;

int inc = 0;

void ring(int signum)
{
	// swap contexts here
	if (inc++ % 2 == 0) 
	{
		swapcontext(&foo_context, &bar_context);
	}
	else
	{
		swapcontext(&bar_context, &foo_context);
	}
}

void foo () 
{
	printf("executing foo...\n");
	while(1)
	{
		printf("foo ");
	}
}

void bar ()
{
	printf("executing bar...\n");
	while(1)
	{
		printf("bar ");
	}
}

void setup_timer()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &ring;
	sigaction(SIGPROF, &sa, NULL);

	struct itimerval timer;

	timer.it_interval.tv_usec = 0;
	timer.it_interval.tv_sec = 1;

	timer.it_value.tv_usec = 0;
	timer.it_value.tv_sec = 1;

	setitimer(ITIMER_PROF, &timer, NULL);
}

int main () 
{
	printf("in main, getting contexts...\n");
	getcontext(&foo_context);
	getcontext(&bar_context);

	printf("registering contexts...\n");
	foo_context.uc_link = &bar_context;
	foo_context.uc_stack.ss_sp = malloc(STACK_SIZE);
	foo_context.uc_stack.ss_size = STACK_SIZE;
	foo_context.uc_stack.ss_flags = 0;

	bar_context.uc_link = &foo_context;
	bar_context.uc_stack.ss_sp = malloc(STACK_SIZE);
	bar_context.uc_stack.ss_size = STACK_SIZE;
	bar_context.uc_stack.ss_flags = 0;

	printf("making contexts...\n");
	makecontext(&foo_context, (void *)&foo, 0);
	makecontext(&bar_context, (void *)&bar, 0);

	printf("creating timer...\n");
	setup_timer();

	printf("setting foo context...\n");
	setcontext(&foo_context);

	return 0;
}
