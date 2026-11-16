# eBPF와 Falco를 이용한 Kubernetes 런타임 위협 모니터링

> 대상 영상: [토스의 K8S를 더 안전하게: Falco를 활용한 K8S 위협 모니터링](https://www.youtube.com/watch?v=Wb6l9XTAKN8)  
> 보조 자료: [GUARDIANS 25 발표 자료](https://static.toss.im/GUARDIANS25/C-1.Falco%EB%A5%BC%20%ED%99%9C%EC%9A%A9%ED%95%9C%20K8S%20%EC%9C%84%ED%98%91%20%EB%AA%A8%EB%8B%88%ED%84%B0%EB%A7%81_%EA%B3%A0%EA%B2%BD%EC%84%9D.pdf)

## 요약

발표의 핵심은 정적 취약점 점검이나 경계 네트워크 장비만으로는 컨테이너 내부의 실제 행위를 알기 어렵다는 점이다. Falco를 Kubernetes 노드마다 DaemonSet으로 배포하고, eBPF 센서가 시스템 콜을 수집한 뒤 컨테이너·Pod 맥락을 붙여 룰로 판단한다. 탐지 결과는 Kafka, SIEM, SOC로 보내 조사와 대응에 사용한다.

## 왜 eBPF 기반 런타임 모니터링인가

| 기존 통제 | 장점 | 부족한 지점 |
| --- | --- | --- |
| SAST, CI, 이미지 스캔 | 배포 전 코드·이미지 취약점 탐지 | 실행 중 명령, 파일 변경, C2 연결은 알기 어려움 |
| IDS/IPS/WAF | 경계 트래픽·알려진 패턴 탐지 | HTTP/2, HTTP/3, mTLS와 컨테이너 내부 행위의 가시성이 제한됨 |
| auditd 등 사용자 공간 수집 | 시스템 콜 감사 | 대량 이벤트에서 커널↔사용자 공간 전환과 로그 전송 부담이 커질 수 있음 |

Kubernetes의 컨테이너는 노드 커널을 공유한다. 따라서 `execve`, `connect`, `open`, `unlink`, `sendmsg` 같은 커널 이벤트를 보고, 이를 실행한 프로세스와 컨테이너/Pod를 연결하면 웹셸, 명령 주입 후속 행위, 리버스 셸, 의심스러운 외부 연결을 런타임에 탐지할 수 있다.

## eBPF란

eBPF(extended Berkeley Packet Filter)는 작은 프로그램을 커널의 지정된 훅에 안전하게 연결해 실행하는 Linux 기술이다.

```text
eBPF 소스(C/Rust) → LLVM 바이트코드 → loader/libbpf → verifier 검사 → JIT 컴파일
                                                           ↓
                                                 커널 훅에서 실행
                                      (syscall, tracepoint, LSM, network 등)
```

- **Verifier**: 메모리 접근, 종료 가능성, 허용 helper 사용 등을 검증한다.
- **JIT**: 검증된 바이트코드를 네이티브 명령어로 변환한다.
- **Map**: 커널과 사용자 공간 사이에서 상태·카운터·정책 데이터를 전달한다.
- eBPF는 무제한 권한의 커널 모듈이 아니다. 허용된 프로그램 유형과 helper 범위에서만 동작한다.

자세한 기반 구조는 [eBPF 공식 인프라 문서](https://ebpf.io/ko-kr/infrastructure/)를 참고한다.

## Falco 센서(스캐너) 아키텍처

발표에서 말하는 스캐너는 CVE를 검색하는 정적 취약점 스캐너가 아니라, 노드 커널 이벤트를 지속 수집하는 **Falco 런타임 센서**다.

```text
컨테이너 프로세스
  │ execve(), connect(), open(), unlink(), sendmsg() ...
  ▼
Linux kernel ── eBPF probe / kernel module
  ▼
libscap → libsinsp (이벤트 파싱, 프로세스·컨테이너·K8S 맥락 보강)
  ▼
Falco rule engine (기본 룰 + 자체 HIDS/FIM 룰)
  ├─ 탐지 로그
  └─ Falco Sidekick → Kafka → SIEM(Splunk) → SOC/Slack 알림

Git → CI → Argo CD → Alpha/Live 클러스터 룰 배포
```

예를 들어 웹 서비스 컨테이너에서 `cat /etc/passwd`가 실행되면 eBPF가 `execve()`를 관찰한다. Falco는 PID, 실행 파일, 인자, 사용자뿐 아니라 컨테이너 ID, 이미지, 컨테이너 이름을 더해 룰셋과 비교하고, 조건에 맞으면 경보를 보낸다.

## 배포 및 운영 방식

1. **Alpha 배포**: DaemonSet으로 노드에 센서를 배포하고 기본 룰과 자체 HIDS 룰을 튜닝한다.
2. **성능 관찰**: CPU, 메모리, 이벤트 드롭률, 경보 지연과 서비스 지연을 측정한다.
3. **GitOps**: 룰 변경을 Git, CI, Argo CD로 Alpha/Live에 단계 승격한다.
4. **중앙 분석**: Sidekick 출력은 Kafka와 SIEM으로 전달하고 SOC에는 조사 가능한 경보를 보낸다.

## 탐지 범위

| 영역 | 대표 이벤트 | 목적 |
| --- | --- | --- |
| HIDS·명령 실행 | `execve()` | 명령 주입, 웹셸, 예상 밖 쉘·관리 도구 실행 |
| 네트워크 | `connect()`, `sendto()`, `sendmsg()` | 리버스/바인드 셸, C2, 비정상 outbound |
| FIM | `open()`, `creat()`, `unlink()`, `rename()` | 중요 파일 읽기·생성·변조·삭제 |
| 소켓 이상 | `setsockopt()` | BPFDoor 계열의 비정상 소켓 동작 신호 |
| io_uring 우회 | `io_uring_setup`, `io_uring_register` | syscall 중심 감시 우회 시도 관찰 |
| K8S Audit | API 이벤트 | privileged Pod, hostPath/hostNetwork, 과도한 RBAC, 비밀정보 ConfigMap 탐지 |

## eBPF/Falco 사용 시 고려사항

### 성능과 안정성

- 모든 이벤트를 전송하지 말고, 필요한 훅과 필드만 선택해 가능한 한 일찍 필터링한다.
- 업무 피크에서 CPU, 메모리, 이벤트율, 드롭률, 경보 지연, 애플리케이션 p95/p99를 기준선과 비교한다.
- privileged DaemonSet은 서비스 영향이 크다. Canary/단계 배포, 리소스 제한, 롤백 경로를 준비한다.
- 커널 버전, BTF, `CONFIG_BPF`, `CONFIG_BPF_JIT`, cgroup/LSM 정책을 사전 점검한다. [Falco modern eBPF 설치 안내](https://falco.org/docs/installation/)도 확인한다.

### 호환성과 권한

- 커널·배포판마다 BTF와 eBPF 기능 범위가 다르다. CO-RE 기반 modern eBPF를 우선 검토하되 실제 노드에서 검증한다.
- 센서는 높은 권한을 필요로 한다. 최소 capability, 전용 ServiceAccount/RBAC, 이미지 서명·검증, 읽기 전용 파일시스템을 적용한다.
- Fargate나 Autopilot처럼 노드 커널 접근이 제한되는 환경에서는 DaemonSet 기반 eBPF 방식이 제한될 수 있다.

### 탐지 품질과 대응

- 기본 룰을 그대로 운영 경보로 쓰지 않는다. 서비스의 정상 행위를 macro/allowlist로 모델링하고 룰을 코드처럼 리뷰·테스트·버전 관리한다.
- Falco는 기본적으로 탐지·경보 도구다. 자동 격리/차단은 오탐의 업무 영향을 고려해 승인 절차와 감사 로그를 둔 별도 흐름으로 설계한다.
- raw syscall만으로는 조사 효율이 낮다. cluster, namespace, Pod, image digest, workload owner, 원격 IP, 위협 인텔리전스를 보강한다.
- 명령 인자와 파일 경로에 개인정보·비밀값이 포함될 수 있으므로 마스킹, 보존 기간, 접근 제어, SIEM 비용을 함께 설계한다.
- 단일 센서에 의존하지 않는다. 이미지 스캔, 네트워크 보안, K8S Audit, RBAC와 결합한다.

## 함께 검토할 프로젝트

| 프로젝트 | 적합한 경우 | 특징 |
| --- | --- | --- |
| [Falco](https://falco.org/docs/) | 룰 기반 K8S·호스트 런타임 탐지와 SIEM/SOC 연계 | 발표의 중심 도구. modern eBPF probe와 플러그인 이벤트 소스 지원 |
| [Cilium Tetragon](https://tetragon.io/docs/overview/) | K8S 인지형 프로세스·파일·네트워크 관찰 및 정책 집행 | eBPF 커널 내 필터링·반응과 runtime enforcement에 강점 |
| [Tracee](https://github.com/aquasecurity/tracee) | 포렌식, 행위 시그니처, eBPF 보안 연구 | 런타임 탐지와 포렌식 성격이 강함 |
| [Inspektor Gadget](https://inspektor-gadget.io/docs/main/) | 장애 분석, 임시 진단, 자체 eBPF 도구 배포 | OCI 이미지 형태의 eBPF Gadget을 K8S/Linux에서 실행 |
| [Cilium](https://cilium.io/) | eBPF CNI, 네트워크 정책, 네트워크 관측 | HIDS보다 네트워킹·네트워크 보안의 기반 |
| [Pixie](https://px.dev/) | 요청·프로토콜 중심의 애플리케이션 관측 | 보안 경보보다 개발자 관측성에 초점 |

## 확장 프로젝트 아이디어

- Falco 룰, K8S Audit 룰, TI 기반 outbound 판정을 묶은 **Kubernetes 런타임 탐지 플랫폼**
- privileged 실행, `setns`, hostPath 마운트, capability 상승을 상관 분석하는 **컨테이너 탈출 조기 경보**
- SBOM/CVE와 실제 프로세스·네트워크 노출을 결합해 우선순위를 매기는 **실행 기반 취약점 관리**
- namespace/workload별 정상 실행·파일·원격 연결을 학습하고 새 행위를 검토하는 **행위 베이스라인**

## 도입 체크리스트

- [ ] 노드 커널, BTF, eBPF/JIT, 관리형 Kubernetes 제한을 확인했다.
- [ ] DaemonSet의 권한과 이미지 공급망을 최소 권한으로 설계했다.
- [ ] Alpha/Canary, 단계 배포, 롤백 절차가 있다.
- [ ] CPU·메모리·드롭률·지연·SIEM 전송량 SLO가 있다.
- [ ] 룰을 Git에서 테스트·리뷰·승격한다.
- [ ] K8S Audit·컨테이너 메타데이터·TI를 경보에 보강한다.
- [ ] 오탐 처리와 자동 대응 승인 기준을 정했다.
- [ ] 로그의 마스킹·보존·접근 제어를 정했다.
