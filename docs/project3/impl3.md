# Project3 Milestone3 - Implement the pread, pwrite system call
이번 milestone은 `pwrite`와 `pread` system call을 구현하는 것이 목표다.

## Design & Implementation
`pwrite`와 `pread`는 각각 파일의 offset을 바꾸지 않고. 호출 당시 받은 인자값을 offset으로 삼아 원하는 크기만큼 쓰고 읽는 system call이다.
xv6에서 `filewrite` 함수와 `fileread` 함수를 참고하면 다음과 같이 file의 offset을 바꾸는 것을 확인할 수 있었다.

```c++
int
fileread(struct file *f, char *addr, int n)
{
              (생략)
      if ((r = readi(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
              (생략)              
}

int
filewrite(struct file *f, char *addr, int n)
{
              (생략)
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
              (생략)              
}
```

따라서 위에서 `f->off`를 변경하는 부분만 지워 각각 `filepread`, `filepwrite` 함수를 만드는 방식으로 구현하였다.
