#!/usr/bin/env python3
"""Exhaustive authored register-offset memory native/reference and actual Rust provider proof."""
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
        native=work/"register_offset-native"
        run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror",runtime/"test_register_offset_jit.c",*objects.values(),"-o",native])
        run([native])
        regression=work/"scalar-immediate-regression"
        run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror",runtime/"test_scalar_jit.c",*objects.values(),"-o",regression])
        run([regression])
        lib=work/"libnextcore_memory_service.rlib"
        run([args.rustc,"--edition=2021","--crate-type=rlib","--crate-name=nextcore_memory_service","-Copt-level=2",runtime/"memory-service/src/lib.rs","-o",lib])
        for name in ("test_memory_provider","test_stage1_provider","test_dynamic_provider"):
            binary=work/name
            run([args.rustc,"--edition=2021","--test","-Copt-level=2",runtime/(name+".rs"),"--extern",f"nextcore_memory_service={lib}","-o",binary,
                 *[f"-Clink-arg={obj}" for obj in objects.values()]])
            run([binary,"--nocapture","--test-threads=1"])
        run([args.cargo,"test","--offline","--manifest-path",runtime/"preos/Cargo.toml","--target-dir",work/"preos-target"])
        mutants={
            "wrong_signed_index":("jit","if(shape.option==6){b(c,0x48);b(c,0x63);b(c,0xc0);}", "if(shape.option==6){b(c,0x90);}"),
            "wrong_native_scale":("jit","b(c,shape.shift);", "b(c,shape.shift+1);"),
            "wrong_fault_metadata":("arch","UINT32_C(0x38200800)","UINT32_C(0x38200000)"),
            "wrong_provider_scale":("jit","return value<<shape->shift;","return value;")}
        for name,(unit,needle,replacement) in mutants.items():
            original=(runtime/(unit+".c")).read_text()
            assert original.count(needle)==1
            source=work/(name+".c");source.write_text(original.replace(needle,replacement))
            obj=work/(name+".o")
            run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2","-Wall","-Wextra","-Werror","-I",runtime,"-c",source,"-o",obj])
            binary=work/name
            if name=="wrong_provider_scale":
                run([args.rustc,"--edition=2021","--test","-Copt-level=2",runtime/"test_stage1_provider.rs",
                     "--extern",f"nextcore_memory_service={lib}","-o",binary,f"-Clink-arg={obj}",
                     *[f"-Clink-arg={value}" for key,value in objects.items() if key!=unit]])
                command=[binary,"register_offset_v2_all_forms_and_precise_service_faults","--nocapture"]
                result=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=180)
                records.append(dict(command=list(map(str,command)),returncode=result.returncode,stdout=result.stdout,stderr=result.stderr,expected_failure=True))
                if result.returncode!=101 or "panicked" not in result.stderr:
                    raise RuntimeError("provider scaling mutation did not fail Rust assertion")
            else:
                run([args.clang,"-std=c11","-D_GNU_SOURCE","-O2",runtime/"test_register_offset_jit.c",obj,
                     *[value for key,value in objects.items() if key!=unit],"-o",binary])
                run([binary],expected_failure=True)
            negatives.append(name)
        run([args.cargo,"build","--offline","--release","--manifest-path",runtime/"preos/Cargo.toml",
             "--target","x86_64-unknown-uefi","--target-dir",work/"uefi-target"])
        for unit in ("jit", "arch"):
            run([args.clang,"--target=x86_64-pc-win32-coff","-ffreestanding","-fshort-wchar","-mno-red-zone",
                 "-std=c11","-O2","-Wall","-Wextra","-Werror","-c",runtime/(unit+".c"),"-o",work/(unit+"-uefi.obj")])
        passed=True
    finally:
        after={str(path):digest(path) for path in sources};passed=passed and before==after
        result=dict(schema="nextcore.register_offset-native-provider-proof.v1",passed=passed,source_preserved=before==after,
                    source_sha256_before=before,source_sha256_after=after,records=records,
                    semantic_negative_controls=negatives,native_generated_x86=passed,actual_c_rust_callbacks=passed,
                    original_images_used=False,macos_boot_verified=False)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(dict(passed=passed,receipt=str(args.output))))
    return 0 if passed else 1


if __name__=="__main__":
    raise SystemExit(main())
