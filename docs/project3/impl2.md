# Project3 Milestone1 - Expand the maximum size of a file
이번 milestone은 xv6에서 지원하는 한 파일의 최대 용량을 늘리는 것이 목표다.

## Design
기존 xv6에서는 direct pointer와 single indirect pointer만 지원한다.
따라서 이때의 최대 파일 크기는 direct pointer에서 12블록, single indirect pointer에서 128블록으로 총 (12 + 128) * BSIZE(=512 bytes) = 71,680 bytes = 70KiB이다.

하지만 doubly pointer와 triple pointer를 도입한다면 최대 지원 가능 파일 크기는 다음과 같다.
direct pointer에서 10블록 (doubly indirect pointer와 triple indirect pointer를 위한 공간 마련 때문에 위의 경우보다 2개 줄었다.), single indirect pointer에서 128블록, double indirect pointer에서 128 * 128블록, triple indirect pointer에서 128 * 128 * 128 블록으로 총 (10 + 128 + 128 * 128 + 128 * 128 * 128) * BSIZE(=512 bytes) = 1,082,201,088 bytes = 약 1032 MiB.

## Implementation
### 0. Add constants for supporting doubly, triple indirect pointer
이번 milestone과 관련된 상수값들을 fs.h에 다음과 같이 정의했다.

```c++
#define NDIRECT 10
#define NINDIRECT (BSIZE / sizeof(uint))
#define NINDIRECT_D (NINDIRECT * (BSIZE / sizeof(uint)))
#define NINDIRECT_T (NINDIRECT_D * (BSIZE / sizeof(uint)))
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT_D + NINDIRECT_T)

enum FS_ADDR_TYPE {
  FS_ADDR_DIRECT = 0,
  FS_ADDR_SINGLE_INDIRECT = NDIRECT,
  FS_ADDR_DOUBLY_INDIRECT = NDIRECT + 1,
  FS_ADDR_TRIPLE_INDIRECT = NDIRECT + 2
};
```

이때 `FS_ADDR_TYPE`은 Inode에서 각 pointer의 시작 index를 의미한다.

### 1. Change `bmap` function
기존 `bmap` 함수에 구현된 single indirect pointer의 구현을 참고하여 `bn`이 doubly 혹은 triple indirection 범위에 포함되면 각 pointer의 정의에 맞게 address를 찾을 수 있도록 구현하였다.

*TODO: code duplication을 개선하도록 리팩터링 할 필요가 있다.*

### 2. Change `itrunc` function
기존 `itrunc` 함수의 구현을 참고해 doubly 혹은 triple indirection과 관련된 block들을 free할 수 있도록 수정하였다.

*TODO: code duplication을 개선하도록 리팩터링 할 필요가 있다.*

### 3. Edit rm program
milestone1의 영향으로 rm 프로그램에서 발생한 수정이 disk에 반영이 안되는 문제가 있었다.
따라서 rm 프로그램이 종료되기 전에 `sync` system call을 호출해 무조건 반영되도록 수정하여 문제를 해결했다.


## Self-Test
### 1. Create a big file.
512 bytes씩 36,864회 데이터를 쓰도록 하여 총 18MiB짜리 파일을 쓰도록 하였고, 다음과 같이 큰 크기의 파일을 정상적으로 쓸 수 있음을 확인했다.

```
$ ls
            (...)
file0          2 26 512
file1          2 27 512
file2          2 28 18874368
```