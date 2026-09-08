#!/usr/bin/env python3
"""Execute an independent Arm CPU oracle for the integer pair-memory contract."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def probe():
    source = Path(__file__).with_name("pair_memory_probe.S")
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    records = []
    with tempfile.TemporaryDirectory(prefix="nextcore-pair-") as directory:
        temp = Path(directory)
        obj, elf = temp / "probe.o", temp / "probe.elf"
        commands = [
            ["clang", "--target=aarch64-none-elf", "-c", str(source), "-o", str(obj)],
            ["ld.lld", "-Ttext=0x40080000", "-e", "_start", str(obj), "-o", str(elf)],
            ["qemu-system-aarch64", "-machine", "virt", "-cpu", "max", "-m", "128",
             "-nographic", "-monitor", "none", "-serial", "none", "-net", "none",
             "-semihosting-config", "enable=on,target=native", "-kernel", str(elf)],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            records.append({"command": command, "exit_code": result.returncode,
                            "stdout": result.stdout, "stderr": result.stderr})
            if result.returncode:
                raise RuntimeError(json.dumps(records[-1]))
        lines = (records[-1]["stdout"] + records[-1]["stderr"]).splitlines()
        if lines != ["PAIR_ORACLE: PASS widths,modes,sp,zr,alignment"]:
            raise RuntimeError("Unexpected pair-memory oracle output: " + repr(lines))
        elf_hash = hashlib.sha256(elf.read_bytes()).hexdigest()
    assert source_hash == hashlib.sha256(source.read_bytes()).hexdigest()
    return {"schema": "nextcore.pair-memory-oracle/1", "passed": True,
            "source_sha256": source_hash, "elf_sha256": elf_hash,
            "commands": records, "apple_assets_used": False,
            "sp_alignment_oracle_verified": False,
            "sp_alignment_limitation": "QEMU gen_check_sp_alignment omits SA/SA0 checks",
            "macos_boot_verified": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = probe()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
