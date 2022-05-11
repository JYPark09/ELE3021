# ELE3021
Operating Systems @ Hanyang Univ.

Simple operating system based on xv6.

## Key features
- MLFQ and Stride scehduler
  - See `set_cpu_share` syscall.
- Light weight process (LWP)
  - See `pthread_create`, `pthread_join`, and `pthread_exit` syscall.
- Large size filesystem.
  - See `sync` syscall.
  - See `pwrite`, and `pread` syscall.
