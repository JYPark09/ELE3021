#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define NUM_MLFQ_LEVEL 3
#define MLFQ_CPU_SHARE 20
#define MLFQ_TIME_CONST_SHIFT 10
#define MLFQ_TIME_CONST_MASK ((1 << MLFQ_TIME_CONST_SHIFT) - 1)
#define MLFQ_TIME_QUANTUM_CONST \
  ((( 5 & MLFQ_TIME_CONST_MASK) << (MLFQ_TIME_CONST_SHIFT * 0)) |\
   ((10 & MLFQ_TIME_CONST_MASK) << (MLFQ_TIME_CONST_SHIFT * 1)) |\
   ((20 & MLFQ_TIME_CONST_MASK) << (MLFQ_TIME_CONST_SHIFT * 2)))
#define MLFQ_TIME_QUANTUM(level) ((MLFQ_TIME_QUANTUM_CONST >> (MLFQ_TIME_CONST_SHIFT * (level))) & MLFQ_TIME_CONST_MASK)
#define MLFQ_BOOSTING_INTERVAL 200

#define STRIDE_TIME_QUANTUM 5
#define STRIDE_TOTAL_TICKETS 100

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

typedef struct proc_queue
{
  struct proc *data[NPROC];
  int front, rear;
  int size;
} proc_queue_t;

static int runnable_proc(struct proc *p)
{
  struct thread *t;

  for (t = p->threads; t < &p->threads[NTHREAD]; ++t)
    if (t->state == RUNNABLE)
      return 1;

  return 0;
}

struct
{
  proc_queue_t queue[NUM_MLFQ_LEVEL]; // multi-level queue
  int executed_ticks;                 // the number of ticks to which MFLQ scheduler worked

  double pass;
} mlfq_mgr;

void mlfq_init()
{
  int lev;
  proc_queue_t *queue;

  for (lev = 0; lev < NUM_MLFQ_LEVEL; ++lev)
  {
    queue = &mlfq_mgr.queue[lev];

    memset(queue->data, 0, sizeof(struct proc *) * NPROC);

    queue->front = 1;
    queue->rear = 0;
    queue->size = 0;
  }

  mlfq_mgr.executed_ticks = 0;
  mlfq_mgr.pass = 0;
}

//! insert proc at mlfq queue
//! \param lev level of queue to insert
//! \param p process to insert
//! \return 0 if success else -1
int mlfq_enqueue(int lev, struct proc *p)
{
  proc_queue_t *const queue = &mlfq_mgr.queue[lev];

  // if queue is full, return failure
  if (queue->size == NPROC)
    return -1;

  queue->rear = (queue->rear + 1) % NPROC;
  queue->data[queue->rear] = p;
  ++queue->size;

  p->mlfq.level = lev;

  return 0;
}

//! dequeue head proc from mlfq queue
//! \param lev level of queue to dequeue
//! \return level of dequeued proc if success else -1
int mlfq_dequeue(int lev, struct proc **ret)
{
  proc_queue_t *const queue = &mlfq_mgr.queue[lev];
  struct proc *p;

  // if queue is empty, return failure
  if (queue->size == 0)
    return -1;

  p = queue->data[queue->front];
  queue->data[queue->front] = 0;

  queue->front = (queue->front + 1) % NPROC;
  --queue->size;

  p->mlfq.level = -1;

  if (ret != 0)
    *ret = p;

  return lev;
}

void mlfq_remove(struct proc *p)
{
  proc_queue_t *const queue = &mlfq_mgr.queue[p->mlfq.level];

  int i;

  for (i = 0; i < NPROC; ++i)
  {
    if (queue->data[i] == p)
    {
      break;
    }
  }

  while (i != queue->front)
  {
    queue->data[i] = queue->data[i ? i - 1 : NPROC - 1];

    i = i ? i - 1 : NPROC - 1;
  }
  queue->data[queue->front] = 0;

  queue->front = (queue->front + 1) % NPROC;
  --queue->size;
}

//! returns front proc
//! \param lev level of queue
struct proc *mlfq_front(int lev)
{
  proc_queue_t *const queue = &mlfq_mgr.queue[lev];

  return queue->data[queue->front];
}

// note: 2021-04-13
// we can't use min heap.
// because process that has min pass value is not runnable.
struct
{
  struct proc *list[NPROC];
  int size;

  int share;
  double pass;
} stride_mgr;

void stride_init()
{
  memset(stride_mgr.list, 0, sizeof(struct proc *) * NPROC);
  stride_mgr.size = 0;

  stride_mgr.share = 0;
  stride_mgr.pass = 0;
}

