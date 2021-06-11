# Project3 Milestone1 - Implement the sync system call
이번 milestone은 unix-like os에서 file write를 할 때 바로 HDD나 SSD에 저장하는 것이 아니라
메모리에 존재하는 buffer에 담아둔 후, 특정 상황에서 device에 buffer의 내용을 옮기는 것을 구현하는 것이 목표다.

## Design
xv6에서 file write를 할 때 file.c의 `filewrite` 함수가 호출되는데 해당 함수의 작동을 대략적으로 타나내면 다음과 같다.

```c++
filewrite(args) {
    ...
    begin_op()              // commit이 진행중인지 확인하고, 없다면 log.outstanding을 1 증가시킨다.
    ... write to BUFFER ... // data를 메모리 상의 buffer에 저장한다.
    end_op()                // 동작하고 있는 fs syscall이 없다면 commit을 호출한다.
    ...
}
```

이때, `commit` 함수는 buffer에 저장된 data를 장치에 저장하는 동작을 수행한다.

따라서 xv6에서 이미 buffer에 file write를 수행하는 것은 구현이 되어있으며, `end_op` 함수를 수정하면 file write가 수행 될 때마다 device에 저장되는 것을 막을 수 있다. 또한, sync syscall은 `commit` 함수를 호출하는 방식으로 구현할 수 있을 것이다.

## Implementation
### 1. Write on buffer cache
우선은 `end_op`에서 `commit` 함수의 호출을 분리하는 작업을 수행하였다.

기존 `end_op` 함수의 동작은 다음과 같다.
1. log.outstanding을 1 감소시켜 실행중인 fs syscall이 하나 줄었음을 표현한다.
2. 현재 수행중인 fs system call이 없다면 commit 한다.

위 design에서 2번 동작인 commit을 막기로 하였으므로 `end_op`에서 commit 관련된 부분을 전부 제거하고
별도의 함수 `commit_sync`로 분리하였다.

### 2. Make data durable by sync call
위 단계에서 sync syscall의 동작에 해당하는 부분을 `commit_sync` 함수로 분리하였다.
따라서 sync가 호출되면 `commit_sync`를 호출하도록 하여 해당 부분을 구현하였다.

### 3. Exceed log space
기존 xv6 코드에서는 `begin_op`에서 log space의 남은 공간이 부족하면 commit이 수행되어 공간이 확보될 때까지 sleep한다.
하지만 이번 프로젝트에서 수정된 부분 때문에 사용자가 `sync`를 호출하지 않는다면 정상적으로 작동하지 않게 된다.
그러므로 log space가 부족해진다면 강제로 `commit_sync`를 호출해 공간을 비워주도록 하였다. (더이상 sleep 하지 않는다)

## Self-Test
### 1. sync test
512 byte씩 30번 쓴 파일의 크기가 재부팅을 거치면 다음과 같이 변했다.

- sync 호출하지 않은 경우
```
before : 15,360 bytes
 after :  8,704 bytes
```

- close하기 전 sync를 호출한 경우
```
before : 15,360 bytes
 after : 15,360 bytes
```

따라서 sync를 호출하지 않으면 일부 데이터는 buffer에만 저장되어 재부팅하면 날아가고
sync를 호출했을 때만 재부팅해도 정보가 날아가지 않은 것으로 보아 이번 프로젝트의 주 목표를 달성한 것으로 파악할 수 있다.

### 2. stress test
```
0 before: 0 after: 1
1 before: 1 after: 2
2 before: 2 after: 3
3 before: 3 after: 4
4 before: 4 after: 5
5 before: 5 after: 6
6 before: 6 after: 7
7 before: 7 after: 8
8 before: 8 after: 9
9 before: 9 after: 10
10 before: 10 after: 11
11 before: 11 after: 12
12 before: 12 after: 13
13 before: 13 after: 14
14 before: 14 after: 15
15 before: 15 after: 16
16 before: 16 after: 17
17 before: 17 after: 18
18 before: 18 after: 19
19 before: 19 after: 20
20 before: 20 after: 21
21 before: 0 after: 1
22 before: 1 after: 2
23 before: 2 after: 3
24 before: 3 after: 4
25 before: 4 after: 5
26 before: 5 after: 6
27 before: 6 after: 7
28 before: 7 after: 8
29 before: 8 after: 9
```
위 사진은 512 byte씩 30번 같은 파일에 write 하는 동작을 수행한 결과이다.
20, 21 사이에 log의 개수가 0으로 줄어든 것으로 보아 log space가 가득 차면 commit 하는 기능이 동작했음을 알 수 있다.
