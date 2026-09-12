#!/usr/bin/env python3
"""Exhaustive authored multiply-add/subtract native/reference and actual Rust provider proof."""
from __future__ import annotations
import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime",type=Path,default=Path(__file__).resolve().parents[1]/"runtime")
    parser.add_argument("--work-dir",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--clang",default="clang")
    parser.add_argument("--rustc",default="rustc")
    parser.add_argument("--cargo",default="cargo")
    args=parser.parse_args()
    if (platform.system(),platform.machine())!=("Linux","x86_64"):
        parser.error("requires actual Linux x86_64 fixture host")
    runtime=args.runtime.resolve(strict=True)
    sources=sorted(path for path in runtime.rglob("*") if path.is_file()
                   and path.suffix in (".c",".h",".inc",".rs",".toml") and "target" not in path.parts)
    sources.append(Path(__file__).resolve())
    before={str(path):digest(path) for path in sources}
    work=args.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
    records=[];passed=False;negatives=[]
    def run(command,expected_failure=False):
        command=list(map(str,command))
        result=subprocess.run(command,capture_output=True,text=True,timeout=180)
        records.append(dict(command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr,
                            expected_failure=expected_failure))
        if expected_failure:
            if result.returncode!=1 or "line" not in result.stderr:
                raise RuntimeError("compiled semantic mutation did not fail an execution assertion")
        elif result.returncode:
            raise RuntimeError(result.stdout+result.stderr)
    try:
        objects={}
        for name in ("jit","arch","boot_jit","memory_boot","memory_boot_v2","memory_dynamic",
                     "memory_layout","memory_layout_v2","memory_dynamic_layout"):
            target=work/(name+".o")
            run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror","-c",runtime/(name+".c"),"-o",target])
            objects[name]=target
        native=work/"multiply_add-native"
        run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror",runtime/"test_multiply_add_jit.c",*objects.values(),"-o",native])
        run([native])
        lib=work/"libnextcore_memory_service.rlib"
        run([args.rustc,"--edition=2021","--crate-type=rlib","--crate-name=nextcore_memory_service","-Copt-level=2",runtime/"memory-service/src/lib.rs","-o",lib])
        for name in ("test_memory_provider","test_stage1_provider","test_dynamic_provider"):
            binary=work/name
            run([args.rustc,"--edition=2021","--test","-Copt-level=2",runtime/(name+".rs"),"--extern",f"nextcore_memory_service={lib}","-o",binary,
                 *[f"-Clink-arg={obj}" for obj in objects.values()]])
            run([binary,"--nocapture","--test-threads=1"])
        run([args.cargo,"test","--offline","--manifest-path",runtime/"preos/Cargo.toml","--target-dir",work/"preos-target"])
        original=(runtime/"jit.c").read_text()
        mutants={
            "reversed_subtract":("b(c,(w&(1u<<15))?0x29:0x01);b(c,0xc8);", "b(c,(w&(1u<<15))?0x29:0x01);b(c,0xc8);if(w&(1u<<15)){if(wide)b(c,0x48);b(c,0xf7);b(c,0xd8);}"),
            "truncated_product":("b(c,wide?0x49:0x41);b(c,0x0f);b(c,0xaf);b(c,0xc1);", "b(c,0x41);b(c,0x0f);b(c,0xaf);b(c,0xc1);"),
            "clobbered_flags":("save(c,rd,0); /* Low-width arithmetic preserves guest PSTATE. */", "save(c,rd,0);field32(c,offsetof(vf_cpu,pstate),0);"),
            "widened_decode":("(w&0x7fe00000)==0x1b000000", "(w&0x7f000000)==0x1b000000")}
        for name,(needle,replacement) in mutants.items():
            assert original.count(needle)==1
            source=work/(name+".c");source.write_text(original.replace(needle,replacement))
            obj=work/(name+".o")
            run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror","-I",runtime,"-c",source,"-o",obj])
            binary=work/name
            run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2",runtime/"test_multiply_add_jit.c",obj,
                 *[value for key,value in objects.items() if key!="jit"],"-o",binary])
            run([binary],expected_failure=True);negatives.append(name)
        run([args.cargo,"build","--offline","--release","--manifest-path",runtime/"preos/Cargo.toml",
             "--target","x86_64-unknown-uefi","--target-dir",work/"uefi-target"])
        run([args.clang,"--target=x86_64-pc-win32-coff","-ffreestanding","-fshort-wchar","-mno-red-zone",
             "-std=c11","-O2","-Wall","-Wextra","-Werror","-c",runtime/"jit.c","-o",work/"jit-uefi.obj"])
        passed=True
    finally:
        after={str(path):digest(path) for path in sources};passed=passed and before==after
        result=dict(schema="nextcore.multiply_add-native-provider-proof.v1",passed=passed,source_preserved=before==after,
                    source_sha256_before=before,source_sha256_after=after,records=records,
                    semantic_negative_controls=negatives,native_generated_x86=passed,actual_c_rust_callbacks=passed,
                    original_images_used=False,macos_boot_verified=False)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(dict(passed=passed,receipt=str(args.output))))
    return 0 if passed else 1


if __name__=="__main__":
    raise SystemExit(main())
