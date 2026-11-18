# Rust + Aya 전환의 실습 VM 영향 조사

작성일: 2026-11-18  
범위: 기존 C + libbpf `execmon`과 같은 `sys_enter_execve` tracepoint → ring buffer → 사용자 공간 출력 흐름을 Rust + Aya로 옮길 때의 **도구·권한·커널 조건**만 조사했다. 소스 코드와 README는 변경하지 않았다.

## 목표

이번 조사는 Rust 전환이 실습 VM의 Linux 커널을 교체해야 하는지, 아니면 빌드 도구를 추가하면 되는지를 구분하는 단계다. Aya는 Rust eBPF 프로그램의 ELF 객체를 만들고 사용자 공간에서 이를 load/attach하는 라이브러리다. eBPF 프로그램은 여전히 커널 verifier(안전 규칙 검사기)의 검사를 거쳐 커널에서 실행된다.

현재와 전환 뒤의 관측 경로는 다음처럼 유지한다.

```text
execve 요청
  → sys_enter_execve tracepoint
  → Rust eBPF 프로그램 (커널, verifier 검사 후 실행)
  → BPF_MAP_TYPE_RINGBUF
  → Aya 기반 Rust loader (사용자 공간)
  → 사람이 읽는 로그
```

## 확인된 사실 — 저장소에 이미 기록된 VM 기준선

아래는 외부 자료로 재검증한 값이 아니라, 이 저장소의 실제 실행 기록을 요약한 것이다.

| 항목 | 기록된 상태 | Rust + Aya 전환과의 관계 |
| --- | --- | --- |
| 실습 VM | Ubuntu 24.04, Linux `6.8.0-147-generic` | Aya의 `RingBuf`가 요구하는 Linux 5.8 이상을 충족한다. |
| kernel BTF(Binary Type Format, 커널 타입 정보) | available | CO-RE가 필요한 구현 또는 C shim을 쓸 경우 재사용 가능하다. |
| `BPF_MAP_TYPE_RINGBUF` | available | Aya는 사용자 공간 `RingBuf` 수신 API와 커널 쪽 ring buffer 선언을 제공한다. |
| `bpf_get_current_task_btf()` | available | 기존 PPID 수집 방식을 그대로 옮길 경우의 helper 조건은 이미 충족한다. Aya 자체가 이 helper를 요구하는 것은 아니다. |
| `sys_enter_execve` tracepoint | available | 관측 hook을 바꾸지 않고 유지할 수 있다. |
| 현재 검증 | C 구현은 load·attach와 실제 ring buffer 이벤트 수신을 완료했지만, Rust toolchain/Aya build와 Rust 객체의 verifier 통과는 아직 미검증이다. | Rust 전환의 다음 검증 대상이다. |

근거(저장소): `log/2026-11-17_진행_로그.md`, `log/2026-11-18_Rust_마이그레이션_진행_로그.md`, `server_setup_guide.md`.

## 확인된 사실 — Aya/Rust 공식 자료 기준 요구 사항

