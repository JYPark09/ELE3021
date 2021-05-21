# Implementation for Project2: Light Weight Process

## First Step: Basic LWP Operations
첫 번째 step에서는 xv6에 *a single execution flow*의 abstraction인 **thread concept**을 도입하는 것이 목표다. 따라서 process control block을 수정하고 thread 관련 함수/자료형을 선언하는 작업을 수행하였다.

### 0. Create TCB(Thread Control Block)
#### a. execution flow 분리
우선적으로 기존 `proc` 구조체에서 execution flow와 관련된 field를 분리하여 `thread` 구조체를 만들었다.
분리된 field는 `kstack`, `tf`, `context`, `chan`이 있다.

#### b. execution status field
`state` field는 `proc`과 `thread` 모두 가지고 있다. 이때 `proc`의 `state` field는 `EMBRYO`, `UNUSED`, `RUNNABLE` 세 가지 값만 가질 수 있으며, 오로지 ptable에서 해당 `proc` instance가 사용중인지 아닌지만 판별하며 실행중인지(RUNNING), 잠자는지(SLEEPING) 여부는 오로지 `thread`의 `state`만 가질 수 있도록 하였다.

#### c. thread table
`thread` instance는 `proc`의 `threads` 배열에 저장되도록 하였다. 특별히 배열의 0번 칸에는 main(master) thread가 위치한다. 가장 최근 실행된 thread의 index는 `proc`의 `curtid` field에 저장한다.

### 1. `thread_create` function
`exec` 함수를 참고하여 구현하였다. 기본적은 틀은 `exec`와 거의 유사하게 하되 `elf.entry`를 `start_routine`으로 바꾸었다. 또한 `kstack`을 새로 할당해주어야 하기 때문에 `kalloc`을 통해 새 thread의 `kstack`을 할당해주었다.

#### **memory issue about user stack**
제공된 test code의 stress test에서 free page가 부족해 결국 kernel stack조차 할당을 못해주어 터지는 issue가 있었다. kernel stack은 thread의 사멸과 동시에 `kfree`를 통해 메모리를 해제해줄 수 있지만 user stack은 그럴 수 없다. 만약 강제적으로 한다면 address space가 깨져 exception이 발생하는 문제가 생긴다. 따라서 `wait`함수에서 할당받은 모든 user stack을 해제할 때까지 free하지 못하는 문제가 발생한다.

이러한 issue를 해결하기 위해 `proc` 구조체에 최대 thread 개수에 해당하는 `ustack_pool`을 도입하여 새 thread를 생성할 때마다 pool에서 ustack에 해당하는 값을 가져오도록 하였다. 만약 `ustack_pool`에 할당받은 게 없다면 할당하고, 이미 있다면 이전에 할당받은 값을 이용해 thread의 esp를 초기화 해주는 방식으로 구현하였다.

### 2. `thread_exit` function
`exit` 함수를 참고하여 구현하였다. `thread`의 `retval`를 설정해주고 `state`를 `ZOMBIE`로 설정하고 process를 깨운다. 다만 `retval`의 보존을 위해 thread의 정리는 `thread_exit`에서 수행하지 않는다.

### 3. `thread_join` function
`wait` 함수를 참고하여 구현하였다. 만약 아직 `thread_exit`이 호출되지 않았다면 종료하길 기다려야하는 thread의 `tid`를 이용해 `sleep` 함수를 호출하여 잠든다. 잠에서 깨어나면(혹은 이미 `thread_exit`이 호출되었다면) 해당 tcb를 초기화 한다.


## Second Step: Interaction with other services in xv6
두 번째 step에서는 xv6의 여러 system call과 project1에서 구현한 mlfq+stride scheduler와 thread concept을 호환시키는 것이 목표다. 따라서 system call과 관련된 race를 해결하도록 lock으로 보호하는 작업과 TCB 도입에 따른 수정을 하였으며, scheduler를 위해 `yield` 함수를 수정하는 작업을 하였다.

