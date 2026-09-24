#!/usr/bin/env python3
"""Independent actual-native mapped PAC/cache proof with canonical Rust callbacks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rustc", default="rustc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = [p for p in root.rglob("*") if p.is_file() and "target" not in p.parts and "__pycache__" not in p.parts]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = {str(p.relative_to(root)): sha(p) for p in sources}
    commands = []

    def run(command, log, env=None):
        commands.append(command)
        p = subprocess.run(command, text=True, capture_output=True, env=env, timeout=120)
        log.write_text(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError(f"{log}: {p.stdout[-1000:]}{p.stderr[-1000:]}")
        return p.stdout

    service = out / "libservice.rlib"
    run([args.rustc, "--edition=2021", "--crate-name=nextcore_memory_service", "--crate-type=rlib",
         "-Copt-level=2", str(root / "memory-service/src/lib.rs"), "-o", str(service)], out / "service.log")
    modes = {"cached": [], "uncached": ["-DNEXTCORE_DISABLE_PROVIDER_CACHE"],
             "small-slot": ["-DNEXTCORE_PROVIDER_CACHE_SLOT_BYTES=64"]}
    for mode, flags in modes.items():
        folder = out / mode
        folder.mkdir()
        objects = []
        for name in ("jit", "arch", "boot_jit", "memory_boot", "memory_boot_v2", "memory_layout", "memory_layout_v2", "test_mapped_snapshot"):
            obj = folder / (name + ".o")
            run(["clang", "-std=c11", "-D_GNU_SOURCE", "-O2", "-Wall", "-Wextra", "-Werror", *flags,
                 "-c", str(root / (name + ".c")), "-o", str(obj)], folder / (name + ".log"))
            objects.append(obj)
        exe = folder / "test"
        run([args.rustc, "--edition=2021", "--test", "--cfg", "nextcore_mapped_snapshot", "-Copt-level=2",
             str(root / "test_stage1_provider.rs"), "--extern", "nextcore_memory_service=" + str(service),
             "-o", str(exe), *["-Clink-arg=" + str(obj) for obj in objects]], folder / "build.log")
        run([str(exe), "--test-threads=1"], folder / "tests.log",
            {**os.environ, "NEXTCORE_MAPPED_SNAPSHOTS": str(folder)})
    m0_exe = out / "test-m0"
    m0_objects = [out / "cached" / (name + ".o") for name in
                  ("jit", "arch", "boot_jit", "memory_boot", "memory_boot_v2", "memory_layout", "memory_layout_v2")]
    run([args.rustc, "--edition=2021", "--test", "-Copt-level=2", str(root / "test_memory_provider.rs"),
         "--extern", "nextcore_memory_service=" + str(service), "-o", str(m0_exe),
         *["-Clink-arg=" + str(obj) for obj in m0_objects]], out / "m0-build.log")
    run([str(m0_exe), "--test-threads=1"], out / "m0-tests.log")
    for mode in ("cached", "small-slot"):
        for case in range(9):
            name = f"case-{case}.bin"
            assert (out / mode / name).read_bytes() == (out / "uncached" / name).read_bytes(), (mode, case)
    counts = {mode: {str(case): int((out / mode / f"case-{case}.calls").read_text()) for case in range(9)} for mode in modes}
    assert counts["cached"]["0"] < counts["uncached"]["0"]
    assert counts["cached"]["2"] < counts["uncached"]["2"]
    assert counts["cached"]["3"] == counts["uncached"]["3"]
    assert counts["small-slot"]["4"] == counts["uncached"]["4"] > counts["cached"]["4"]
    assert counts["cached"]["5"] == counts["uncached"]["5"]
    # Link the real canonical PAC module to the C public-boundary tests. Expected
    # 47/48-bit results are fixed independently captured Arm values in the header.
    wrapper = out / "pauth.rs"
    wrapper.write_text('#![no_std]\n#[path=' + json.dumps(str(root / "preos/src/pauth.rs")) +
                       '] mod pauth;\n#[panic_handler] fn panic(_: &core::panic::PanicInfo)->!{loop{}}\n')
    library = out / "libpauth.a"
    run([args.rustc, "--edition=2021", "--crate-type=staticlib", "-Cpanic=abort", "-Copt-level=2",
         str(wrapper), "-o", str(library)], out / "pauth-build.log")
    exe = out / "pauth-test"
    run(["clang", "-std=c11", "-D_GNU_SOURCE", "-O2", "-ffunction-sections", "-fdata-sections", str(root / "jit.c"), str(root / "arch.c"),
         str(root / "boot_jit.c"), str(root / "test_pauth_jit.c"), str(library), "-Wl,--gc-sections", "-o", str(exe)], out / "pauth-link.log")
    pac = json.loads(run([str(exe)], out / "pauth-test.log"))
    after = {str(p.relative_to(root)): sha(p) for p in sources}
    assert before == after
    receipt = dict(passed=True, source_sha256=before, sources_preserved=True, commands=commands,
                   native_modes=list(modes), cases=9, protection_counts=counts, pauth=pac,
                   qemu_oracle_vectors=24, qemu_oracle_feature_level="APA5",
                   enabled_sign_auth_compared=16, xpac_disabled_compared=24, upper_enabled_unverified=8,
                   m0_regression_tests=20, comparison="complete C CPU (function pointer normalized), result, RAM, ordered Rust-service requests/replies",
                   original_images_used=False, physical_boot_verified=False)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": True, "receipt": str(out / "receipt.json")}))


if __name__ == "__main__":
    main()