//! insert process to stride scheduler
//! \param p process to insert
//! \return 0 if success else -1
int stride_insert(struct proc *p)
{
  int i, j;

  // if list is full, return failure
  if (stride_mgr.size == NPROC)
    return -1;

  for (i = 0; i < stride_mgr.size; ++i)
  {
    if (stride_mgr.list[i]->stride.pass > p->stride.pass)
      break;
  }

  for (j = stride_mgr.size; j > i; --j)
  {
    stride_mgr.list[j] = stride_mgr.list[j - 1];
  }

  stride_mgr.share += p->stride.share;
  stride_mgr.list[i] = p;
  ++stride_mgr.size;

  return 0;
}

void stride_remove_idx(int i)
{
  stride_mgr.share -= stride_mgr.list[i]->stride.share;

  for (; i < stride_mgr.size - 1; ++i)
    stride_mgr.list[i] = stride_mgr.list[i + 1];

  stride_mgr.list[--stride_mgr.size] = 0;
}

int stride_remove(struct proc *p)
{
  int i;

  for (i = 0; i < stride_mgr.size; ++i)
  {
    if (stride_mgr.list[i] == p)
    {
      stride_remove_idx(i);

      return 0;
    }
  }

  return -1;
}

//! pop minimal pass process from stride scheduler
//! \param ret popped process
//! \return 0 if success else -1
int stride_pop(struct proc **ret)
{
  int i;

  for (i = 0; i < stride_mgr.size; ++i)
    if (runnable_proc(stride_mgr.list[i]))
      goto found;

  // there is no suitable process to run
  return -1;

found:
  *ret = stride_mgr.list[i];
  stride_remove_idx(i);

  return 0;
}

//! returns minimal process
struct proc *stride_min_proc()
{
  int i;

  for (i = 0; i < stride_mgr.size; ++i)
    if (stride_mgr.list[i]->state == RUNNABLE)
      return stride_mgr.list[i];

  return 0;
}

void print_stride_info()
{
  int i;

  cprintf("[stride info]\n");
  cprintf("list size: %d\n", stride_mgr.size);

  for (i = 0; i < NPROC; ++i)
  {
    cprintf("%d ", stride_mgr.list[i] ? stride_mgr.list[i]->pid : -1);
  }

  cprintf("\n");
}

void print_mlfq_info()
{
  int lev, i;

  cprintf("[mlfq info]\n");
  for (lev = 0; lev < NUM_MLFQ_LEVEL; ++lev)
  {
    cprintf("<level %d>\n", lev);
    cprintf("size: %d\n", mlfq_mgr.queue[lev].size);

    for (i = 0; i < mlfq_mgr.queue[lev].size; ++i)
      cprintf("%d ", mlfq_mgr.queue[lev].data[(mlfq_mgr.queue[lev].front + i) % NPROC] ? mlfq_mgr.queue[lev].data[(mlfq_mgr.queue[lev].front + i) % NPROC]->pid : -1);

    cprintf("\n");
  }
}

//! returns the number of issued tickets
//! ptable locking is required before calling.
int get_stride_total_tickets()
{
  // int i;
  // int ticket = MLFQ_CPU_SHARE;

  // for (i = 0; i < NPROC; ++i)
  // {
  //   if (ptable.proc[i].state != UNUSED && ptable.proc[i].schedule_type == STRIDE)
  //   {
  //     ticket += ptable.proc[i].stride.share;
  //   }
  // }

  return MLFQ_CPU_SHARE + stride_mgr.share;
}

