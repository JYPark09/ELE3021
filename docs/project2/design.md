# Design for Project2: Light-weight Process

## 1. Process/Thread

### a. Process
Process란 실행중인 program이다. process는 program counter, stack pointer, general registers 등의 CPU의 state와, 메모리 address space를 가진다. 각 process는 별도의 address space를 가지며, 따라서 한 process는 다른 process의 데이터에 접근할 수 없다.

Process에서의 context switch는 process의 모든 구성요소를 변경한다. 따라서 address space도 변경하여 TLB(Table Lookaside Buffer) miss가 증가할 수 있다.

### b. Thread
Thread는 process의 실행 단위이다. 따라서 thread는 한 process의 속하며, 같은 process에 속한 thread는 모두 같은 address space를 공유한다. 다만, 해당 thread를 위해 별도의 stack을 할당한다.

같은 process에 속한 thread간의 context switch는 address space를 공유한다는 속성에 기반하여, stack 영역의 변경만 수행해주면 된다. 따라서 TLB miss가 덜 발생하며, 결과적으로 process간의 context switch 시 보다 cost가 덜 발생한다.


## 2. POSIX Thread

### a. Overview
POSIX Thread(PThread)는 모든 unix 계열의 POSIX 시스템에서 multi-threading을 사용하기 위해 쓰이는 라이브러리다. 다양한 프로그래밍 언어를 지원한다.

특히 C/C++에서 PThread를 사용하려면 pthread.h를 include하고, libpthread를 link 해야 한다.

### b. Thread Creation
```c++
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg);
```
#### Description
thread를 생성하는 함수다.

#### Arguments
- thread : 생성된 thread의 식별자이다. (pthread_t는 내부적으로 unsigned long 타입이다.)
- attr : thread의 특성을 지정하기 위해 사용한다. (0을 넘기면 기본값을 사용한다.)
- start_routine : 생성된 thread가 실행할 함수이다.
- arg : start_routine의 인자이다.

### c. Thread Join
```c++
int pthread_join(pthread_t thread, void** ret_val);
```

#### Description
thread가 종료할 때까지 해당 thread를 멈추는 함수다.

#### Arguments
- thread : 기다릴 thread의 식별자이다.
- ret_val : thread의 반환값을 받아오는 포인터다. (0을 넘기면 받지 않을 수 있다.)

### d. Thread Exit
```c++
void pthread_exit(void *ret_val);
```

#### Description
현재 thread를 종료하는 함수다.

#### Arguments
- ret_val : 반환값을 설정한다.


## 3. Design Basic LWP Operations for xv6

### a. Overview
이번 project는 xv6에 thread란 개념을 추가하는 것이 목표다.
디자인의 일관성을 위해 기존의 proc 조차 main thread로 분리해내는 것으로 시작한다.

우선 `proc`의 식별자가 pid인 것처럼, `thread`의 식별자를 tid라 하고 그 타입은 int형 `thread_t`로 한다.
`proc` 구조체에서 실행과 관련된 field는 `tf`, `context`, `chan`이 있다. 따라서 이를 `thread`란 구조체로 분리해내야 한다. `thread` 구조체의 prototype은 다음과 같다.

```c++
typedef int thread_t;

struct thread {
  struct proc *p;             // Process that this thread belongs

  thread_t tid;               // Thread ID

  struct trapframe *tf;       // Trap frame for current syscall
  struct context *context;    // swtch() here to run process
  void *chan;                 // If non-zero, sleeping on chan

  void* retval;               // Return value of this thread
};
```

`proc`에선 실행과 관련된 정보가 빠짐과 동시에 thread를 저장할 수단이 필요하다. 따라서 수정된 `proc` 구조체의 prototype은 다음과 같다.

```c++
struct proc {
  uint sz;                    // Size of process memory (bytes)
  pde_t *pgdir;               // Page table
  char *kstack;               // Bottom of kernel stack for this process
  enum procstate state;       // Process state
  int pid;                    // Process ID
  struct proc *parent;        // Parent process
  int killed;                 // If non-zero, have been killed
  struct file *ofile[NOFILE]; // Open files
  struct inode *cwd;          // Current directory
  char name[16];              // Process name (debugging)

  // informations for scheduling
  enum schedule_policy schedule_type;
  union {
    struct mlfq_info mlfq;
    struct stride_info stride;
  };

  // informations for threads
  thread_list_t threads;
};
```

이제 `allocproc`에서 process를 생성한다면 threads에 main thread를 만들어 넣는 방식으로 구현하여 thread 개념을 디자인의 일관성을 해치지 않고 도입 할 수 있다.

### b. 1st Step of Second Milestone
이 단계에선 thread의 생애 주기를 관리하는 여러 system call을 구현하는 것이 목표다.

#### 1) Create
```c++
int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg);
```

`allocproc` 함수를 참고하여 구현한다.

이때 생성된 thread의 trapframe의 eip에 start_routine을 넣어주고, trapframe의 esp 위치에 arg를 넣어 system call 호출 끝에 user mode로 돌아갈 때 start_routine이 실행되도록 한다.

#### 2) Exit
```c++
void thread_exit(void *retval);
```

`exit` 함수를 참고하여 구현한다.

우선 `retval`의 내용을 thread에 저장한다. 그 이후에 thread의 `state`를 `ZOMBIE`로 변경하고, thread의 실질적인 정리는 `thread_join`에서 수행하도록 한다.

#### 3) Join
```c++
int thread_join(thread_t thread, void **retval);
```

`wait` 함수를 참고하여 구현한다.

thread를 정리할 때 thread의 `retval`을 `retval`에 저장한다.


### c. 2nd Step of Second Milestone

#### 1) Interaction with system calls in xv6

- Exit system call  
해당 process의 thread state를 전부 `ZOMBIE`로 바꾼다.

- Fork system call  
main thread를 미리 생성된 `proc` instance에 초기화한다.

- Kill system call  
해당 process의 thread state를 전부 `ZOMBIE`로 바꾼다.

- Sleep system call  
`sleep` 함수에서 `p->chan`이 아닌 해당 thread의 `chan` field에 값을 쓰도록 변경한다.

#### 2) Interaction with the schedulers
우선 mlfq에만 존재하던 `execution_ticks`를 `proc`으로 뺀다. 또한, `sched` 함수를 수정하여 만약 `execution_ticks`가 정해진 tick만큼 수행되지 않았다면 `scheduler`로 context switch하지 않고 `sched` 함수에서 바로 LWP group 내의 thread로 switch 한다.