| 구분 | 확인된 요구/동작 | 이 VM에서의 영향 |
| --- | --- | --- |
| Rust toolchain | Aya의 개발 환경 문서는 **stable과 nightly**를 모두 설치하도록 안내한다. `rustup`은 Rust stable과 Cargo 설치에 쓰인다. | stable만으로는 Aya 표준 eBPF 빌드 경로를 충족하지 않는다. stable은 loader 등 일반 사용자 공간 crate에, nightly + `rust-src`는 BPF crate에 필요하다. |
| BPF target | Aya의 `bpf-linker` 문서는 eBPF crate를 `bpfel-unknown-none`과 `-Z build-std=core`로 빌드한다. big-endian 머신이면 `bpfeb-unknown-none`을 쓴다. | 기록된 x86_64 Ubuntu VM은 little-endian이므로 `bpfel-unknown-none` 후보가 맞다. 실제 아키텍처는 VM에서 `uname -m`으로 다시 확인한다. |
| BPF target 배포 방식 | `bpf-linker` 공식 문서는 BPF target이 Rust Tier 3라 stable rustup이 BPF target을 제공하지 않는다고 명시한다. stable 경로에는 `RUSTC_BOOTSTRAP=1`과 별도 sysroot 구축이 필요하다. | 처음 실습에서는 Aya 문서의 nightly 경로를 사용하고, stable-only 우회는 채택하지 않는 편이 단순하다. |
| `bpf-linker` | Aya 개발 환경은 `bpf-linker` 설치를 필수로 열거한다. 공식 저장소는 `cargo binstall bpf-linker` 또는 릴리스 tarball을 rustup 사용자에게 권장하며, 단순 `cargo install` 소스 빌드는 LLVM·시스템 라이브러리 결합 때문에 일반 사용자에게 권장하지 않는다. | VM에 `bpf-linker`를 추가한다. 릴리스 binary 또는 `cargo-binstall` 경로를 우선 검토한다. |
| LLVM/Clang | 순수 Rust Aya eBPF 빌드의 기본 요구는 `bpf-linker`다. 그러나 CO-RE C shim을 쓰면 `clang -target bpfel -emit-llvm`, `libbpf` 개발 헤더, `bpftool`이 필요하며, Clang은 Rust toolchain 및 `bpf-linker`의 LLVM과 호환되어야 한다. | 기존 VM의 clang/bpftool/libbpf 개발 환경은 C shim 선택 시 재사용 가능하다. **C shim을 쓰지 않는 첫 Rust tracepoint 구현에 clang 추가 설치가 Aya 문서상 필수라고 확인되지는 않았다.** |
| `bpftool` | Aya 문서는 커널 구조체 binding 생성을 위해 `bpftool`이 필요하다고 안내한다. C shim CO-RE 문서는 BTF에서 `vmlinux.h`를 생성하는 명령을 제시한다. | 이미 설치/기능 probe가 기록됐지만, 현재 실행 파일과 버전은 Rust 실습 직전에 다시 확인한다. Ubuntu 24.04의 `bpftool` 가상 패키지 문제는 기존 오류 기록을 따른다. |
| ring buffer | Aya `RingBuf` 문서는 eBPF 이벤트를 사용자 공간에 전달하는 map이며 최소 커널은 Linux 5.8이라고 명시한다. 커널 측 `reserve`가 `None`이면 buffer가 가득 찬 것이다. | Linux 6.8 기준선은 충족한다. 새 구현에는 reserve 실패(드롭) 횟수 계측을 넣어야 한다. |
| CO-RE와 BTF | Aya의 C shim 문서는 build-time BTF로 만든 CO-RE relocation을 Aya loader가 **배포 대상 커널 BTF**로 적용한다고 설명한다. helper/attachment point의 부재와 필드 의미 변화는 프로그램이 별도로 처리해야 한다. | BTF가 available인 현재 VM은 유리하지만, BTF 존재만으로 load 성공을 보장하지 않는다. 현재 helper와 tracepoint probe도 계속 확인한다. |

## 권한과 커널 영향

### 확인된 사실

- Aya의 공식 C shim 흐름은 최종 객체를 Aya로 load하고, 별도 C 사용자 공간 loader가 필요 없다고 설명한다. 즉 C loader/skeleton은 Rust loader로 교체할 수 있다.
- `RingBuf` API의 최소 커널은 Linux 5.8이다. 현재 프로젝트가 PPID용 `bpf_get_current_task_btf()`를 계속 쓴다면 저장소에서 기록한 Linux 5.11 이상이라는 더 높은 기존 기준을 유지한다.

### 현재 자료만으로 확정하지 않는 항목

- Aya/Rust 공식 문서에서 이 `tracepoint` 프로그램을 load/attach하는 데 필요한 Linux capability 조합을 직접 명시한 근거는 이번 범위에서 찾지 못했다. 따라서 `CAP_BPF`/`CAP_PERFMON`만으로 충분하다고 이 노트에서 단정하지 않는다.
- 이 프로젝트의 첫 Rust 실행은 기존과 같이 전용 실습 VM에서 `sudo`로 수행해 실제 load/attach를 먼저 확인한다. 이후에만 최소 capability로 좁힐 수 있는지 별도 Linux 커널 공식 문서와 실제 실행으로 검증한다.

