#!/usr/bin/env python3
"""Run authored FP/SIMD, C/Rust provider and terminal ABI checks on Linux x86.

Builds run sequentially with installed Clang and Rust. This verifier installs
nothing, uses no original media and starts no guest. It works from a standalone
ISE checkout as well as the combined workspace. Receipts bind the checked source.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import subprocess
import tempfile
import time


SERVICES = (
    "fp_scalar", "fp_guest", "fp_instruction", "fp_control", "simd_bitwise",
    "simd_memory", "simd_copy", "simd_transfer", "fp_execution",
)
FIXTURES = (*SERVICES, "fp_arch")
BRIDGE_DEVICES = (
    "aic_v1", "adp_display_v1", "ans_v1", "ans_pci_v1", "ans_mbox_v1",
    "ans_autoboot_v1", "ascwrap_v1", "apple_gxf_v1", "apple_uart_v1",
    "apple_smc_v1", "dart_v1", "nvram_v1", "recovery_v1", "sart_v1",
    "sep_mailbox_v1", "storage_v1", "timer_v1",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(module: Path) -> dict[str, str]:
    paths = [Path(__file__).resolve()]
    for directory in (module / "runtime", module / "devices"):
        paths.extend(path for path in directory.rglob("*")
                     if path.is_file() and path.suffix in (".c", ".h", ".inc", ".rs", ".toml")
                     and "target" not in path.relative_to(directory).parts)
    return {str(path.relative_to(module)): digest(path) for path in sorted(paths)}


class Checks:
    def __init__(self, output: Path, timeout: int):
        self.output = output
        self.timeout = timeout
        self.records: list[dict] = []
        self.results: dict[str, dict] = {}

    def run(self, name: str, command: list[str | Path]) -> str:
        command = list(map(str, command))
        index = len(self.records)
        stdout = self.output / f"{index:03d}-{name}.stdout.log"
        stderr = self.output / f"{index:03d}-{name}.stderr.log"
        started = time.monotonic()
        environment = os.environ.copy()
        environment.update(CARGO_BUILD_JOBS="1", RUST_BACKTRACE="0")
        status, timed_out = -1, False
        try:
            with stdout.open("w") as out, stderr.open("w") as err:
                result = subprocess.run(command, stdout=out, stderr=err,
                                        env=environment, timeout=self.timeout)
                status = result.returncode
        except subprocess.TimeoutExpired:
            timed_out = True
        self.records.append({"name": name, "command": command, "exit_code": status,
                             "timed_out": timed_out,
                             "elapsed_seconds": round(time.monotonic() - started, 3),
                             "stdout": str(stdout), "stderr": str(stderr)})
        if status or timed_out:
            raise RuntimeError(f"{name} failed: exit {status}; see {stderr} and {stdout}")
        return stdout.read_text()

    def execute(self, name: str, executable: Path, *arguments: str) -> dict:
        output = self.run(name, [executable, *arguments])
        try:
            result = json.loads(output)
        except json.JSONDecodeError:
            match = re.search(r"test result: ok\. (\d+) passed; (\d+) failed", output)
            if match:
                result = {"passed": int(match[1]) > 0 and int(match[2]) == 0,
                          "tests_passed": int(match[1])}
            else:
                match = re.search(r"(\d+) assertions", output)
                if not match or int(match[1]) <= 0:
                    raise RuntimeError(f"{name} emitted no recognized success receipt")
                result = {"passed": True, "assertions": int(match[1])}
        if result.get("passed") is not True:
            raise RuntimeError(f"{name} did not report success")
        self.results[name] = result
        print(json.dumps({"check": name, **result}), flush=True)
        return result


def verify(module: Path, build: Path, checks: Checks, clang: str, rustc: str,
           suite: str) -> None:
    runtime, devices = module / "runtime", module / "devices"
    common = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
              "-I", str(runtime), "-I", str(devices)]
    service_flags = ["-ffreestanding", "-fno-builtin", "-mno-red-zone",
                     "-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float"]
    objects: dict[tuple[str, str], Path] = {}

    def compile_object(mode: str, name: str, research: bool = False) -> Path:
        key = (mode, name + ("-research" if research else ""))
        if key in objects:
            return objects[key]
        obj = build / f"{key[0]}-{key[1]}.o"
        extra = (["-fsanitize=undefined", "-fno-sanitize-recover=all"]
                 if mode == "ubsan" else [])
        flags = [*common, "-D_GNU_SOURCE", *extra]
        if mode == "ubsan" and name == "jit":
            # Emitted native entries have no compiler function-type prefix.
            # Clang's function check reads before the RX page and is inapplicable
            # to this machine-code boundary; all other UBSan checks stay enabled.
            flags += ["-fno-sanitize=function"]
        if name in SERVICES:
            flags += service_flags
        if research:
            flags += ["-DNEXTCORE_FP_EXECUTION=1"]
        checks.run(f"compile-{mode}-{key[1]}",
                   [clang, *flags, "-c", runtime / f"{name}.c", "-o", obj])
        objects[key] = obj
        return obj

    for mode in (("native", "ubsan") if suite in ("all", "services") else ()):
        sanitize = (["-fsanitize=undefined", "-fno-sanitize-recover=all"]
                    if mode == "ubsan" else [])
        service_objects = [compile_object(mode, name) for name in SERVICES]
        base_objects = [compile_object(mode, name) for name in ("jit", "arch", "boot_jit")]
        for fixture in FIXTURES:
            name = f"{mode}-{fixture}"
            executable = build / name
            # The transfer fixture supplies its own _GNU_SOURCE definition.
            flags = [*common, *sanitize]
            if fixture != "simd_transfer":
                flags += ["-D_GNU_SOURCE"]
            selected = base_objects
            if fixture == "fp_execution":
                flags += ["-DNEXTCORE_FP_EXECUTION=1"]
                selected = [compile_object(mode, "jit", True), *base_objects[1:]]
            if fixture == "fp_scalar":
                # Hosted floating-point is used only by the independent oracle.
                flags += ["-frounding-math", "-ffp-contract=off", "-fno-fast-math"]
            checks.run(f"link-{name}", [clang, *flags, runtime / f"test_{fixture}.c",
                                       *service_objects, *selected, "-lm", "-o", executable])
            checks.execute(name, executable)

    # Compile the exact memory-service implementation for actual C/Rust callbacks.
    library = build / "libnextcore_memory_service.rlib"
    if suite in ("all", "providers"):
        checks.run("memory-service-library", [rustc, "--edition=2021", "--crate-type=rlib",
                   "--crate-name=nextcore_memory_service", "-Copt-level=2",
                   runtime / "memory-service/src/lib.rs", "-o", library])
    for research in ((False, True) if suite in ("all", "providers") else ()):
        name = "fp-provider-research" if research else "fp-provider-scalar"
        linked = [compile_object("native", "jit", research),
                  compile_object("native", "arch"), compile_object("native", "boot_jit"),
                  compile_object("native", "memory_boot"),
                  compile_object("native", "memory_boot_v2", research),
                  *[compile_object("native", source) for source in SERVICES]]
        executable = build / name
        command = [rustc, "--edition=2021", "--test", "-Copt-level=2",
                   runtime / "test_fp_provider.rs", "--extern",
                   f"nextcore_memory_service={library}", "-o", executable,
                   *[f"-Clink-arg={obj}" for obj in linked]]
        if research:
            command += ["--cfg", "nextcore_fp_execution"]
        checks.run(f"link-{name}", command)
        checks.execute(name, executable, "--nocapture", "--test-threads=1")

    if suite not in ("all", "terminal"):
        return
    # The reference suite checks the appended terminal reason and fixed layouts.
    reference = build / "preos-reference"
    checks.run("compile-preos-reference", [rustc, "--edition=2021", "--test",
               runtime / "preos/src/lib.rs", "-o", reference])
    checks.execute("preos-reference", reference, "--nocapture", "--test-threads=1")
    reference_stdout = Path(checks.records[-1]["stdout"]).read_text()
    for name, marker in (("abi_layout", "VF_ABI_LAYOUT"),
                         ("platform_layout", "VF_PLATFORM_ABI")):
        executable = build / name
        checks.run(f"compile-{name}", [clang, *common, runtime / f"{name}.c",
                                      "-o", executable])
        c_layout = json.loads(checks.run(name, [executable]))
        layouts = [json.loads(line.split(marker + " ", 1)[1])
                   for line in reference_stdout.splitlines() if marker + " " in line]
        if layouts != [c_layout]:
            raise RuntimeError(f"C/Rust {name} mismatch")
        checks.results[name] = {"passed": True, "values": c_layout}

    staticlib = build / "libvenfire_preos.a"
    checks.run("preos-static-library", [rustc, "--edition=2021", "--crate-type=staticlib",
               "--crate-name=venfire_preos", "-Cpanic=abort", "-Copt-level=2",
               runtime / "preos/src/lib.rs", "-o", staticlib])
    bridge = build / "terminal-wrapper"
    checks.run("link-terminal-wrapper", [clang, *common, "-msse4.2", "-mno-avx",
               runtime / "jit.c", runtime / "arch.c", runtime / "preos_bridge.c",
               *[devices / f"{name}.c" for name in BRIDGE_DEVICES],
               runtime / "preos_host_test.c", staticlib,
               "-Wl,--gc-sections", "-o", bridge])
    result = checks.execute("terminal-wrapper", bridge)
    if result.get("c_rust_abi_executed") is not True:
        raise RuntimeError("terminal wrapper did not execute the actual C/Rust ABI")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True, help="fresh receipt/log directory")
    parser.add_argument("--work-dir", type=Path, help="parent for temporary build files")
    parser.add_argument("--clang", default=shutil.which("clang") or "clang")
    parser.add_argument("--rustc", default=shutil.which("rustc") or "rustc")
    parser.add_argument("--timeout", type=int, default=180, help="per-command deadline in seconds")
    parser.add_argument("--suite", choices=("all", "services", "providers", "terminal"),
                        default="all", help="default runs every required suite")
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("native execution requires Linux x86_64")
    if not 1 <= args.timeout <= 600:
        parser.error("--timeout must be between 1 and 600 seconds")
    module = args.module.resolve(strict=True)
    # Ensure a requested module override identifies this runner's source tree.
    if module != Path(__file__).resolve().parents[1]:
        parser.error("--module must identify this runner's own checkout")
    output = args.output.resolve()
    if output.exists():
        parser.error("--output must not already exist")
    output.mkdir(parents=True)
    if args.work_dir:
        args.work_dir.mkdir(parents=True, exist_ok=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    checks = Checks(output, args.timeout)
    before = source_hashes(module)
    report = {"schema": "nextcore.fp-simd-native-regression/1", "passed": False,
              "host": {"system": platform.system(), "machine": platform.machine()},
              "source_sha256": before, "compile_jobs": 1,
              "suite": args.suite,
              "sanitizer_exclusions": {"jit.c": ["function-type metadata for emitted native entries"]},
              "guest_started": False, "original_inputs_used": False,
              "macos_boot_verified": False, "metal_verified": False}
    build = None
    try:
        report["compiler_version"] = checks.run("clang-version", [args.clang, "--version"]).splitlines()[0]
        report["rustc_version"] = checks.run("rustc-version", [args.rustc, "--version"]).strip()
        with tempfile.TemporaryDirectory(prefix="nextcore-fp-simd-", dir=args.work_dir) as temporary:
            build = Path(temporary)
            verify(module, build, checks, args.clang, args.rustc, args.suite)
        report["passed"] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        report["sources_preserved"] = before == source_hashes(module)
        report["temporary_files_removed"] = build is not None and not build.exists()
        report["passed"] = report["passed"] and report["sources_preserved"] and report["temporary_files_removed"]
        report["tests"] = checks.results
        report["commands"] = checks.records
        receipt = output / "receipt.json"
        receipt.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "receipt": str(receipt),
                      **({"error": report["error"]} if "error" in report else {})}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