int set_cpu_share(struct proc *p, int share)
{
  int tickets;

  acquire(&ptable.lock);

  tickets = get_stride_total_tickets();

  // if system has not enough tickets, return failure
  if (share <= 0 || tickets + share > STRIDE_TOTAL_TICKETS)
  {
    release(&ptable.lock);
    return -1;
  }

  mlfq_remove(p);

  p->schedule_type = STRIDE;
  p->stride.share = share;
  p->stride.pass = stride_mgr.pass;

  stride_insert(p);

  release(&ptable.lock);

  return 0;
}

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");

  mlfq_init();
  stride_init();
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  MAIN(p).state = EMBRYO;
  MAIN(p).tid = nexttid++;

  // scheduling init
  p->schedule_type = MLFQ;
  if (mlfq_enqueue(0, p) != 0)
  {
    release(&ptable.lock);
    return 0;
  }

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((MAIN(p).kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    MAIN(p).state = UNUSED;
    return 0;
  }
  sp = MAIN(p).kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *MAIN(p).tf;
  MAIN(p).tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *MAIN(p).context;
  MAIN(p).context = (struct context *)sp;
  memset(MAIN(p).context, 0, sizeof *MAIN(p).context);
  MAIN(p).context->eip = (uint)forkret;

  p->curtid = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(MAIN(p).tf, 0, sizeof(*MAIN(p).tf));
  MAIN(p).tf->cs = (SEG_UCODE << 3) | DPL_USER;
  MAIN(p).tf->ds = (SEG_UDATA << 3) | DPL_USER;
  MAIN(p).tf->es = MAIN(p).tf->ds;
  MAIN(p).tf->ss = MAIN(p).tf->ds;
  MAIN(p).tf->eflags = FL_IF;
  MAIN(p).tf->esp = PGSIZE;
  MAIN(p).tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  MAIN(p).state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(MAIN(np).kstack);
    MAIN(np).kstack = 0;
    np->state = UNUSED;
    MAIN(np).state = UNUSED;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *MAIN(np).tf = *RTHREAD(curproc).tf;

  // Clear %eax so that fork returns 0 in the child.
  MAIN(np).tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  
  acquire(&ptable.lock);

  np->state = RUNNABLE;
  MAIN(np).state = RUNNABLE;

  for (i = 0; i < NTHREAD; ++i)
    np->ustack_pool[i] = curproc->ustack_pool[i];
  np->ustack_pool[0] = curproc->ustack_pool[curproc->curtid];
  np->ustack_pool[curproc->curtid] = curproc->ustack_pool[0];

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  struct thread *t;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  for (t = curproc->threads; t < &curproc->threads[NTHREAD]; ++t)
  {
    if (t->state != UNUSED)
      t->state = ZOMBIE;
  }

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  struct thread *t;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        if (p->schedule_type == MLFQ)
        {
          mlfq_remove(p);
        }
        else if (p->schedule_type == STRIDE)
        {
          stride_remove(p);

          // to prevent overflow, if there is no stride process
          // clear the pass values.
          if (stride_mgr.size == 0)
          {
            mlfq_mgr.pass = 0;
            stride_mgr.pass = 0;
          }
        }

        // init thread data
        for (t = p->threads; t < &p->threads[NTHREAD]; ++t)
        {
          // clean up memory pool
          p->ustack_pool[t - p->threads] = 0;

          if (t->kstack != 0)
            kfree(t->kstack);

          t->kstack = 0;
          t->tid = 0;
          t->state = UNUSED;
        }

        // Found one.
        pid = p->pid;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        // init schedule data
        p->schedule_type = MLFQ;
        p->stride.pass = 0;
        p->stride.share = 0;

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

// dbg 0 - from mlfq_choose or stride_choose
// dbg 1 - else
void incr_ticks(struct proc *p, int dbg)
{
  // if (p->schedule_type == MLFQ)
  //   cprintf("%d pid: %d tid: %d st: %d lv: %d t: %d(%d) strs: %d mlfq_p: %d strd_p: %d\n", dbg, p->pid, RTHREAD(p).tid, p->schedule_type, p->mlfq.level, p->executed_ticks, MLFQ_TIME_QUANTUM(p->mlfq.level), stride_mgr.size, (int)mlfq_mgr.pass, (int)stride_mgr.pass);
  // else if (p->schedule_type == STRIDE)
  //   cprintf("%d pid: %d tid: %d st: %d mlfq_p: %d pass: %d\n", dbg, p->pid, RTHREAD(p).tid, p->schedule_type, (int)mlfq_mgr.pass, (int)p->stride.pass);
  // else
  //   cprintf("%d pid: %d tid: %d st: %d\n", dbg, p->pid, RTHREAD(p).tid, p->schedule_type);

  ++p->executed_ticks;

  if (p->schedule_type == MLFQ)
  {
    ++mlfq_mgr.executed_ticks;

    if (dbg != 0)
      mlfq_mgr.pass += (stride_mgr.size > 0) * STRIDE_TOTAL_TICKETS / (double)MLFQ_CPU_SHARE;
  }
  else if (p->schedule_type == STRIDE)
  {
    if (dbg != 0)
      stride_mgr.pass += (stride_mgr.size > 0) * STRIDE_TOTAL_TICKETS / (double)stride_mgr.share;
  }
}

struct proc *
mlfq_choose()
{
  static const int TIME_ALLOTMENT[] = {20, 40};

  struct proc *ret;
  int lev = 0, size, i;

  mlfq_mgr.pass += (stride_mgr.size > 0) * STRIDE_TOTAL_TICKETS / (double)MLFQ_CPU_SHARE;

  while (1)
  {
    for (; lev < NUM_MLFQ_LEVEL; ++lev)
    {
      if (mlfq_mgr.queue[lev].size > 0)
        break;
    }

    // if there is no process in the mlfq
    if (lev == NUM_MLFQ_LEVEL)
      return 0;

    size = mlfq_mgr.queue[lev].size;
    for (i = 0; i < size; ++i)
    {
      ret = mlfq_front(lev);

      if (!runnable_proc(ret))
      {
        mlfq_dequeue(lev, 0);
        mlfq_enqueue(lev, ret);
      }
      else
      {
        goto found;
      }
    }

    // queue has no runnable process
    // then find candidate at next lower queue
    ++lev;
  }

found:
  incr_ticks(ret, 0);
  
  if (lev < NUM_MLFQ_LEVEL - 1 && ret->executed_ticks >= TIME_ALLOTMENT[lev])
  {
    mlfq_dequeue(lev, 0);
    mlfq_enqueue(lev + 1, ret);

    ret->executed_ticks = 0;
  }
  else if (ret->executed_ticks % MLFQ_TIME_QUANTUM(lev) == 0)
  {
    mlfq_dequeue(lev, 0);
    mlfq_enqueue(lev, ret);

    if (lev == NUM_MLFQ_LEVEL - 1)
      ret->executed_ticks = 0;
  }

  return ret;
}

void mlfq_boosting()
{
  struct proc *p;
  int lev;
  for (lev = 1; lev < NUM_MLFQ_LEVEL; ++lev)
  {
    while (mlfq_mgr.queue[lev].size)
    {
      mlfq_dequeue(lev, &p);
      mlfq_enqueue(0, p);

      p->executed_ticks = 0;
    }
  }

  mlfq_mgr.executed_ticks = 0;
}

struct proc *
stride_choose()
{
  struct proc *p;

  stride_mgr.pass += (stride_mgr.size > 0) * STRIDE_TOTAL_TICKETS / (double)stride_mgr.share;

  if (stride_pop(&p) != 0)
    return 0;

  incr_ticks(p, 0);

  p->stride.pass += STRIDE_TOTAL_TICKETS / (double)p->stride.share;

  if (stride_insert(p) != 0)
    panic("cannot insert process at stride");

  return p;
}

//! choose next process
//! ptable must be locked before calling.
struct proc *
schedule_choose()
{
  struct proc *p;
  struct thread *t;
  int start = 0;

  if (stride_mgr.pass >= mlfq_mgr.pass)
    p = mlfq_choose();
  else
    p = stride_choose();

  if (p != 0)
  {
    for (t = &RTHREAD(p); ; ++t)
    {
      if (t == &p->threads[NTHREAD])
        t = &p->threads[0];

      if (t->state == RUNNABLE)
        break;

      if (start && t == &RTHREAD(p))
        panic("invalid logic");
      start = 1;
    }

    p->curtid = t - p->threads;
  }

  return p;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    p = schedule_choose();
    t = p ? &RTHREAD(p) : 0;

    if (p != 0)
    {
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      t->state = RUNNING;

      // after intoducing thread concept,
      // state of process can be only UNUSED, EMBRYO, or RUNNABLE
      // p->state = RUNNING;

      swtch(&(c->scheduler), t->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

    if (mlfq_mgr.executed_ticks >= MLFQ_BOOSTING_INTERVAL)
    {
      mlfq_boosting();
    }

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();
  struct thread *t = &RTHREAD(p);

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (t->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&t->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// choose next thread
static void shift_thread(struct proc *p)
{
  int intena;
  struct thread *t;
  struct thread *curthread = &RTHREAD(p);

  acquire(&ptable.lock);

  for (t = &p->threads[(p->curtid + 1) % NTHREAD]; ; ++t)
  {
    if (t == &p->threads[NTHREAD])
      t = p->threads;

    if (t == curthread)
    {
      if (t->state == RUNNING)
      {
        incr_ticks(p, 1);

        release(&ptable.lock);
        return;
      }

      sched();
      panic("zombie thread");
    }
      
    if (t->state == RUNNABLE)
      break;
  }

  curthread->state = RUNNABLE;
  t->state = RUNNING;
  p->curtid = t - p->threads;

  incr_ticks(p, 1);

  // switchuvm for thread
  pushcli();
  mycpu()->ts.esp0 = (uint)t->kstack + KSTACKSIZE;
  popcli();

  // sched for thread
  intena = mycpu()->intena;
  swtch(&curthread->context, t->context);
  mycpu()->intena = intena;

  release(&ptable.lock);
}

// Give up the CPU for one scheduling round.
static void shift_process(struct proc *p)
{
  acquire(&ptable.lock); //DOC: yieldlock
  p->state = RUNNABLE;
  RTHREAD(p).state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

void yield(void)
{
  struct proc *p = myproc();

  if (p->state != RUNNABLE)
  {
    panic("why you call me...?");
  }

  if ((p->schedule_type == MLFQ && (((1 + p->executed_ticks) % MLFQ_TIME_QUANTUM(p->mlfq.level)) != 0))
    || (p->schedule_type == STRIDE && (p->executed_ticks < STRIDE_TIME_QUANTUM)))
  {
    shift_thread(p);
  }
  else
  {
    shift_process(p);
  }
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct thread *t = &RTHREAD(p);

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  t->chan = chan;
  t->state = SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  struct thread *t;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == RUNNABLE)
      for (t = p->threads; t < &p->threads[NTHREAD]; ++t)
        if (t->state == SLEEPING && t->chan == chan)
          t->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;
  struct thread *t;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake threads from sleep if necessary.
      for (t = p->threads; t < &p->threads[NTHREAD]; ++t)
        if (t->state == SLEEPING)
          t->state = RUNNABLE;

      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)RTHREAD(p).context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


/****************************************
 *  Thread (Light Weight Process)       *
 ****************************************/
int thread_create(thread_t *thread, void *(*start_routine)(void*), void *arg)
{
  struct thread *nt;
  struct proc *curproc = myproc();
  char *sp;
  uint sz;
  int tidx;

  acquire(&ptable.lock);

  for (nt = curproc->threads; nt < &curproc->threads[NTHREAD]; ++nt)
    if (nt->state == UNUSED)
      goto found;

  cprintf("cannot found unused thread\n");
  release(&ptable.lock);

  return -1;

found:
  tidx = nt - curproc->threads;
  nt->state = EMBRYO;
  nt->tid = nexttid++;

  // Allocate kernel stack.
  if ((nt->kstack = kalloc()) == 0)
  {
    cprintf("cannot alloc kernel stack\n");
    goto bad;
  }
  sp = nt->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *nt->tf;
  nt->tf = (struct trapframe *)sp;
  *nt->tf = *RTHREAD(curproc).tf;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *nt->context;
  nt->context = (struct context *) sp;
  memset(nt->context, 0, sizeof *nt->context);
  nt->context->eip = (uint)forkret;

  // Allocate user stack.
  if (curproc->ustack_pool[tidx] == 0)
  {
    sz = PGROUNDUP(curproc->sz);
    if ((sz = allocuvm(curproc->pgdir, sz, sz + PGSIZE)) == 0)
    {
      cprintf("cannot alloc user stack\n");
      goto bad;
    }

    curproc->ustack_pool[tidx] = sz;
    curproc->sz = sz;
  }
  sp = (char *)curproc->ustack_pool[tidx];

  // Push argument, prepare rest of stack in ustack.
  sp -= 4;
  *(uint *)sp = (uint)arg;

  // fake return PC
  sp -= 4;
  *(uint *)sp = 0xffffffff;

  // Commit to the user image.
  nt->tf->eip = (uint)start_routine;
  nt->tf->esp = (uint)sp;

  *thread = nt->tid;

  nt->state = RUNNABLE;

  release(&ptable.lock);

  return 0;

bad:
  nt->kstack = 0;
  nt->tid = 0;
  nt->state = UNUSED;

  release(&ptable.lock);

  return -1;
}

void thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  struct thread *curthread = &RTHREAD(curproc);

  acquire(&ptable.lock);

  wakeup1((void*)curthread->tid);

  curthread->retval = retval;
  curthread->state = ZOMBIE;

  sched();
  panic("zombie thread exit");
}

int thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  struct thread *t;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; ++p)
    if (p->state == RUNNABLE)
      for (t = p->threads; t < &p->threads[NTHREAD]; ++t)
        if (t->state != UNUSED && t->tid == thread)
          goto found;

  release(&ptable.lock);

  return -1;

found:
  if (t->state != ZOMBIE)
  {
    sleep((void*)thread, &ptable.lock);
  }

  if (retval != 0)
    *retval = t->retval;

  // clean up thread
  kfree(t->kstack);
  t->kstack = 0;
  t->retval = 0;
  t->tid = 0;
  t->state = UNUSED;

  release(&ptable.lock);

  return 0;
}
