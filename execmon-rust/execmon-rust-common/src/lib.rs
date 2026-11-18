#![no_std]

/*
 * 이 구조체는 다음 단계에서 eBPF crate와 사용자 공간 loader가 함께 사용할
 * 고정 바이트 배치다. repr(C)는 Rust 기본 배치 대신 C와 같은 필드 순서와 정렬을
 * 사용하도록 지정한다. 아직 map에 쓰거나 읽지 않으므로 ABI는 실제 이벤트 추가
 * 단계에서 크기와 NUL 종료 규칙까지 검증한다.
 */
#[repr(C)]
#[derive(Copy, Clone)]
pub struct ExecEvent {
    pub pid: u32,
    pub ppid: u32,
    pub uid: u32,
    pub comm: [u8; 16],
    pub filename: [u8; 256],
}

/* Aya의 사용자 공간 RingBuf reader가 이 구조체를 raw bytes로 읽을 때만 필요하다. */
#[cfg(feature = "user")]
unsafe impl aya::Pod for ExecEvent {}
