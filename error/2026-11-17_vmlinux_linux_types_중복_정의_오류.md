# `vmlinux.h`와 `linux/types.h` 중복 타입 정의 오류

작성일: 2026-11-17

## 증상

`execmon.bpf.c`를 BPF object로 컴파일할 때 `typedef redefinition with different types` 오류가 발생했다.

대표 오류는 다음과 같다.

```text
/usr/include/linux/posix_types.h:27:3: error: typedef redefinition with different types
src/vmlinux.h:95096:3: note: previous definition is here
```

## 실행한 명령

```bash
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 \
  -I/usr/include/$(uname -m)-linux-gnu -Isrc \
  -c src/execmon.bpf.c -o build/execmon.bpf.o
```

## 확인된 사실

- `execmon.bpf.c`는 `vmlinux.h`를 먼저 include한다.
- `vmlinux.h`는 BTF에서 생성한 커널 타입 정의이며 `__u32`, `__kernel_fd_set` 등의 타입을 포함한다.
- 공용 헤더 `execmon.h`가 이어서 `<linux/types.h>`를 include하면서 같은 타입을 다시 정의하려 했다.

## 원인

하나의 BPF 컴파일 단위에 BTF 기반 `vmlinux.h`와 Linux UAPI 타입 헤더를 함께 무조건 include해 타입 정의가 충돌했다.

## 해결 조치

- `execmon.bpf.c`가 공용 헤더를 include하기 전에 `EXECMON_BPF`를 정의했다.
- `execmon.h`는 BPF 컴파일일 때 `<linux/types.h>`를 건너뛰고, 사용자 공간 loader 컴파일일 때만 포함하도록 수정했다.
- BPF 쪽 타입은 `vmlinux.h`, loader 쪽 타입은 `<linux/types.h>`에서 각각 제공받는다.

## 후속 확인 항목

- 수정한 두 소스를 VM에 반영한 뒤 같은 clang 명령이 성공하는지 확인한다.
- 성공하면 skeleton 생성과 loader 빌드를 계속 진행한다.
