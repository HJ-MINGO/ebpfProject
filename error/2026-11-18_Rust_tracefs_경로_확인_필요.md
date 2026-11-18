# Rust 사전 조건 점검의 `sys_enter_execve` tracepoint 경로 확인 필요

작성일: 2026-11-18  
실행 환경: Ubuntu 24.04 실습 VM (`ebpf-lab` 계정)

## 증상

Rust/Aya 사전 조건 스크립트는 Rust toolchain, BTF, ring buffer, current-task BTF helper를 확인했지만 `sys_enter_execve tracepoint: unavailable`을 출력했다.

## 실행한 명령

```bash
cd ~/workspace/execmon-rust
bash scripts/check-prerequisites.sh
```

## 확인된 출력

```text
rustup                           available
cargo                            available
bpf-linker                       available
rustc 1.100.0 (0ef1bc386 2026-11-09)
cargo 1.100.0 (3acc79c0c 2026-11-05)
stable-x86_64-unknown-linux-gnu (active, default)
nightly-x86_64-unknown-linux-gnu
rustc 1.102.0-nightly (59cbdee2e 2026-11-17)
6.8.0-147-generic
x86_64
BTF: available
sys_enter_execve tracepoint: unavailable
ring buffer: available
current task BTF helper: available
```

## 확인된 사실

- Rust stable, nightly, `bpf-linker`가 설치됐다.
- 기존 Rust 뼈대가 기대하는 BTF와 ring buffer, current-task BTF helper는 사용 가능하다.
- 현재 사전 조건 스크립트는 `/sys/kernel/tracing/events/syscalls/sys_enter_execve/format` 한 경로만 비-root 계정으로 검사한다.

## 확인된 원인

- tracefs는 `/sys/kernel/tracing`과 `/sys/kernel/debug/tracing`에 모두 정상 마운트되어 있다.
- `sys_enter_execve` 이벤트 디렉터리와 `format` 파일은 존재하며, `sudo test -r`로 읽기 가능하다.
- 사전 조건 스크립트가 비-root `test -r`을 사용해, tracepoint 파일의 현재 계정 접근 권한을 기능 부재로 잘못 표시했다.

## 후속 확인 명령

```bash
for path in \
  /sys/kernel/tracing/events/syscalls/sys_enter_execve/format \
  /sys/kernel/debug/tracing/events/syscalls/sys_enter_execve/format
do
  if sudo test -r "$path"; then
    echo "available: $path"
  else
    echo "unavailable: $path"
  fi
done

findmnt -t tracefs
```

## 해결 조치

- `execmon-rust/scripts/check-prerequisites.sh`의 tracepoint 검사를 `sudo test -r`로 수정했다.
- Rust loader는 기존 C 구현과 같은 `syscalls` / `sys_enter_execve` tracepoint 이름을 유지한다.

## 후속 확인 항목

- 수정된 스크립트로 `sys_enter_execve tracepoint: available`을 확인한다.
- Aya workspace를 release 빌드하고, 실제 load·attach를 검증한다.
