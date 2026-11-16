# 단일 Linux 서버용 eBPF `execve` 모니터 개발 가이드

이 가이드는 Kubernetes 없이 Linux 서버 한 대에서 프로세스 실행(`execve`)을 수집하는 eBPF 에이전트를 만드는 순서다. 개발·부하 검증은 별도 Ubuntu VM에서 하고, 운영 서버에는 검증된 결과만 배포한다.

## 목표와 구조

```text
프로세스 execve()
  → eBPF tracepoint가 PID·PPID·UID·실행 파일 수집
  → BPF ring buffer
  → 사용자 공간 loader(C/libbpf)
  → JSON Lines 로그
```

처음 버전은 **탐지만** 한다. 자동 차단은 오탐의 서비스 영향을 충분히 검증한 뒤 별도 기능으로 설계한다.

## 1. 개발 환경 준비

권장 환경은 Ubuntu 22.04/24.04 VM, 2 vCPU/4 GB RAM 이상, sudo 가능한 실습 계정이다.

### 1.1 커널과 BTF 확인

```bash
uname -a
uname -r
test -r /sys/kernel/btf/vmlinux && echo 'BTF: available' || echo 'BTF: unavailable'
```

`/sys/kernel/btf/vmlinux`가 있으면 CO-RE(Compile Once, Run Everywhere)를 이용할 수 있다. CO-RE는 실행 시점 커널의 BTF 타입 정보를 이용해 구조체 오프셋 차이를 처리한다. [Linux 커널 libbpf 문서](https://docs.kernel.org/bpf/libbpf/libbpf_overview.html)

### 1.2 도구 설치

```bash
sudo apt update
sudo apt install -y \
  bpftrace bpftool \
  clang llvm libbpf-dev libelf-dev zlib1g-dev \
  linux-tools-common linux-tools-$(uname -r) linux-headers-$(uname -r) \
  build-essential pkg-config git

bpftrace --version
bpftool version
clang --version
```

`linux-tools-$(uname -r)` 또는 headers 패키지가 없으면 현재 커널과 apt 저장소 패키지의 계열이 맞지 않을 수 있다. 커널·도구를 같은 계열로 갱신한 뒤 재부팅하고 재시도한다.

### 1.3 eBPF 기능 점검

```bash
sudo bpftool feature probe kernel | less
sudo sysctl net.core.bpf_jit_enable
```

아래를 확인한다.

- `BPF_MAP_TYPE_RINGBUF`: 이벤트를 사용자 공간으로 전달한다.
- `CONFIG_BPF`, `CONFIG_BPF_SYSCALL`: eBPF 프로그램 로드에 필요하다.
- `CONFIG_DEBUG_INFO_BTF` 또는 BTF 파일: CO-RE에 필요하다.
- `CONFIG_BPF_JIT`: 성능을 위해 권장한다.

## 2. 기존 도구로 먼저 관찰하기

### 현재 Ubuntu 20.04 / Linux 5.4 환경의 호환성 메모

이 가이드의 최신 커널용 예제를 그대로 실행하기 전에, 현재 환경의 제한을 먼저 확인한다.

| 항목 | 현재 환경에서 확인된 상태 | 진행 방법 |
| --- | --- | --- |
| `bpftool` APT 패키지 | 독립 패키지 없음 | `linux-tools-common`이 제공하는 `/usr/sbin/bpftool`을 사용 |
| bpftrace `strftime()` | 지원하지 않음 | 시간 열 없이 고정 폭 표 출력 사용 |
| `BPF_MAP_TYPE_RINGBUF` | Linux 5.4에서 미지원 | `bootstrap_legacy` 또는 perf event array 기반 예제 사용 |
| `bpf_get_current_task_btf()` | Linux 5.11부터 지원 | 현재 `execmon`의 PPID 수집 코드에는 Linux 5.11 이상 필요 |
| `make` 전체 실행 | `minimal_ns`에서 BTF 메타데이터 링크 실패 가능 | 전체 빌드 대신 필요한 target 하나만 빌드 |

`/opt`에 clone한 경로와 `configure` 미실행은 위 오류의 원인이 아니다. libbpf-bootstrap은 이미 포함된 Makefile을 사용하며, `make`는 컴파일 명령을 실행할 뿐 Makefile 자체를 컴파일하지 않는다.

코드를 작성하기 전에 `bpftrace`로 실제 이벤트와 빈도를 본다.

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_execve
{
  printf("pid=%d comm=%s file=%s\n", pid, comm, str(args->filename));
}'
```

다른 터미널에서 테스트한다.

```bash
/usr/bin/id
/bin/echo hello
curl --version
```

종료는 `Ctrl+C`다. 다음을 기록한다.

- 정상적으로 자주 실행되는 명령은 무엇인가?
- 탐지 후보(예: `sh`, `bash`, `curl`, `wget`, `nc`)는 무엇인가?
- 파일명만 필요한가, 인자까지 필요한가?
- 이벤트 양이 감당 가능한가?

tracepoint 포맷은 직접 확인할 수 있다.

```bash
sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_execve/format
```

처음에는 커널 내부 함수 이름에 의존하는 kprobe보다 안정적인 tracepoint를 사용한다.

## 3. 공식 기준 예제 실행

직접 구현 전에 libbpf bootstrap 예제를 실행해 컴파일러·BTF·권한 체인이 정상인지 검증한다. 현재 서버는 Ubuntu 20.04와 Linux 5.4이므로, 먼저 호환용 `minimal_legacy`를 사용한다.

### 3.1 현재 서버: `minimal_legacy`로 eBPF load/attach 검증

```bash
git clone --recurse-submodules https://github.com/libbpf/libbpf-bootstrap.git
cd libbpf-bootstrap/examples/c
make minimal_legacy
file ./minimal_legacy
sudo ./minimal_legacy
```

`minimal_legacy`가 `Successfully started!`를 출력하면 eBPF object load, BPF map 생성, tracepoint attach까지 성공한 것이다. 실행 중인 터미널은 유지하고, 다른 SSH 세션에서 다음을 실행한다.

```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

`BPF triggered from PID ...` 메시지가 보이면 `bpf_printk()`가 커널 trace log에 쓴 결과다. 이 예제는 `sys_enter_write` tracepoint에 붙는다.

### Linux 5.4 환경의 호환 경로

`make`만 실행하면 모든 예제를 빌드한다. 최신 예제 중 일부는 이전 배포판의 clang/libbpf/BTF 조합에서 빌드되지 않을 수 있으므로, 현재 커널 5.4에서는 전체 빌드 대신 예제를 하나씩 빌드한다.

```bash
# BTF가 있는 경우에만 다음 예제로 진행
test -r /sys/kernel/btf/vmlinux && echo 'BTF available' || echo 'BTF unavailable'
make bootstrap_legacy
sudo ./bootstrap_legacy
```

`bootstrap_legacy`는 Linux 5.8부터 지원되는 ring buffer 대신 이전 커널과 호환되는 perf event array를 사용한다. `/opt`에 clone한 경로는 빌드 오류의 원인이 아니며, libbpf-bootstrap은 이 학습 단계에서 `configure`나 시스템 설치를 요구하지 않는다.

### 3.2 최신 커널 환경: 기본 `bootstrap`

Linux 5.8 이상이며 ring buffer와 BTF를 지원하는 환경에서만 기본 `bootstrap`을 실행한다. 단, 이 저장소의 `execmon` 소스는 PPID 수집에 `bpf_get_current_task_btf()`를 사용하므로 Linux 5.11 이상을 기준으로 한다.

```bash
make bootstrap
sudo ./bootstrap
```

`bootstrap`은 프로세스 시작과 종료, BPF map, ring buffer, CO-RE를 함께 보여 주는 예제다. [libbpf-bootstrap](https://github.com/libbpf/libbpf-bootstrap)

## 4. 프로젝트 생성

```bash
mkdir -p ~/execmon/src ~/execmon/build
cd ~/execmon
bpftool btf dump file /sys/kernel/btf/vmlinux format c > src/vmlinux.h
```

```text
execmon/
├── src/
│   ├── execmon.h       # BPF/사용자 공간 공용 이벤트 구조체
│   ├── execmon.bpf.c   # 커널에서 실행될 eBPF 프로그램
│   ├── execmon.c       # loader 및 JSON 출력
│   └── vmlinux.h       # 현재 커널 BTF로 생성
├── build/
└── Makefile
```

커널을 바꿨다면 `vmlinux.h`를 다시 생성한다.

## 5. 공용 이벤트 구조체 작성

`src/execmon.h`:

```c
#ifndef __EXECMON_H
#define __EXECMON_H

#define TASK_COMM_LEN 16
#define FILENAME_LEN  256

struct event {
    unsigned int pid;
    unsigned int ppid;
    unsigned int uid;
    char comm[TASK_COMM_LEN];
    char filename[FILENAME_LEN];
};
#endif
```

처음에는 명령행 인자를 수집하지 않는다. 인자에는 토큰·비밀번호·개인정보가 포함될 수 있기 때문이다.

## 6. eBPF 프로그램 작성

`src/execmon.bpf.c`:

```c
// SPDX-License-Identifier: Dual BSD/GPL
#include "vmlinux.h"
#include "execmon.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(struct trace_event_raw_sys_enter *ctx)
{
    struct event *event;
    struct task_struct *task;
    __u64 id;
    const char *filename;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    id = bpf_get_current_pid_tgid();
    event->pid = id >> 32;
    event->uid = (__u32)bpf_get_current_uid_gid();
    bpf_get_current_comm(event->comm, sizeof(event->comm));

    task = (struct task_struct *)bpf_get_current_task_btf();
    event->ppid = BPF_CORE_READ(task, real_parent, tgid);

    filename = (const char *)ctx->args[0];
    bpf_probe_read_user_str(event->filename, sizeof(event->filename), filename);
    bpf_ringbuf_submit(event, 0);
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
```

핵심은 다음과 같다.

- `sys_enter_execve`는 실행 요청 시점 이벤트다. 성공/실패 구분은 다음 단계에서 `sys_exit_execve`를 추가해 한다.
- `bpf_probe_read_user_str()`로 사용자 공간 포인터의 파일명을 읽는다.
- `BPF_CORE_READ()`가 커널 버전별 구조체 오프셋 차이를 처리한다.
- `bpf_ringbuf_reserve()` 실패는 이벤트 드롭이다. 운영 전에는 드롭 카운터 map을 추가해 측정한다.

## 7. BPF 객체와 skeleton 빌드

```bash
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 -Isrc \
  -c src/execmon.bpf.c -o build/execmon.bpf.o

bpftool gen skeleton build/execmon.bpf.o > build/execmon.skel.h
```

ARM64라면 `-D__TARGET_ARCH_arm64`를 사용한다. skeleton은 libbpf의 load, attach, detach, destroy API를 BPF 객체에 맞게 생성한다. [bpftool skeleton 문서](https://github.com/libbpf/bpftool/blob/main/docs/bpftool-gen.rst)

## 8. 사용자 공간 loader 작성

`src/execmon.c`:

```c
// SPDX-License-Identifier: BSD-2-Clause
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <bpf/libbpf.h>
#include "execmon.h"
#include "execmon.skel.h"

static volatile sig_atomic_t exiting;

static void on_signal(int sig) { (void)sig; exiting = 1; }

static int handle_event(void *ctx, void *data, size_t size)
{
    const struct event *e = data;
    (void)ctx;
    if (size < sizeof(*e))
        return 0;

    /* 운영 버전은 JSON escape와 로그 순환을 구현한다. */
    printf("{\\\"pid\\\":%u,\\\"ppid\\\":%u,\\\"uid\\\":%u,"
           "\\\"comm\\\":\\\"%s\\\",\\\"filename\\\":\\\"%s\\\"}\\n",
           e->pid, e->ppid, e->uid, e->comm, e->filename);
    fflush(stdout);
    return 0;
}

int main(void)
{
    struct execmon_bpf *skel;
    struct ring_buffer *rb;
    int err = 0;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    skel = execmon_bpf__open_and_load();
    if (!skel) return 1;
    if ((err = execmon_bpf__attach(skel))) goto out;

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events),
                          handle_event, NULL, NULL);
    if (!rb) { err = -errno; goto out; }

    while (!exiting) {
        err = ring_buffer__poll(rb, 250);
        if (err == -EINTR) { err = 0; break; }
        if (err < 0) break;
    }
    ring_buffer__free(rb);
out:
    execmon_bpf__destroy(skel);
    return err < 0 ? -err : err;
}
```

빌드 및 실행:

```bash
cc -g -O2 -Wall -Ibuild -Isrc src/execmon.c -lbpf -lelf -lz -o build/execmon
sudo ./build/execmon
```

다른 터미널에서 `/usr/bin/id`를 실행한다. 다음과 비슷한 JSON Lines가 출력되어야 한다.

```json
{"pid":12345,"ppid":12001,"uid":1000,"comm":"id","filename":"/usr/bin/id"}
```

## 9. 검증과 부하 측정

기능 검증:

```bash
sudo ./build/execmon > execmon.jsonl
# 다른 터미널
/usr/bin/id
/bin/sh -c 'echo test'
tail -f execmon.jsonl
```

확인할 항목:

- PID/UID/파일명이 실제 실행과 일치하는가?
- `SIGINT`/ `SIGTERM` 뒤 BPF link가 정리되는가?
- 종료 뒤 `sudo bpftool prog list`에 프로그램이 남지 않는가?
- 파일명 잘림, JSON 깨짐, 로그 폭주가 없는가?

실습 VM에서만 간단한 부하를 준다.

```bash
for i in $(seq 1 1000); do /usr/bin/true; done
pidstat -p $(pgrep -n execmon) 1
```

드롭이 있다면 ring buffer만 키우지 않는다. 먼저 대상 UID·실행 파일·경로를 좁히고, JSON 처리와 원격 전송을 비동기화한다.

## 10. 다음 기능의 우선순위

1. `sys_exit_execve` 추가: 실행 성공 여부와 return code 기록
2. 드롭 카운터 map 추가: ring buffer reserve 실패 수 관찰
3. 사용자 공간 규칙: shell, downloader, 비정상 UID 등 설정 파일 기반 판정
4. `connect`: 어떤 프로세스가 어느 IP:Port에 연결했는지 관찰
5. `openat`, `unlink`, `rename`: 민감 파일 접근과 변조 관찰
6. 로그 순환, 재시도 큐, Prometheus metrics, systemd 서비스화

복잡한 탐지 판단은 eBPF 안이 아니라 사용자 공간에서 처리한다. eBPF는 필요한 이벤트를 안전하고 저렴하게 전달하는 역할에 집중시키는 편이 유지보수에 유리하다.

## 11. 운영 전 체크리스트

- [ ] 운영 커널에서도 BTF/eBPF/JIT 기능을 점검했다.
- [ ] 업무 피크에서 CPU·메모리·드롭률·로그량을 측정했다.
- [ ] 명령 인자와 경로의 민감정보 마스킹 정책이 있다.
- [ ] root 또는 capability 권한을 최소화할 계획이 있다.
- [ ] 바이너리/BPF 객체의 빌드 재현성, 해시·서명, 롤백 절차가 있다.
- [ ] 로그 순환과 전송 실패 backpressure 정책이 있다.
- [ ] 초기에는 자동 차단을 사용하지 않는다.

## 참고 자료

- [Linux 커널: libbpf와 CO-RE](https://docs.kernel.org/bpf/libbpf/libbpf_overview.html)
- [Linux 커널: BPF ring buffer](https://docs.kernel.org/6.6/bpf/ringbuf.html)
- [libbpf-bootstrap](https://github.com/libbpf/libbpf-bootstrap)
- [bpftool skeleton 생성 문서](https://github.com/libbpf/bpftool/blob/main/docs/bpftool-gen.rst)
- [Falco 공식 문서](https://falco.org/docs/)
- [Tetragon 공식 문서](https://tetragon.io/docs/overview/)

