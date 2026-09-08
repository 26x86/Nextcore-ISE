#!/usr/bin/env python3
"""Check independent Arm 4K/16K upper-address translations in QEMU's CPU model."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

def probe():
    source = Path(__file__).with_name("mmu_granule_probe.S")
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    records = []
    with tempfile.TemporaryDirectory(prefix="nextcore-mmu-oracle-") as directory:
        temporary = Path(directory)
        obj, elf = temporary / "probe.o", temporary / "probe.elf"
        commands = [
            ["clang", "--target=aarch64-none-elf", "-c", str(source), "-o", str(obj)],
            ["ld.lld", "-Ttext=0x40080000", "-e", "_start", str(obj), "-o", str(elf)],
            ["qemu-system-aarch64", "-machine", "virt", "-cpu", "max", "-m", "128",
             "-nographic", "-monitor", "none", "-serial", "none", "-net", "none",
             "-semihosting-config", "enable=on,target=native", "-kernel", str(elf)],
        ]
        for command in commands:
            result = subprocess.run(command, text=True, capture_output=True, timeout=20)
            records.append({"command": command, "exit_code": result.returncode,
                            "stdout": result.stdout, "stderr": result.stderr})
            if result.returncode:
                raise RuntimeError(json.dumps(records[-1]))
        text = records[-1]["stdout"] + records[-1]["stderr"]
        for marker in ("MMU_ORACLE: 4K_TTBR1_PA_OK", "MMU_ORACLE: 16K_TTBR1_PA_OK"):
            if marker not in text:
                raise RuntimeError("missing architectural outcome: " + marker)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == before
    return {"schema": "nextcore.mmu-granule-oracle/1", "passed": True,
            "source_sha256": before, "apple_assets_used": False,
            "native_jit_mmu_verified": False, "commands": records}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = probe()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
