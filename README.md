# eBPF 실행 이벤트 학습 프로젝트

단일 Linux 서버에서 프로세스 실행 요청을 관찰하는 작은 eBPF 학습 프로젝트다. 첫 목표는 Kubernetes 연동이나 자동 차단이 아니라, **커널 이벤트가 사용자 공간 로그가 되는 흐름**을 직접 확인하는 것이다.

> 상태: `execmon` 소스 초안과 실습 문서는 준비됐지만, 새 실습 VM에서의 빌드·load·attach·이벤트 출력은 아직 검증하지 않았다.

## 무엇을 관찰하나

`execmon`은 `execve()` 시스템 콜의 진입 시점에 발생하는 `sys_enter_execve` tracepoint를 관찰한다.

```text
/usr/bin/id 실행 요청
  → sys_enter_execve tracepoint
  → execmon.bpf.c (커널의 eBPF 프로그램)
  → ring buffer events map
  → execmon.c (사용자 공간 loader)
  → 터미널 로그
```

- **tracepoint**: 커널이 관찰용으로 안정적으로 공개한 이벤트 지점이다.
- **eBPF**: 커널이 verifier(안전성 검사기)로 검증한 뒤 제한된 형태로 실행하는 프로그램이다.
- **ring buffer**: eBPF 프로그램이 사용자 공간으로 이벤트를 전달하는 커널 메모리 큐다.
- **BTF(Binary Type Format)**: 커널 구조체의 타입 정보다. CO-RE가 현재 커널에 맞는 필드 위치를 찾는 데 쓴다.
- **CO-RE(Compile Once, Run Everywhere)**: 한 번 컴파일한 eBPF 코드를 여러 커널 구조체 배치에 맞춰 로드 시 보정하는 방식이다.

## 현재 범위와 개인정보 원칙

현재 이벤트는 PID, PPID, UID, 호출 당시 프로세스 이름(`comm`), 실행 대상 파일 경로만 수집한다.

명령행 인자(`argv`)와 환경 변수(`envp`)는 토큰·비밀번호 등 민감한 값을 담을 수 있으므로 수집하지 않는다. 이 프로젝트는 관찰 전용이며, 프로세스 종료나 네트워크 차단 기능을 구현하지 않는다.

`sys_enter_execve`는 **실행 시도** 시점이다. PATH 탐색 중 실패한 시도도 보일 수 있으며, 실행 성공 여부는 아직 구분하지 않는다. 또한 `comm`은 대상 실행 파일 이름으로 바뀌기 전의 값일 수 있다.

## 저장소 구성

| 경로 | 내용 |
| --- | --- |
| `guide.md` | 전체 학습 가이드와 개념 설명 |
| `tool_commands.md` | 환경·도구 점검 명령 |
| `server_setup_guide.md` | 별도 실습 VM 준비와 보안 설정 |
| `scenario/` | VM 검증을 위한 단계별 테스트 시나리오 |
| `execmon/src/execmon.bpf.c` | 커널에서 실행되는 eBPF 프로그램 |
| `execmon/src/execmon.c` | BPF를 load·attach하고 이벤트를 출력하는 loader |
| `execmon/src/execmon.h` | BPF와 loader가 공유하는 이벤트 구조체 |
| `log/`, `error/` | 진행 사실과 오류 원문·해결 기록 |

## 실행 환경

운영 서버가 아닌 별도 Linux 실습 VM을 사용한다. 권장 환경은 Ubuntu Server 24.04 LTS, 2 vCPU, 4 GiB RAM, 30 GiB SSD다.

현재 `execmon`은 다음 기능을 사용한다.

| 필요 항목 | 이유 |
| --- | --- |
| Linux 5.11 이상 | `bpf_get_current_task_btf()` helper로 PPID를 수집한다. |
| BTF | `vmlinux.h` 생성과 CO-RE 구조체 접근에 필요하다. |
| ring buffer | eBPF 이벤트를 loader로 전달한다. |
| `sys_enter_execve` tracepoint | `execve()` 요청을 관찰하는 hook이다. |

