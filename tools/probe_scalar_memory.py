#!/usr/bin/env python3
"""Independently execute scalar ISA fixtures and a rejected negative control."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def probe():
    source = Path(__file__).with_name("scalar_memory_probe.S")
    original = source.read_bytes()
    records = []
    hashes = {}
    with tempfile.TemporaryDirectory(prefix="nextcore-scalar-") as directory:
        temp = Path(directory)
        for negative in (False, True):
            stem = "negative" if negative else "scalar"
            assembly, obj, elf = (temp / (stem + suffix) for suffix in (".S", ".o", ".elf"))
            content = original.decode()
            if negative:
                # Change only the expected signed-X byte value, leaving the
                # executed LDRSB and all other memory instructions intact.
                assert content.count("mov x10, #-128") == 1
                content = content.replace("mov x10, #-128", "mov x10, #128")
            assembly.write_text(content)
            commands = [
                ["clang", "--target=aarch64-none-elf", "-c", str(assembly), "-o", str(obj)],
                ["ld.lld", "-Ttext=0x40080000", "-e", "_start", str(obj), "-o", str(elf)],
                ["qemu-system-aarch64", "-machine", "virt", "-cpu", "max", "-m", "128",
                 "-nographic", "-monitor", "none", "-serial", "none", "-net", "none",
                 "-semihosting-config", "enable=on,target=native", "-kernel", str(elf)],
            ]
            for index, command in enumerate(commands):
                result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                record = {"case": stem, "command": command, "exit_code": result.returncode,
                          "stdout": result.stdout, "stderr": result.stderr}
                records.append(record)
                expected = 1 if negative and index == 2 else 0
                if result.returncode != expected:
                    raise RuntimeError(json.dumps(record))
            lines = (result.stdout + result.stderr).splitlines()
            expected = "SCALAR_ORACLE: FAIL" if negative else "SCALAR_ORACLE: PASS 13forms,imm12,sign,sp,zr,alignment"
            if lines != [expected]:
                raise RuntimeError("Unexpected scalar oracle output: " + repr(lines))
            hashes[stem] = hashlib.sha256(elf.read_bytes()).hexdigest()
    assert source.read_bytes() == original
    return {"schema": "nextcore.scalar-memory-oracle/1", "passed": True,
            "source_sha256": hashlib.sha256(original).hexdigest(), "elf_sha256": hashes,
            "commands": records, "negative_control_rejected": True,
            "sp_alignment_oracle_verified": False,
            "mmu_off_device_alignment_oracle_verified": False,
            "alignment_limitations": "QEMU omits SP SA checks and observed MMU-off A=0 Device alignment; explicit A=1 data faults are checked",
            "apple_assets_used": False, "macos_boot_verified": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.exists():
        parser.error("Refusing to overwrite an existing receipt")
    result = probe()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
