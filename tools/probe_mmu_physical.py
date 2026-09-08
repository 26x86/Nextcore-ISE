#!/usr/bin/env python3
"""Independent Arm CPU checks for TCR_EL1.IPS output and table address faults."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

MARKERS = ['MMU_PA_ORACLE: 4K_LEAF32_FAULT', 'MMU_PA_ORACLE: 4K_LEAF36_OK', 'MMU_PA_ORACLE: 4K_TABLE32_FAULT', 'MMU_PA_ORACLE: 4K_ROOT32_FAULT', 'MMU_PA_ORACLE: 16K_LEAF32_FAULT', 'MMU_PA_ORACLE: 16K_LEAF36_OK', 'MMU_PA_ORACLE: 16K_TABLE32_FAULT', 'MMU_PA_ORACLE: 16K_ROOT32_FAULT', 'MMU_PA_ORACLE: 4K_PAGE32_FAULT', 'MMU_PA_ORACLE: 4K_PAGE36_OK', 'MMU_PA_ORACLE: 16K_PAGE32_FAULT', 'MMU_PA_ORACLE: 16K_PAGE36_OK', 'MMU_PA_ORACLE: 16K_L1_LOW32_TRANSLATION', 'MMU_PA_ORACLE: 16K_L1_HIGH32_TRANSLATION']

def probe():
    source=Path(__file__).with_name('mmu_physical_probe.S')
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    records=[]
    with tempfile.TemporaryDirectory(prefix='nextcore-mmu-pa-') as directory:
        temp=Path(directory); obj=temp/'probe.o'; elf=temp/'probe.elf'
        for args in (["clang","--target=aarch64-none-elf","-c",str(source),"-o",str(obj)],
                     ["ld.lld","-Ttext=0x40080000","-e","_start",str(obj),"-o",str(elf)],
                     ["qemu-system-aarch64","-machine","virt","-cpu","max","-m","128",
                      "-nographic","-monitor","none","-serial","none","-net","none",
                      "-semihosting-config","enable=on,target=native","-kernel",str(elf)]):
            result=subprocess.run(args,text=True,capture_output=True,timeout=20)
            records.append({'command':args,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
            if result.returncode:raise RuntimeError(json.dumps(records[-1]))
        lines=(records[-1]['stdout']+records[-1]['stderr']).splitlines()
        if lines!=MARKERS:raise RuntimeError('Unexpected physical-width outcome: '+repr(lines))
        elf_hash=hashlib.sha256(elf.read_bytes()).hexdigest()
    assert digest==hashlib.sha256(source.read_bytes()).hexdigest()
    return {'schema':'nextcore.mmu-physical-oracle/1','passed':True,'cases':len(MARKERS),
            'source_sha256':digest,'elf_sha256':elf_hash,'commands':records,
            'apple_assets_used':False,'native_jit_mmu_verified':False}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=probe()
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