### 0. Edit system call
#### a. `fork` system call
`fork`의 경우 `allocproc`을 통해 새로운 process를 만들기 때문에 TCB를 별도로 초기화 해줄 필요는 없지만 `fork`를 호출한 thread의 정보만은 새로운 process의 main thread에 넣어야 한다. 또한 address space를 전부 복사하기 때문에 ustack pool도 복사해주어야 한다.

따라서 우선 `ustack_pool`을 전부 복사한 뒤 `proc`의 `ustack_pool[0]`과 `threads[0]`를 `curproc`의 `curtid`에 해당하는 값으로 변경하고 `ustack_pool[curproc->curtid]`, `threads[curproc->curtid]`는 `curproc->ustack_pool[0]`과 `curproc->threads[0]`으로 각각 교체하였다.

#### b. `exec` system call
`fork`의 경우와는 다르게 새로운 process를 생성하지 않고 실행한 process를 사용하는 것이므로 우선적으로 `threads[0]`는 `threads[curproc->curtid]`로 바꾸고 그 이외의 값들은 0으로 초기화 해주었다. ustack_pool의 경우에는 address space가 완전히 변하였으므로 `sz`값을 `ustack_pool[0]`에 넣어주었다. 역시 그 이외의 값은 0으로 초기화 해주었다.

#### c. `growproc` system call (sbrk)
여러 thread에서 동시에 `growproc`을 호출하면 race가 발생하기 때문에 ptable.lock을 이용해 growproc 전체를 보호해주는 방식으로 race를 방지하였다.

### 1. Scheduler
#### a. execution ticks & time quantum
stride scheduler에서도 time quantum을 사용하게 되었으므로 기존엔 MLFQ에서만 존재했던 `executed_ticks` field를 `proc` 구조체로 빼내었다.

또한, time quantum을 `proc.c`의 여러 곳에서 사용해야하므로 매크로 상수/함수로 분리해내었다. 기존엔 변수에 time quantum을 저장하였지만 bit 연산을 활용하여 저장/사용 하도록 하였다. 관련 코드는 다음과 같다.

```c++
#define MLFQ_TIME_CONST_SHIFT 10
#define MLFQ_TIME_CONST_MASK ((1 << MLFQ_TIME_CONST_SHIFT) - 1)
#define MLFQ_TIME_QUANTUM_CONST \
  ((( 5 & MLFQ_TIME_CONST_MASK) << (MLFQ_TIME_CONST_SHIFT * 0)) |\
   ((10 & MLFQ_TIME_CONST_MASK) << (MLFQ_TIME_CONST_SHIFT * 1)) |\
   ((20 & MLFQ_TIME_CONST_MASK) << (MLFQ_TIME_CONST_SHIFT * 2)))
#define MLFQ_TIME_QUANTUM(level) ((MLFQ_TIME_QUANTUM_CONST >> (MLFQ_TIME_CONST_SHIFT * (level))) & MLFQ_TIME_CONST_MASK)
```

아래와 같은 구조로 time quantum이 저장된다.
```
0000 0001 0100 0000 0010 1000 0000 0101
  └─ level2 ─┘ └─ level1 ─┘└─ level0 ─┘
```

#### b. edit `yield` function
`trap` 함수에서 timer interrupt가 발생할 때 `yield` 함수를 호출한다. 하지만 기존의 `yield` 함수는 `sched` 함수를 호출해 scheduler로 context switch를 발생시킨다. 따라서 `yield` 함수에서 `sched` 함수를 호출하지 않고 같은 lwp group에 속한 다른 thread로 switch할 수 있도록 수정해야 하였다.

처음에는 thread간의 context switch 또는 process간의 context switch가 필요한지 여부를 검사하고 thread간의 context switch를 먼저 시도한 뒤 실패시에 `sched` 함수를 호출하도록 하였으나 double lock acquire 등의 문제가 발생하여 단순하게 구현하기 위해 다음과 같이 `yield` 함수를 수정하였다.

```c++
if (thread context swtch is needed)
    shift_thread();
else
    shift_process();
```

위 코드에서 `shift_process`는 기존의 `yield` 함수와 동일하다.