ring buffer 자체는 Linux 5.8부터지만, 현재 PPID 수집 방식 때문에 이 소스의 최소 기준은 Linux 5.11이다.

## 시작하기

아래 명령은 **실습 VM의 Ubuntu 터미널**에서 실행한다. BPF 프로그램을 실제로 load·attach하는 마지막 실행만 `sudo`가 필요하다.

이 저장소 전체를 VM으로 복사하거나 clone했을 때, 표준 작업 경로는 다음과 같다. 이후의 빌드 명령은 이 디렉터리에서 실행한다.

```bash
cd ~/workspace/ebpfProject/execmon
```

`/usr/local/src`와 root의 홈은 사용하지 않는다. 소스와 build 산출물은 전용 계정 `ebpf-lab`의 홈 아래에 둔다.

### 1. 도구 설치

```bash
sudo apt update
sudo apt install -y \
  clang llvm libbpf-dev libelf-dev zlib1g-dev \
  linux-tools-common linux-tools-$(uname -r) linux-headers-$(uname -r) \
  build-essential pkg-config git bpftrace
```

Ubuntu 24.04에서 `bpftool`은 독립 설치 패키지가 아니라 `linux-tools-common` 등이 제공하는 가상 패키지일 수 있다. 따라서 `apt install bpftool`은 실행하지 않고, 설치 후 명령을 확인한다.

```bash
command -v bpftool || command -v /usr/sbin/bpftool
bpftool version
clang --version
```

`linux-tools-$(uname -r)` 또는 `linux-headers-$(uname -r)`를 찾지 못하면, 현재 실행 중인 커널과 APT 저장소의 패키지 계열이 맞는지 먼저 확인한다.

```bash
apt-cache policy linux-tools-$(uname -r) linux-headers-$(uname -r)
```

### 2. 커널 기능 확인

```bash
uname -r
test -r /sys/kernel/btf/vmlinux && echo 'BTF: available' || echo 'BTF: unavailable'
sudo bpftool feature probe kernel | grep -Eqi 'map_type.*ringbuf|ringbuf' && echo 'ring buffer: available' || echo 'ring buffer: unavailable'
sudo bpftool feature probe kernel | grep -q 'bpf_get_current_task_btf' && echo 'current task BTF helper: available' || echo 'current task BTF helper: unavailable'
test -r /sys/kernel/tracing/events/syscalls/sys_enter_execve/format && echo 'tracepoint: available' || echo 'tracepoint: unavailable'
```

네 항목이 모두 `available`이어야 다음 단계로 진행한다. 하나라도 실패하면 `error/YYYY-MM-DD_주제.md`에 명령과 원문 출력을 기록하고, 먼저 환경 문제를 해결한다.

### 3. 빌드

```bash
cd execmon
mkdir -p build

bpftool btf dump file /sys/kernel/btf/vmlinux format c > src/vmlinux.h
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 \
  -I/usr/include/$(uname -m)-linux-gnu -Isrc \
  -c src/execmon.bpf.c -o build/execmon.bpf.o
bpftool gen skeleton build/execmon.bpf.o > build/execmon.skel.h

cc -g -O2 -Wall -Ibuild -Isrc src/execmon.c \
  -lbpf -lelf -lz -o build/execmon
```

ARM64 VM에서는 `-D__TARGET_ARCH_x86`을 `-D__TARGET_ARCH_arm64`로 바꾼다. 커널을 업데이트했다면 `src/vmlinux.h`를 다시 생성한다.

### 4. 최소 기능 검증

첫 터미널에서 실행한다.

```bash
cd execmon
sudo ./build/execmon
```

다른 터미널에서 다음을 실행한다.

```bash
/usr/bin/id
```

첫 터미널에 아래와 비슷한 출력이 나타나면, 커널 tracepoint부터 사용자 공간 loader까지 연결된 것이다.

```text
pid=12345    ppid=12001    uid=1000   comm=id              file=/usr/bin/id
```

`Ctrl-C`로 종료한 뒤 아래 명령으로 남아 있는 BPF link가 없는지 확인한다.

```bash
sudo bpftool link show
```
