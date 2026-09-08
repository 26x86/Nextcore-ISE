#!/usr/bin/env python3
"""Execute the native C JIT with the canonical no_std Rust memory service.

Linux x86_64 test host only; the product remains x86 EFI. No guest host-pointer
is passed to generated code. This is an architectural fixture, not macOS boot.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime",type=Path,default=Path(__file__).resolve().parents[1]/"runtime")
    parser.add_argument("--work-dir",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--clang",default="clang")
    parser.add_argument("--rustc",default="rustc")
    args=parser.parse_args()
    if platform.system()!="Linux" or platform.machine()!="x86_64":
        raise SystemExit("Requires Linux x86_64 for the native execution fixture")
    runtime=args.runtime.resolve(strict=True);work=args.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
    paths=[runtime/name for name in ("jit.c","jit.h","arch.c","boot_jit.c","boot_jit.h","platform_abi.h",
        "memory_abi.h","memory_boot.h","memory_boot.c","memory_boot.rs","memory_layout.c","test_memory_provider.rs",
        "preos/src/platform.rs","memory-service/Cargo.toml","memory-service/src/lib.rs","memory-service/src/abi.rs")]
    before={str(p.relative_to(runtime)):digest(p) for p in paths}
    records=[]
    def run(command: list[str]) -> None:
        completed=subprocess.run(command,text=True,capture_output=True,timeout=180)
        records.append({"command":command,"returncode":completed.returncode,"stdout":completed.stdout,"stderr":completed.stderr})
        if completed.returncode:
            raise RuntimeError(completed.stdout+completed.stderr)
    passed=False;negative_control=False
    try:
        library=work/"libnextcore_memory_service.rlib"
        run([args.rustc,"--edition=2021","--crate-type=rlib","--crate-name=nextcore_memory_service","-Copt-level=2",str(runtime/"memory-service/src/lib.rs"),"-o",str(library)])
        objects=[]
        for name in ("jit","arch","boot_jit","memory_boot","memory_layout"):
            obj=work/(name+".o");objects.append(obj)
            run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror","-c",str(runtime/(name+".c")),"-o",str(obj)])
        executable=work/"memory_provider_tests"
        command=[args.rustc,"--edition=2021","--test","-Copt-level=2",str(runtime/"test_memory_provider.rs"),"--extern",f"nextcore_memory_service={library}","-o",str(executable)]
        command.extend(f"-Clink-arg={obj}" for obj in objects)
        run(command);run([str(executable),"--nocapture","--test-threads=1"])
        service_tests=work/"memory_service_tests"
        run([args.rustc,"--edition=2021","--test","-Copt-level=2",str(runtime/"memory-service/src/lib.rs"),"-o",str(service_tests)])
        run([str(service_tests),"--nocapture"])
        # Rebuild only an external copy with memory dispatch deliberately
        # bypassed. The same mixed native/Rust test must detect the bypass.
        mutation=work/"jit_provider_bypass.c"
        text=(runtime/"jit.c").read_text()
        needle="if(provider && memory_family(w))"
        assert text.count(needle)==1
        mutation.write_text(text.replace(needle,"if(0 && memory_family(w))"))
        mutant_obj=work/"jit_provider_bypass.o"
        run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror","-I",str(runtime),"-c",str(mutation),"-o",str(mutant_obj)])
        mutant_executable=work/"memory_provider_bypass_tests"
        command=[args.rustc,"--edition=2021","--test","-Copt-level=2",str(runtime/"test_memory_provider.rs"),"--extern",f"nextcore_memory_service={library}","-o",str(mutant_executable)]
        command.extend(f"-Clink-arg={obj}" for obj in [mutant_obj,*objects[1:]])
        run(command)
        command=[str(mutant_executable),"--exact","native_alu_and_memory_commit_through_actual_rust_callbacks","--nocapture"]
        completed=subprocess.run(command,text=True,capture_output=True,timeout=30)
        records.append({"command":command,"returncode":completed.returncode,"stdout":completed.stdout,"stderr":completed.stderr,"expected_failure":True})
        if completed.returncode==0 or "native_alu_and_memory_commit_through_actual_rust_callbacks ... FAILED" not in completed.stdout:
            raise RuntimeError("Provider-bypass negative control was not detected")
        negative_control=True
        passed=True
    finally:
        after={str(p.relative_to(runtime)):digest(p) for p in paths}
        passed=passed and before==after
        result={"schema":"nextcore-native-memory-provider-v1","passed":passed,
            "native_jit_executed":passed,"actual_c_rust_callbacks":passed,"source_preserved":before==after,
            "negative_control_detected":negative_control,"source_sha256":before,"records":records,"scope":"M=0 caller-owned RAM only; no macOS boot claim"}
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"passed":passed,"receipt":str(args.output)}))
    if not passed:
        raise SystemExit(1)

if __name__=="__main__":
    main()
