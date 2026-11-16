# 2026-10-13 — libbpf-bootstrap 전체 빌드 실패

## 증상

Ubuntu 20.04, Linux 5.4.0-216-generic 서버에서 libbpf-bootstrap의 C 예제를 전체 빌드하기 위해 make를 실행했다.

    git clone --recurse-submodules https://github.com/libbpf/libbpf-bootstrap.git
    cd libbpf-bootstrap/examples/c
    make

빌드 중 minimal_ns 예제에서 다음 오류가 발생했다.

    libbpf: failed to find BTF info for global/extern symbol 'dev'
    Error: failed to link '.output/minimal_ns.tmp.bpf.o': No such file or directory (2)
    make: *** [Makefile:109: .output/minimal_ns.bpf.o] Error 254

## 확인된 원인

- /opt에 저장한 경로는 원인이 아니다. Makefile은 현재 소스 디렉터리 아래의 .output 경로를 사용하므로 /opt에서도 정상적으로 빌드할 수 있다.
- configure를 실행하지 않은 문제도 아니다. libbpf-bootstrap은 예제별 Makefile 빌드 방식을 제공한다.
- make는 bootstrap 하나만 빌드하는 명령이 아니라 모든 C 예제를 대상으로 한다.
- 최신 libbpf-bootstrap의 minimal_ns 예제는 현재 Ubuntu 20.04/커널 5.4 도구 조합에서 필요한 BTF 메타데이터를 만들지 못해 BPF 객체 정적 링크 단계에서 실패했다.
- 현재 커널 5.4는 ring buffer도 지원하지 않으므로 기본 bootstrap 예제를 그대로 실행할 수 없는 제약이 있다.

## 해결 방향

먼저 가장 호환성이 높은 minimal_legacy 예제만 개별 빌드한다.

    cd /opt/libbpf-bootstrap/examples/c
    make minimal_legacy
    sudo ./minimal_legacy
    sudo cat /sys/kernel/debug/tracing/trace_pipe

다음으로 BTF 지원 여부를 확인한다.

    test -r /sys/kernel/btf/vmlinux && echo 'BTF available' || echo 'BTF unavailable'
    /usr/sbin/bpftool version

BTF가 확인되면 ring buffer 대신 perf event array를 사용하는 bootstrap_legacy를 별도로 시도한다.

    make bootstrap_legacy
    sudo ./bootstrap_legacy

## 학습 메모

- make: Makefile에 정의된 빌드 규칙을 실행한다.
- make install: 빌드 결과를 시스템 경로에 복사하는 규칙이 있을 때만 사용한다.
- libbpf-bootstrap의 ./bootstrap, ./minimal_legacy는 설치 프로그램이 아니라 현재 디렉터리에서 실행하는 학습용 예제 바이너리다.

