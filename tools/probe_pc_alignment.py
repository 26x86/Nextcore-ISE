#!/usr/bin/env python3
"""Independent AArch64 branch/ESR oracle, including the wrong-IL negative control."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();work=args.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
    source=Path(__file__).with_name("pc_alignment_probe.S").resolve();before=hashlib.sha256(source.read_bytes()).hexdigest()
    records=[]
    for name,esr,expected in [("correct",0x8a000000,0),("wrong_il",0x88000000,1)]:
        obj=work/(name+".o");elf=work/(name+".elf")
        commands=[["clang","--target=aarch64-none-elf",f"-DEXPECTED_ESR={esr}","-c",str(source),"-o",str(obj)],
            ["ld.lld","-Ttext=0x40080000","-e","_start",str(obj),"-o",str(elf)],
            ["qemu-system-aarch64","-machine","virt","-cpu","max","-m","128","-nographic","-monitor","none","-serial","none","-net","none",
             "-semihosting-config","enable=on,target=native","-kernel",str(elf)]]
        for index,command in enumerate(commands):
            r=subprocess.run(command,capture_output=True,text=True,timeout=20)
            records.append({"case":name,"command":command,"returncode":r.returncode,"stdout":r.stdout,"stderr":r.stderr})
            assert r.returncode==(expected if index==2 else 0),records[-1]
        output=records[-1]["stdout"]+records[-1]["stderr"]
        assert ("PASS ESR=8a000000 FAR=target ELR=target" if expected==0 else "FAIL") in output
    assert before==hashlib.sha256(source.read_bytes()).hexdigest()
    result={"schema":"nextcore.pc-alignment-oracle/1","passed":True,"source_sha256":before,"negative_control_detected":True,
        "records":records,"apple_assets_used":False,"macos_boot_verified":False}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"passed":True,"receipt":str(args.output)}))
if __name__=="__main__":main()
