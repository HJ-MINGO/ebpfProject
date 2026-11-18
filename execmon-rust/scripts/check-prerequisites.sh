#!/usr/bin/env bash
# Rust + Aya 뼈대를 빌드하기 전에 실습 VM에서만 실행한다.
# 이 스크립트는 상태를 읽고 결과를 보여 줄 뿐, 패키지 설치나 커널 설정 변경을 하지 않는다.
set -eu

required_command() {
    if command -v "$1" >/dev/null 2>&1; then
        printf '%-32s available\n' "$1"
    else
        printf '%-32s unavailable\n' "$1"
        return 1
    fi
}

printf '%s\n' '== Rust toolchain =='
required_command rustup
required_command cargo
required_command bpf-linker
rustc --version
cargo --version
rustup toolchain list
rustup run nightly rustc --version
rustup run nightly rustc --print sysroot

printf '%s\n' '== Kernel and eBPF features =='
uname -r
uname -m
test -r /sys/kernel/btf/vmlinux && echo 'BTF: available' || echo 'BTF: unavailable'
sudo test -r /sys/kernel/tracing/events/syscalls/sys_enter_execve/format \
    && echo 'sys_enter_execve tracepoint: available' \
    || echo 'sys_enter_execve tracepoint: unavailable'

required_command bpftool
if sudo bpftool feature probe kernel | grep -Eqi 'map_type.*ringbuf|ringbuf'; then
    echo 'ring buffer: available'
else
    echo 'ring buffer: unavailable'
fi

if sudo bpftool feature probe kernel | grep -q 'bpf_get_current_task_btf'; then
    echo 'current task BTF helper: available'
else
    echo 'current task BTF helper: unavailable'
fi
