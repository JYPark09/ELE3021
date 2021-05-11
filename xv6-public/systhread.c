#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_thread_create(void)
{
  thread_t *thread;
  void *(*start_routine)(void*);
  void* arg;

  if (argint(0, (int*)&thread) < 0)
    return -1;

  if (argint(1, (int*)&start_routine) < 0)
    return -1;

  if (argint(2, (int*)&arg) < 0)
    return -1;  

  return thread_create(thread, start_routine, arg);
}

int
sys_thread_exit(void)
{
  void* retval;

  if (argint(0, (int*)&retval) < 0)
    return -1;

  thread_exit(retval);

  return 0;
}

int
sys_thread_join(void)
{
  thread_t thread;
  void **retval;

  if (argint(0, &thread) < 0)
    return -1;

  if (argint(1, (int*)&retval) < 0)
    return -1;

  return thread_join(thread, retval);
}