### 결론: VM을 바꿔야 하는가

**커널 교체가 필요한 것으로 확인되지 않았다.** 기록된 Linux 6.8, BTF, ring buffer, `sys_enter_execve`, current-task BTF helper는 기존 관측 범위를 유지하기에 이미 충분한 기준선이다. Rust + Aya 전환의 직접적인 VM 영향은 커널 기능이 아니라 사용자 공간의 Rust toolchain, nightly `rust-src`, `bpf-linker`, 그리고 선택한 CO-RE 방식에 따른 `bpftool`/clang/libbpf 개발 헤더다.

단, 이것은 호환성 판단이지 Rust 객체의 실행 성공 증명은 아니다. verifier, attach, ring buffer 수신은 해당 VM에서 빌드한 실제 Aya 객체로 확인해야 한다.

## 권장하는 최소 설치·점검 순서

아래는 **권장 사항**이며 아직 VM에서 실행하지 않았다. 패키지 이름·버전은 설치 전에 Ubuntu APT와 Aya의 해당 시점 공식 `bpf-linker` 문서로 확인한다.

1. `rustup`으로 stable과 nightly(`rust-src` 포함)를 설치한다. `rustc --version`, `cargo --version`, `rustup toolchain list`로 기록한다.
2. Aya 공식 경로에 맞는 `bpf-linker`를 설치하고 `bpf-linker --version`을 기록한다. 소스 빌드는 LLVM version 결합 문제가 있으므로 피한다.
3. 현재 VM에서 `uname -m`, `/sys/kernel/btf/vmlinux`, `bpftool feature probe kernel`, tracepoint format 파일을 다시 점검한다.
4. 첫 구현은 C shim 없이 `sys_enter_execve` + ring buffer 한 이벤트만 만든다. 이로써 Rust compiler/linker, verifier, Aya loader, ring buffer reader를 한 번에 최소 범위로 검증한다.
5. PPID를 유지하는 단계에서 Aya의 순수 Rust helper 접근으로 충분한지 먼저 확인한다. kernel struct field 접근이 필요할 때에만 C shim CO-RE와 clang/libbpf 개발 헤더를 추가한다.

## 공식 출처

- [Aya Book — Development Environment](https://aya-rs.dev/book/start/development): stable/nightly + `rust-src`, `bpf-linker`, `bpftool`, template 요구 사항.
- [Aya `bpf-linker` README](https://github.com/aya-rs/bpf-linker#readme): 설치 경로, nightly eBPF build target, BTF linker flag.
- [Aya `bpf-linker` BUILDING.md](https://github.com/aya-rs/bpf-linker/blob/main/BUILDING.md): BPF target의 stable 제한, LLVM version 일치 요구, source build 조건.
- [Aya Book — Rust with C shims](https://aya-rs.dev/book/aya/co-re/rust-with-c-shims): CO-RE C shim의 `bpftool`/clang/libbpf header 요구, deployment-kernel BTF relocation, Aya loader 역할.
- [Aya `RingBuf` API](https://docs.rs/aya/latest/aya/maps/ring_buf/struct.RingBuf.html): Linux 5.8 최소 버전, 사용자 공간 수신 방식.
- [Rust Cargo Book — Installation](https://doc.rust-lang.org/cargo/getting-started/installation.html): rustup이 stable Rust와 Cargo를 설치한다는 공식 안내.

## 회고와 다음 단계

이번 조사는 기존 VM의 커널 기반을 바꾸지 않고 Aya toolchain을 추가하는 경로가 타당함을 확인했다. 아직 모르는 것은 실제 설치된 Rust/LLVM/bpf-linker 조합과 Aya 객체의 verifier 결과다. 다음 작은 작업은 VM에서 toolchain과 BPF 기능을 읽기 전용으로 점검하고, 결과를 새 진행 로그에 남기는 것이다.
