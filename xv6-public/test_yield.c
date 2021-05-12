#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char** argv)
{
  int i;
  int pid = fork();
  int tid = gettid();

  printf(1, "tid: %d\n", tid);

  if (pid < 0)
  {
    printf(1, "Fork failed.\n");
    exit();
  }

  for (i = 0; i < 10; ++i)
  {
    if (pid > 0)
    {
      write(1, "Parent\n", 8);
    }
    else if (pid == 0)
    {
      write(1, "Child\n", 7);
    }

    yield();
  }

  wait();

	exit();
}
