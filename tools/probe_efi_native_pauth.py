#!/usr/bin/env python3
"""Build and execute the actual x86 EFI JIT with its no_std software PAC provider.

Linux x86_64 development proof: caller RAM and mprotect exercise the same C/Rust
runtime sources linked by NXARMJIT. This does not assert macOS boot completion.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def probe(runtime: Path, clang: str, rustc: str) -> dict:
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
        raise ValueError("native execution proof requires Linux x86_64 (including WSL2)")
    runtime = runtime.resolve(strict=True)
    sources = [runtime / name for name in (
        "jit.c", "jit.h", "arch.c", "boot_jit.c", "boot_jit.h",
        "test_jit.c", "test_boot_jit.c", "test_pauth_jit.c", "test_flags_jit.c",
        "preos/src/pauth.rs",
    )]
    for path in sources:
        if not path.is_file():
            raise ValueError(f"missing runtime source: {path}")
    records = []

    def run(command: list[str]) -> str:
        result = subprocess.run(command, text=True, capture_output=True, timeout=120)
        records.append({"command": command, "returncode": result.returncode,
                        "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode:
            raise RuntimeError(f"command failed: {command!r}\n{result.stdout}{result.stderr}")
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="nextcore-native-pauth-") as directory:
        temporary = Path(directory)
        wrapper = temporary / "pauth_provider.rs"
        # JSON quoting is Rust-compatible for ordinary POSIX paths, and avoids
        # treating the caller's source directory as generated source syntax.
        wrapper.write_text("#![no_std]\n#[allow(dead_code)]\n#[path=" +
            json.dumps(str(runtime / "preos/src/pauth.rs")) + "]\nmod pauth;\n" +
            "#[panic_handler]\nfn panic(_: &core::panic::PanicInfo) -> ! { loop {} }\n")
        provider = temporary / "libpauth.a"
        run([rustc, "--edition=2021", "--crate-type=staticlib", "-C", "panic=abort",
             "-C", "opt-level=2", str(wrapper), "-o", str(provider)])
        tests = {}
        for name, needs_boot, needs_pauth in (
            ("test_jit", False, False), ("test_boot_jit", True, False),
            ("test_pauth_jit", True, True), ("test_flags_jit", True, False),
        ):
            executable = temporary / name
            command = [clang, "-std=c11", "-D_GNU_SOURCE", "-O2", "-Wall", "-Wextra",
                "-ffunction-sections", "-fdata-sections", str(runtime / "jit.c"),
                str(runtime / "arch.c")]
            if needs_boot:
                command.append(str(runtime / "boot_jit.c"))
            command.append(str(runtime / (name + ".c")))
            if needs_pauth:
                command.append(str(provider))
            command += ["-Wl,--gc-sections", "-o", str(executable)]
            run(command)
            result = json.loads(run([str(executable)]))
            if result.get("passed") is not True or result.get("native_jit_executed") is not True:
                raise RuntimeError(f"native test did not report success: {result}")
            tests[name] = {**result, "executable_sha256": digest(executable)}
        primitive_tests = temporary / "qarma5_tests"
        run([rustc, "--edition=2021", "--test", str(runtime / "preos/src/pauth.rs"),
             "-o", str(primitive_tests)])
        run([str(primitive_tests)])
    return {
        "schema": "nextcore.efi-native-pauth-proof/1", "passed": True,
        "host": {"system": platform.system(), "machine": platform.machine()},
        "compiler_versions": {"clang": run([clang, "--version"]).splitlines()[0],
                              "rustc": run([rustc, "--version"]).strip()},
        "runtime_sources": {str(path.relative_to(runtime)): digest(path) for path in sources},
        "tests": tests, "commands": records, "macos_boot_verified": False,
        "scope": "Native x86_64 C JIT, NZCV/branches, explicit registers, nonzero physical RAM, QARMA5 callback and W^X",
        "limitations": ["EL1 PAuth, 48-bit address ranges, TBI disabled",
                        "C JIT stops when the guest enables its MMU",
                        "PACGA and enhanced PAuth instruction features unadvertised"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=Path(__file__).resolve().parents[1] /
                        "runtime")
    parser.add_argument("--clang", default=shutil.which("clang") or "clang")
    parser.add_argument("--rustc", default=shutil.which("rustc") or "rustc")
    parser.add_argument("--output", type=Path, help="fresh JSON receipt path")
    args = parser.parse_args()
    if args.output and args.output.exists():
        parser.error("--output must not already exist")
    try:
        report = probe(args.runtime, args.clang, args.rustc)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report = {"schema": "nextcore.efi-native-pauth-proof/1", "passed": False,
                  "error": str(error), "macos_boot_verified": False}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x") as output:
            json.dump(report, output, indent=2)
            output.write("\n")
    print(json.dumps({key: report[key] for key in ("schema", "passed", "macos_boot_verified")}
                     | ({"error": report["error"]} if "error" in report else {})))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
