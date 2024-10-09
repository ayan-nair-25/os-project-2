#include "thread-worker.h"

void myballs()
{
  printf("balls\n");
}
int main()
{
  worker_t *threadnum;
  int res = worker_create(threadnum, NULL, myballs, NULL);
  printf("%d", res);
  printf("\n--Hello--\n");
  return 0;
}