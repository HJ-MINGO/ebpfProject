# Rust 마이그레이션 VM SSH 별칭 미해결

작성일: 2026-11-18  
실행 환경: Windows 작업 환경

## 증상

기존 eBPF 실습 VM의 Rust toolchain과 커널 기능을 읽기 전용으로 확인하려 했지만, `ebpf-lab` SSH 호스트 별칭을 해석하지 못했다.

## 실행한 명령

```powershell
ssh -o BatchMode=yes -o ConnectTimeout=15 ebpf-lab "uname -r; uname -m; command -v rustup || true; command -v cargo || true; command -v bpf-linker || true; test -r /sys/kernel/btf/vmlinux && echo BTF:available || echo BTF:unavailable; test -r /sys/kernel/tracing/events/syscalls/sys_enter_execve/format && echo tracepoint:available || echo tracepoint:unavailable"
```

## 원문 출력

```text
ssh: Could not resolve hostname ebpf-lab
```

## 확인된 원인

- 현재 작업 환경에서 `ebpf-lab`이라는 SSH 별칭에 대응하는 호스트 이름 또는 IP 주소를 찾지 못했다.

## 해결 조치

- VM·커널·저장소 원격에는 변경을 가하지 않았다.
- VM에 접속할 수 있는 SSH 별칭, 실제 호스트 이름 또는 IP 주소가 확인되면 같은 읽기 전용 점검을 다시 실행한다.

## 후속 확인 항목

- 대상 VM에서 `execmon-rust/scripts/check-prerequisites.sh`를 실행한다.
- Rust stable, nightly(`rust-src`), `bpf-linker`가 확인된 뒤 Aya workspace 빌드와 tracepoint attach를 검증한다.

## 구분

- 확인된 사실: 이 작업 환경에서는 `ebpf-lab` 별칭으로 VM에 연결할 수 없다.
- 확인하지 않은 사항: 대상 VM의 현재 Rust toolchain 설치 상태와 Aya build 결과.
