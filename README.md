# Rust 기반 eBPF 실행 이벤트 학습 프로젝트

이 저장소는 단일 Linux 서버에서 프로세스 실행 요청을 관찰하는 eBPF 학습 프로젝트다. 기존 C + libbpf `execmon` 구현은 기준 구현으로 Git 이력에 보존하고, 이제부터 동일한 데이터 흐름을 **Rust 기반으로 단계적으로 마이그레이션**한다.

첫 목표는 Kubernetes 연동이나 자동 차단이 아니다. 커널 이벤트가 사용자 공간 로그가 되는 아래 흐름을 Rust 코드에서도 직접 확인하는 것이 목표다.

```text
/usr/bin/id 실행 요청
  → sys_enter_execve tracepoint
  → Rust eBPF 프로그램 (커널)
  → ring buffer map
  → Rust 사용자 공간 loader
  → 사람이 읽을 수 있는 로그
```

> 현재 단계: Rust 마이그레이션의 문서와 기준점을 준비하는 단계다. Rust 소스, 의존성, 빌드 설정은 아직 추가하지 않았다. 따라서 Rust 실행 결과는 아직 없다.

## 왜 Rust로 옮기나

C + libbpf 구현은 eBPF의 기본 구조를 가장 직접적으로 보여 주지만, 사용자 공간 코드에서는 메모리 안전성과 오류 처리를 개발자가 직접 관리해야 한다. Rust 마이그레이션에서는 같은 관측 범위를 유지하면서 다음 차이를 학습한다.

| 구분 | 기존 기준 구현 | Rust 마이그레이션 목표 |
| --- | --- | --- |
| 커널 프로그램 | C eBPF | Rust eBPF |
| 사용자 공간 loader | C + libbpf skeleton | Rust crate 기반 loader |
| 이벤트 전달 | `BPF_MAP_TYPE_RINGBUF` | 같은 ring buffer 방식 유지 |
| 관측 hook | `sys_enter_execve` tracepoint | 같은 tracepoint 유지 |
| 수집 범위 | PID, PPID, UID, `comm`, 실행 파일 경로 | 같은 최소 필드부터 재현 |
| 동작 성격 | 관찰 전용 | 관찰 전용 유지 |

**eBPF**는 커널 안에서 제한적으로 실행되는 프로그램이며, verifier(안전성 검사기)가 로드 전에 모든 실행 경로를 검사한다. Rust를 사용해도 eBPF 프로그램은 verifier와 커널 helper의 제약을 그대로 받는다. Rust는 이 제약을 없애는 도구가 아니라, 사용자 공간과 공유 데이터 구조를 더 안전하게 다루는 선택지다.

## 마이그레이션 원칙

- 기존 `execmon/`의 C 소스는 비교와 학습을 위한 기준 구현으로 보존한다. Rust 버전이 최소 검증을 끝내기 전까지 C 버전을 삭제하거나 덮어쓰지 않는다.
- 첫 Rust 구현은 tracepoint, ring buffer, `execve` 이벤트 하나만 다룬다. kprobe, 컨테이너 메타데이터, 자동 차단을 함께 추가하지 않는다.
- Rust eBPF 프레임워크는 다음 단계의 도구 호환성 점검 후 확정한다. 현재 후보는 Aya이며, 아직 의존성 버전을 고정하거나 설치하지 않았다.
- 공용 이벤트 구조체는 커널 쪽과 사용자 공간에서 바이트 배치가 같아야 한다. Rust에서는 정수 크기, 정렬, NUL 종료 문자열, `repr(C)` 같은 ABI(Application Binary Interface, 프로그램 간 메모리 약속)를 명시적으로 검증한다.
- CO-RE(Compile Once, Run Everywhere)는 한 번 빌드한 eBPF 프로그램을 다른 커널 구조체 배치에 맞춰 로드 시 보정하는 방식이다. Rust로 옮겨도 대상 커널의 BTF(Binary Type Format, 커널 타입 정보)와 필요한 helper·hook이 실제로 존재하는지 먼저 확인한다.

## 수집 범위와 보안 경계

