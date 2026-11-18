use anyhow::Context as _;
use aya::programs::TracePoint;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    /*
     * 최신 커널은 보통 cgroup memory accounting을 사용한다. 실패해도 아래
     * load/attach가 실제 결과를 알려 주므로, 이 보정 실패만으로 중단하지 않는다.
     */
    let limit = libc::rlimit {
        rlim_cur: libc::RLIM_INFINITY,
        rlim_max: libc::RLIM_INFINITY,
    };
    if unsafe { libc::setrlimit(libc::RLIMIT_MEMLOCK, &limit) } != 0 {
        eprintln!("warning: could not remove the memlock limit");
    }

    let mut ebpf = aya::Ebpf::load(aya::include_bytes_aligned!(concat!(
        env!("OUT_DIR"),
        "/execmon-rust"
    )))
    .context("failed to load the embedded eBPF object")?;

    let program: &mut TracePoint = ebpf
        .program_mut("execmon_enter")
        .context("execmon_enter program is missing from the eBPF object")?
        .try_into()
        .context("execmon_enter is not a tracepoint program")?;
    program.load().context("failed to load execmon_enter")?;
    program
        .attach("syscalls", "sys_enter_execve")
        .context("failed to attach sys_enter_execve")?;

    println!("execmon-rust attached; press Ctrl-C to stop");
    tokio::signal::ctrl_c()
        .await
        .context("failed to wait for Ctrl-C")?;
    println!("execmon-rust stopping");

    Ok(())
}
