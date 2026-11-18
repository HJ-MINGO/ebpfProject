use anyhow::{Context as _, anyhow};
use aya_build::Toolchain;

fn main() -> anyhow::Result<()> {
    let cargo_metadata::Metadata { packages, .. } = cargo_metadata::MetadataCommand::new()
        .no_deps()
        .exec()
        .context("failed to read Cargo workspace metadata")?;

    let ebpf_package = packages
        .into_iter()
        .find(|package| package.name == "execmon-rust-ebpf")
        .ok_or_else(|| anyhow!("execmon-rust-ebpf package not found"))?;

    let ebpf_package = aya_build::Package {
        name: ebpf_package.name.as_str(),
        root_dir: ebpf_package
            .manifest_path
            .parent()
            .ok_or_else(|| anyhow!("eBPF manifest has no parent directory"))?
            .as_str(),
        ..Default::default()
    };

    aya_build::build_ebpf([ebpf_package], Toolchain::default())
}