첫 Rust 버전은 PID, PPID, UID, 호출 당시 프로세스 이름(`comm`), 실행 대상 파일 경로만 수집한다.

명령행 인자(`argv`)와 환경 변수(`envp`)는 토큰·비밀번호·개인정보를 포함할 수 있으므로 수집하지 않는다. 실행 파일 경로도 민감할 수 있으므로, 실제 VM 검증에서는 로그 파일 권한과 보존 기간을 별도로 확인한다. 이 프로젝트는 관찰 전용이며 프로세스 종료, 네트워크 차단, 정책 강제를 구현하지 않는다.

`sys_enter_execve`는 실행 **시도** 시점이다. 실행 실패와 PATH 탐색 중의 실패한 후보도 보일 수 있다. 실행 성공 여부는 후속 단계에서 `sys_exit_execve`를 추가해 구분한다.

## 마이그레이션 순서

| 단계 | 작은 목표 | 완료 기준 |
| --- | --- | --- |
| 0. 기준점 기록 | C 구현과 기존 실습 기록을 보존하고 Rust 작업 로그를 분리한다. | 이 README와 Rust 전용 진행 로그가 있다. |
| 1. 도구 적합성 점검 | Rust toolchain, 대상 VM의 BTF, ring buffer, tracepoint를 확인한다. | 명령과 실제 출력이 기록된다. |
| 2. 프로젝트 뼈대 | 커널 eBPF crate, 사용자 공간 crate, 공용 이벤트 타입을 만든다. | 빌드만 성공한다. |
| 3. 이벤트 하나 | `sys_enter_execve`에서 최소 필드를 ring buffer로 보낸다. | verifier를 통과해 load·attach한다. |
| 4. 사용자 공간 출력 | loader가 이벤트를 읽어 안전하게 출력한다. | `/usr/bin/id` 실행이 한 줄의 로그로 보인다. |
| 5. 종료·부하 검증 | Ctrl-C 정리, ring buffer 드롭, 로그량을 확인한다. | link 해제와 측정 결과가 기록된다. |

현재는 0단계다. 다음 변경은 1단계의 Rust toolchain 및 대상 VM 적합성 점검만 다룬다.

## 저장소 구성

| 경로 | 내용 |
| --- | --- |
| `execmon/` | 기존 C + libbpf 기준 구현. Rust 마이그레이션 중에는 비교용으로 유지한다. |
| `guide.md` | 기존 C 기반 학습 가이드. Rust 전환과 섞지 않고 필요할 때 별도 문서로 추가한다. |
| `tool_commands.md` | 환경과 eBPF 도구 점검 명령. Rust 관련 명령은 검증 후 추가한다. |
| `server_setup_guide.md` | 별도 실습 VM 준비와 보안 설정 |
| `scenario/` | 기존 C 구현의 단계별 테스트 시나리오 |
| `log/` | 날짜별 진행 기록. Rust 작업은 파일명과 내용에 Rust임을 명시한다. |
| `error/` | 오류 원문, 원인, 해결 조치, 후속 확인 기록 |

## 실행 환경과 권한

운영 서버가 아닌 별도 Linux 실습 VM에서 진행한다. 기존 관측 범위를 그대로 재현하려면 Linux 5.11 이상, BTF, ring buffer, `sys_enter_execve` tracepoint와 현재 task BTF helper가 필요하다. 실제 요구 사항은 Rust eBPF 프레임워크와 대상 VM을 확정한 뒤 명령으로 다시 검증한다.

eBPF 프로그램을 load·attach하려면 root 또는 제한된 Linux capability가 필요할 수 있다. 학습 단계에서는 전용 VM과 전용 계정을 사용하고, 장기 실행 서비스 등록이나 운영 서버 배포는 하지 않는다.

## 기록 규칙

- Rust 작업 중 확인된 사실은 `log/YYYY-MM-DD_Rust_진행_로그.md`에 남긴다.
- 오류가 발생하면 `error/YYYY-MM-DD_Rust_주제.md`에 명령, 원문 출력, 확인된 원인과 추정을 구분해 기록한다.
- 실제 verifier 통과, load·attach, 이벤트 출력을 확인하기 전에는 동작한다고 표현하지 않는다.
