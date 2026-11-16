# eBPF 도구 명령어 레퍼런스

이 문서는 이 프로젝트에서 실제로 사용한 명령과 앞으로 자주 쓸 점검 명령을 모아 둔 참고 노트다. 명령은 Ubuntu 20.04, 커널 5.4 환경을 기준으로 기록한다.

> 주의: 현재 서버의 bpftrace는 이전 버전이다. 최신 문서의 모든 함수를 지원하지 않을 수 있다. 예를 들어 이 환경에서는 strftime()를 사용할 수 없다.

## 1. 환경 정보 확인

### 운영체제와 커널 확인

목적: 설치할 패키지와 eBPF 기능의 기준이 되는 OS/커널을 확인한다.

    cat /etc/os-release
    uname -r

현재 확인된 값:

    Ubuntu 20.04 LTS
    5.4.0-216-generic

### BTF 존재 여부 확인

BTF(Binary Type Format)는 커널 구조체 타입 정보다. CO-RE 방식에서 현재 커널에 맞는 구조체 필드 위치를 찾는 데 사용한다.

    test -r /sys/kernel/btf/vmlinux && echo 'BTF available' || echo 'BTF unavailable'

예상 결과:

- BTF available: CO-RE 학습 경로를 검토할 수 있다.
- BTF unavailable: 현재 커널의 CO-RE 사용이 제한될 수 있다.

## 2. 패키지와 도구 확인

### 설치된 bpftrace 버전 확인

목적: 지원하는 문법과 함수를 확인한다.

    bpftrace --version

주의: Ubuntu 20.04의 기본 bpftrace는 strftime()를 지원하지 않을 수 있다.

### bpftool 위치와 버전 확인

Ubuntu 20.04에서는 bpftool이 독립 APT 패키지가 아니라 linux-tools-common에 포함될 수 있다.

    dpkg -L linux-tools-common | grep -E '/bpftool$'
    /usr/sbin/bpftool version

예상 결과: 일반적으로 /usr/sbin/bpftool 경로가 표시된다.

### BPF 기능 확인

목적: 현재 커널이 어떤 eBPF 기능을 지원하는지 확인한다.

    sudo /usr/sbin/bpftool feature probe kernel | less

ring buffer, BTF, JIT 관련 줄만 보려면 다음을 사용한다.

    sudo /usr/sbin/bpftool feature probe kernel |       grep -E 'BPF_MAP_TYPE_RINGBUF|CONFIG_BPF|CONFIG_DEBUG_INFO_BTF|CONFIG_BPF_JIT'

주의: 현재 커널 5.4는 BPF_MAP_TYPE_RINGBUF를 지원하지 않는다. ring buffer 기반 프로그램은 5.8 이상 커널 또는 Ubuntu 22.04/24.04 환경에서 진행하는 편이 좋다.

## 3. bpftrace 기본 사용법

### 도움말과 tracepoint 탐색

목적: 사용할 수 있는 probe와 tracepoint 이름을 확인한다.

    bpftrace --help
    sudo bpftrace -l 'tracepoint:syscalls:*exec*'
    sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_execve/format

마지막 명령은 sys_enter_execve tracepoint가 제공하는 args 필드를 보여 준다.

### execve 이벤트를 가장 간단히 관찰

목적: 어떤 프로세스가 실행 파일을 실행하려 하는지 확인한다.

    sudo bpftrace -e '
    tracepoint:syscalls:sys_enter_execve
    {
      printf("pid=%d comm=%s file=%s
", pid, comm, str(args->filename));
    }'

필드 의미:

- pid: execve를 요청한 프로세스 ID
- comm: 실행 요청 직전 프로세스 이름. 최대 15글자라 길면 잘린다.
- args->filename: execve에 전달된 실행 대상 경로
- str(): 사용자 공간의 문자열 포인터를 읽는다.

중요: sys_enter_execve는 실행 성공 여부가 아니라 실행 시도를 관찰한다. PATH 탐색 중 실패한 후보 경로도 보일 수 있다.

### 표 형식으로 execve 관찰

목적: 전체 이벤트를 읽기 쉬운 열 형식으로 출력한다.

    sudo bpftrace -e '
    BEGIN
    {
      printf("%-8s %-16s %s
", "PID", "COMM", "FILE");
    }

    tracepoint:syscalls:sys_enter_execve
    {
      printf("%-8d %-16s %s
",
             pid,
             comm,
             str(args->filename));
    }'

예상 출력:

    PID      COMM             FILE
    1548     sshd             /usr/sbin/sshd
    1551     sshd             /bin/sh

종료: Ctrl+C

### 특정 프로세스만 관찰

목적: SSH 로그인 관련 이벤트처럼 관심 대상만 보아 로그 양을 줄인다.

    sudo bpftrace -e '
    tracepoint:syscalls:sys_enter_execve
    / comm == "sshd" /
    {
      printf("pid=%d comm=%s file=%s
", pid, comm, str(args->filename));
    }'

조건식 / ... / 은 predicate라고 하며, 조건이 참일 때만 출력 블록을 실행한다.

### 실행 횟수 집계

목적: 실시간 이벤트 대신 어떤 프로세스가 많이 실행되는지 요약한다.

    sudo bpftrace -e '
    tracepoint:syscalls:sys_enter_execve
    {
      @[comm] = count();
    }'

Ctrl+C로 종료하면 map의 집계 결과가 출력된다.

## 4. 작은 검증 명령

다른 SSH 세션에서 아래를 실행하고 bpftrace 화면의 변화를 확인한다.

    /usr/bin/id
    /bin/echo hello
    /bin/sh -c 'echo test'

기대 결과는 각각 id, echo, sh 등의 실행 시도가 로그에 추가되는 것이다.

## 5. 주의할 명령과 제한

- 모든 명령은 우선 학습 VM에서 sudo로 실행한다.
- 명령행 인자와 환경 변수에는 비밀값이 들어갈 수 있으므로 기본 수집 대상에 넣지 않는다.
- bpftrace 출력이 많으면 성능·가독성이 나빠진다. process 이름, PID, 파일 경로 등으로 범위를 좁힌다.
- 최신 블로그나 문서의 bpftrace 문법은 현재 설치 버전과 다를 수 있다. 오류가 나면 먼저 bpftrace --version을 확인한다.
- 장기 운영용 모니터링은 bpftrace보다 C/libbpf, Go/cilium-ebpf, Rust/Aya 기반 프로그램으로 발전시키는 것이 목표다.

