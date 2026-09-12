#!/usr/bin/env python3
"""Execute authored PAC address-width vectors on QEMU's independent Arm CPU."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import time


def load(register, value):
    return [f"movz {register}, #{value & 65535}"] + [
        f"movk {register}, #{(value >> shift) & 65535}, lsl #{shift}" for shift in (16, 32, 48)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--cpu", default="max")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    qemu = shutil.which(args.qemu)
    assert qemu
    key_lo, key_hi = 0x48ad369c24681357, 0xb752c963db97eca8
    enable = (1 << 31) | (1 << 30) | (1 << 27) | (1 << 13)
    vectors = []
    source = [".arch armv8.3-a", ".text", ".global _start", "_start:",
              *load("x18", 0x40100000), "mrs x19, CurrentEL", "str x19, [x18], #8",
              "mrs x19, ID_AA64ISAR1_EL1", "str x19, [x18], #8"]
    for slot in ("APIA", "APIB", "APDA", "APDB"):
        source += [*load("x10", key_lo), *load("x11", key_hi),
                   f"msr {slot}KeyLo_EL1, x10", f"msr {slot}KeyHi_EL1, x11"]
    for bits in (47, 48):
        tcr = (64 - bits) | ((64 - bits) << 16) | (2 << 14) | (1 << 30) | (5 << 32)
        for index, suffix in enumerate(("ia", "ib", "da", "db")):
            for pointer in (0x130, (1 << (bits - 1)) | 0x130, ((1 << 64) - (1 << bits)) | 0x130):
                source += [*load("x10", tcr), "msr TCR_EL1, x10",
                           *load("x10", 0x30d00800 | enable), "msr SCTLR_EL1, x10", "isb",
                           *load("x1", pointer), *load("x2", 0x9876), "mov x3, x1",
                           f"pac{suffix} x3, x2", "str x3, [x18], #8", "mov x4, x3",
                           f"aut{suffix} x4, x2", "str x4, [x18], #8", "mov x4, x3",
                           f"xpac{'i' if index < 2 else 'd'} x4", "str x4, [x18], #8",
                           *load("x10", 0x30d00800 & ~enable), "msr SCTLR_EL1, x10", "isb",
                           "mov x4, x1", f"pac{suffix} x4, x2", "str x4, [x18], #8",
                           "mov x4, x3", f"aut{suffix} x4, x2", "str x4, [x18], #8"]
                vectors.append(dict(bits=bits, key=index, pointer=pointer, modifier=0x9876,
                                    key_lo=key_lo, key_hi=key_hi, tcr=tcr))
    source += ["mov x0, #0x64e", "1: b 1b"]
    assembly = out / "probe.S"
    assembly.write_text("\n".join(source) + "\n")
    elf = out / "probe.elf"
    build = ["clang", "--target=aarch64-none-elf", "-nostdlib", "-Wl,-Ttext=0x40080000",
             "-Wl,-e,_start", str(assembly), "-o", str(elf)]
    subprocess.run(build, check=True, capture_output=True)
    endpoint = out / "qmp.sock"
    command = [qemu, "-machine", "virt,virtualization=off,secure=off", "-cpu", args.cpu, "-m", "128",
               "-display", "none", "-serial", "none", "-monitor", "none", "-nic", "none",
               "-qmp", f"unix:{endpoint},server=on,wait=off", "-kernel", str(elf)]
    with (out / "qemu.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while not endpoint.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(5)
                sock.connect(str(endpoint))
                stream = sock.makefile("rwb", buffering=0)
                json.loads(stream.readline())

                def qmp(execute, arguments=None):
                    request = {"execute": execute}
                    if arguments is not None:
                        request["arguments"] = arguments
                    stream.write((json.dumps(request) + "\n").encode())
                    while True:
                        response = json.loads(stream.readline())
                        assert "error" not in response, response
                        if "return" in response:
                            return response["return"]

                qmp("qmp_capabilities")
                while time.monotonic() < deadline:
                    registers = qmp("human-monitor-command", {"command-line": "info registers"})
                    if "X00=000000000000064e" in registers:
                        break
                    time.sleep(.05)
                else:
                    raise AssertionError(registers)
                qmp("stop")
                dump = out / "results.bin"
                saved = qmp("human-monitor-command", {"command-line": f'pmemsave 0x40100000 {16 + 40 * len(vectors)} "{dump}"'})
                assert dump.is_file(), saved
                (out / "registers.txt").write_text(registers)
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)
    words = struct.unpack("<" + "Q" * (2 + 5 * len(vectors)), dump.read_bytes())
    assert words[0] == 4, "probe must execute at EL1"
    assert (words[1] >> 4) & 15 == 5, "capture contract requires the recorded QEMU APA5 profile"
    for index, vector in enumerate(vectors):
        vector.update(zip(("signed", "authenticated", "stripped", "disabled_sign", "disabled_auth"),
                          words[2 + index * 5:7 + index * 5]))
        assert vector["authenticated"] == vector["pointer"] == vector["stripped"]
        assert vector["disabled_sign"] == vector["pointer"]
        assert vector["disabled_auth"] == vector["signed"]
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    receipt = dict(passed=True, vectors=vectors, qemu_version=subprocess.check_output([qemu, "--version"], text=True).splitlines()[0],
                   qemu_sha256=sha(qemu), assembly_sha256=sha(assembly), elf_sha256=sha(elf),
                   build=build, command=command, current_el=words[0], isar1=words[1],
                   qemu_oracle_feature_level="APA5", enabled_sign_auth_compared=16,
                   xpac_disabled_compared=24, upper_enabled_unverified=8,
                   comparison_scope="APA1 sign/auth compares lower ranges only; all raw APA5 captures retained",
                   process_reaped=True, physical_arm_verified=False, macos_boot_verified=False)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": True, "vectors": len(vectors), "receipt": str(out / "receipt.json")}))


if __name__ == "__main__":
    main()
