#!/usr/bin/env python3
"""Independent AT/PAR oracle: exact Arm stage1 fault levels, no guest OS."""
from pathlib import Path
import argparse, hashlib, json, subprocess


def cases():
    result=[]
    def add(name,g,tsz,start,level,kind,fsc,write=0,upper=0,ips=0,pa=None):
        result.append(dict(name=name,granule=g,tsz=tsz,upper=upper,start=start,
                           level=level,kind=kind,write=write,ips=ips,expected_fsc=fsc,expected_pa=pa))
    for g,tsz,start in [(4096,16,0),(16384,17,1)]:
        tag='4K' if g==4096 else '16K'
        for level in range(start,4):
            for upper in [0,1]:
                for kind in [0,1]:
                    add(f'{tag}_TTBR{upper}_invalid{kind}_L{level}',g,tsz,start,level,kind,4+level,upper=upper)
            if level in ([0,3] if g==4096 else [1,3]):
                add(f'{tag}_reserved_block_L{level}',g,tsz,start,level,2,4+level)
            if level<3:
                add(f'{tag}_table_PA32_L{level}',g,tsz,start,level,6,level)
        for level in ([1,2,3] if g==4096 else [2,3]):
            for kind,write,base in [(3,0,8),(4,1,12),(5,0,0)]:
                add(f'{tag}_leaf_kind{kind}_L{level}',g,tsz,start,level,kind,base+level,write=write)
            add(f'{tag}_leaf_success_L{level}',g,tsz,start,level,9,None,pa=0x40000000)
            add(f'{tag}_leaf_PA36_L{level}',g,tsz,start,level,5,None,ips=1,pa=1<<32)
        for alt_tsz,alt_start in ([(16,0),(25,1),(34,2),(39,2)] if g==4096 else [(17,1),(28,2),(39,3),(47,3)]):
            for upper in [0,1]:
                add(f'{tag}_root_PA32_start{alt_start}_tsz{alt_tsz}_T{upper}',g,alt_tsz,alt_start,3,7,0,upper=upper)
                add(f'{tag}_EPD_start{alt_start}_tsz{alt_tsz}_T{upper}',g,alt_tsz,alt_start,3,8,4,upper=upper)
                add(f'{tag}_invalid_start{alt_start}_tsz{alt_tsz}_T{upper}',g,alt_tsz,alt_start,alt_start,0,4+alt_start,upper=upper)
            add(f'{tag}_noncanonical_tsz{alt_tsz}',g,alt_tsz,alt_start,3,10,4)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).resolve().parent; test_cases=cases()
    header='static const struct test_case cases[]={\n'+''.join(
        '{"'+c['name']+'",'+','.join(str(c[k]) for k in ['granule','tsz','upper','start','level','kind','write','ips'])+'},\n' for c in test_cases)+'};\n'
    (output/'cases.h').write_text(header);(output/'cases.json').write_text(json.dumps(test_cases,indent=2)+'\n')
    source_paths=[source/name for name in ['oracle.c','oracle_start.S','oracle.ld','probe_fault_levels.py']]
    before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
    commands=[];executables={};regimes={}
    def run(command):
        p=subprocess.run(command,capture_output=True,text=True,timeout=40)
        commands.append(dict(argv=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
        if p.returncode:raise RuntimeError(json.dumps(commands[-1]))
        return p.stdout+p.stderr
    common=['clang','--target=aarch64-none-elf','-ffreestanding','-fno-builtin','-mgeneral-regs-only','-O2','-I',str(output)]
    run(common+['-c',str(source/'oracle_start.S'),'-o',str(output/'start.o')])
    outcomes={}
    for variant in ['normal','negative']:
        run(common+(['-DNEGATIVE_CONTROL'] if variant=='negative' else [])+['-c',str(source/'oracle.c'),'-o',str(output/(variant+'.o'))])
        run(['ld.lld','-T',str(source/'oracle.ld'),str(output/'start.o'),str(output/(variant+'.o')),'-o',str(output/(variant+'.elf'))])
        executables[variant]=hashlib.sha256((output/(variant+'.elf')).read_bytes()).hexdigest()
        text=run(['qemu-system-aarch64','-machine','virt,virtualization=on','-cpu','max','-m','128','-nographic','-monitor','none','-serial','none','-net','none','-semihosting-config','enable=on,target=native','-kernel',str(output/(variant+'.elf'))])
        (output/(variant+'.log')).write_text(text)
        records={line.split()[0]:int(line.split()[1],16) for line in text.splitlines()}
        regimes[variant]={key:records[key] for key in ['FEATURES','HCR','CURRENT_EL']}
        observed=[]
        for c in test_cases:
            par=records[c['name']];fsc=((par>>1)&63) if par&1 else None
            matched=fsc==c['expected_fsc'] and (c['expected_pa'] is None or (par&0x0000fffffffff000)==c['expected_pa'])
            if par&1:matched=matched and (par&0x300)==0
            keys=['ttbr0','ttbr1','tcr','va','access']+[f'{kind}{i}' for i in range(4) for kind in ['table','value']]
            captured={key:records[c['name']+'.'+key] for key in keys}
            observed.append(dict(**c,par=par,observed_fsc=fsc,observed_input=captured,passed=matched))
        outcomes[variant]=observed
    report={'schema':'nextcore.mmu-fault-level-oracle.v1','cases':outcomes['normal'],'case_count':len(test_cases),
            'passed':all(c['passed'] for c in outcomes['normal']) and not outcomes['negative'][0]['passed'],
            'negative_control_detected':not outcomes['negative'][0]['passed'],'negative_control':outcomes['negative'][0],
            'source_sha256_before':before,'source_sha256_after':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths},
            'executable_sha256':executables,'observed_regimes':regimes,
            'qemu_version':run(['qemu-system-aarch64','--version']).splitlines()[0],'commands':commands,
            'apple_assets_used':False,'native_jit_mmu_verified':False}
    report['passed']=report['passed'] and report['source_sha256_before']==report['source_sha256_after'] and all(r['HCR']==1<<31 and r['CURRENT_EL']==8 for r in regimes.values())
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':report['passed'],'case_count':len(test_cases),'failures':[(c['name'],c['expected_fsc'],c['observed_fsc']) for c in outcomes['normal'] if not c['passed']],'negative_control_detected':report['negative_control_detected']}))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
