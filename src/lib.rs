pub mod decoder;
pub mod emulator;
pub mod trap;
pub mod cpu_features;

/// Freestanding ARM64-to-x86 runtime sources shipped with this module.
/// Used by dependent EFI build scripts; no host runtime dependency is introduced.
pub const EFI_RUNTIME_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/runtime");
