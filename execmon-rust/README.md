# execmon-rust 뼈대

이 디렉터리는 기존 C + libbpf `execmon/`을 바꾸지 않고, Aya 기반 Rust eBPF 구현을 별도로 시작하는 workspace다.

현재 뼈대는 `sys_enter_execve` tracepoint 프로그램을 load·attach한 뒤 Ctrl-C까지 유지하는 단계까지만 담는다. 아직 ring buffer map, 이벤트 수집, PPID 읽기, 로그 출력은 넣지 않았다. 따라서 이 프로그램을 실행해도 `execve` 이벤트 행은 출력되지 않는 것이 정상이다.

`execmon-rust-common` crate는 다음 ring buffer 단계에서 loader가 실제 이벤트 바이트를 읽을 때 연결한다. 현재 attach 전용 loader는 이 구조체를 쓰지 않으므로 의존하지 않는다.

```text
execmon-rust-ebpf/    커널 안에서 verifier 검사를 받는 Rust eBPF crate
execmon-rust-common/  양쪽 crate가 공유할 이벤트 ABI 자리
execmon-rust/         eBPF 객체를 load·attach하는 사용자 공간 Rust loader
```

## 왜 이벤트 수집을 다음 단계로 미뤘나

Rust eBPF의 최소 실패 지점을 작게 유지하기 위해서다. 이번 단계는 Rust toolchain → `bpf-linker` → eBPF ELF → Aya loader → `sys_enter_execve` attach만 검증한다. ring buffer와 이벤트 구조체 해석을 동시에 추가하면 verifier·ABI·사용자 공간 수신 중 어디에서 실패했는지 분리하기 어렵다.

기존 C 구현의 PPID는 `task_struct` 필드 접근을 사용한다. Rust에서 이 접근을 커널 간 호환되는 CO-RE로 구현하려면 현 시점에는 C shim 여부를 별도로 판단해야 한다. 첫 Rust tracepoint 뼈대에는 넣지 않는다.

## VM 사전 조건

기존 VM의 Linux 6.8, BTF, ring buffer, `sys_enter_execve` tracepoint 기준선은 유지한다. Aya 표준 빌드에는 아래 도구가 추가된다.

- Rust stable과 nightly toolchain
- nightly의 `rust-src` component
- `bpf-linker`

이 도구들이 실제로 설치되고 버전이 기록되기 전에는 `cargo build`나 `sudo cargo run`을 실행하지 않는다. 자세한 근거와 점검 순서는 `log/2026-11-18_Rust_Aya_VM_영향_조사.md`에 있다.

VM에서는 먼저 아래 읽기 전용 점검을 실행한다. 이 스크립트는 패키지를 설치하거나 커널 설정을 바꾸지 않는다.

```bash
cd ~/workspace/ebpfProject/execmon-rust
bash scripts/check-prerequisites.sh
```
