#![no_std]
#![no_main]

use aya_ebpf::{macros::tracepoint, programs::TracePointContext};

/*
 * tracepoint는 커널이 관찰용으로 공개한 안정적인 이벤트 지점이다.
 * 이번 뼈대에서는 attach와 verifier 통과만 확인한다. 이벤트를 수집하거나
 * 사용자 공간으로 전송하지 않으므로 ctx는 다음 단계에서만 사용한다.
 */
#[tracepoint]
pub fn execmon_enter(_ctx: TracePointContext) -> u32 {
    0
}

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[unsafe(link_section = "license")]
#[unsafe(no_mangle)]
static LICENSE: [u8; 13] = *b"Dual MIT/GPL\0";
