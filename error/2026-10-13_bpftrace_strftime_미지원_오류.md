# 2026-10-13 — bpftrace strftime 미지원 오류

## 증상

execve 로그에 시간 열을 추가하려고 strftime 함수를 사용했을 때 다음 오류가 발생했다.

    ERROR: Unknown function: strftime

## 원인

현재 서버는 Ubuntu 20.04 LTS 기본 저장소의 이전 bpftrace 계열을 사용한다. 이 버전에는 strftime 함수가 없다. 최신 bpftrace 문서의 예제는 현재 설치된 bpftrace보다 새 기능을 포함할 수 있다.

## 해결 조치

현재 학습 단계에서는 strftime을 제거하고 PID, COMM, FILE을 고정 폭 열로 출력한다. 시간은 필요하면 이후 최신 Ubuntu 또는 최신 bpftrace 환경에서 추가한다.

## 확인 방법

아래 명령이 ERROR 없이 Attaching 1 probe를 표시하면 해결된 것이다.

    sudo bpftrace -e '
    BEGIN
    {
      printf("%-8s %-16s %s\\n", "PID", "COMM", "FILE");
    }
    tracepoint:syscalls:sys_enter_execve
    {
      printf("%-8d %-16s %s\\n", pid, comm, str(args->filename));
    }'

