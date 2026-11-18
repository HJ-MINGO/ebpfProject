# Windows 작업 환경의 `cargo` 미설치

작성일: 2026-11-18  
실행 환경: 저장소를 편집한 Windows 작업 환경

## 증상

Rust workspace의 manifest를 로컬에서 읽기 전용으로 검사하려 했지만 `cargo` 명령을 찾지 못했다.

## 실행한 명령

```powershell
cargo --version
cargo metadata --no-deps --format-version 1 --manifest-path execmon-rust/Cargo.toml
```

## 원문 출력

```text
cargo : 'cargo' 용어가 cmdlet, 함수, 스크립트 파일 또는 실행할 수 있는 프로그램 이름으로 인식되지 않습니다.
```

## 확인된 원인

- 이 Windows 작업 환경의 `PATH`에서 `cargo` 실행 파일을 찾지 못했다.

## 해결 조치

- 이 환경에 Rust를 설치하지 않았다. Rust eBPF 프로그램의 실제 빌드와 실행 대상은 기존 Linux 실습 VM이므로, VM의 Rust toolchain을 먼저 점검하는 순서를 유지한다.
- `execmon-rust/scripts/check-prerequisites.sh`에 VM에서 실행할 Rust/Aya 사전 조건 점검을 추가했다.

## 후속 확인 항목

- Linux VM에서 stable Rust, nightly의 `rust-src`, `bpf-linker`가 있는지 확인한다.
- 그 VM에서 `cargo metadata`와 Aya eBPF 빌드를 수행해 workspace manifest 및 실제 toolchain 조합을 검증한다.

## 구분

- 확인된 사실: 현재 Windows 작업 환경에서는 `cargo`를 실행할 수 없다.
- 확인하지 않은 사항: 대상 Linux 실습 VM의 Rust 설치 상태와 Aya 빌드 가능 여부.
