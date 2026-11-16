# execmon — eBPF `execve` 학습용 모니터

`execmon`은 단일 Linux 서버에서 프로세스의 `execve()` 실행 요청을 관찰하는 학습용 프로그램이다.

```text
프로세스가 execve() 호출
  → tracepoint/syscalls/sys_enter_execve
  → execmon.bpf.c의 handle_execve 실행
  → ring buffer events map
  → execmon.c의 사용자 공간 loader
  → 표준 출력
```

## 현재 작성한 파일

- `src/execmon.h`: eBPF 프로그램과 사용자 공간이 공유하는 이벤트 구조체
- `src/execmon.bpf.c`: 커널 tracepoint에서 이벤트를 수집하는 eBPF 프로그램
- `src/execmon.c`: skeleton을 load/attach하고 ring buffer를 읽는 사용자 공간 loader

## 아직 하지 않은 단계

1. Linux 대상 서버에서 BTF 기반 `src/vmlinux.h` 생성
2. eBPF 객체와 skeleton 생성
3. loader 빌드 및 VM에서 실행 검증

현재 실습 서버의 Linux 5.4는 ring buffer를 지원하지 않는다. 또한 이 소스는 PPID 수집에 Linux 5.11부터 제공되는 `bpf_get_current_task_btf()` helper를 사용한다. 따라서 현재 기준 구현의 최소 커널은 Linux 5.11이며, 해당 서버에서 실행하려면 perf event array 기반 legacy 변형을 별도로 만든다.

## 수집하는 정보와 수집하지 않는 정보

현재는 PID, 부모 PID, UID, 기존 프로세스 이름, 실행 대상 파일 경로만 수집한다.

명령행 인자(`argv`)와 환경 변수(`envp`)는 비밀값을 포함할 수 있으므로 수집하지 않는다.

