#!/usr/bin/env python3
"""Actual generated-x86 / Rust one-way MMU-enable proof, with semantic mutants.

This authored Linux execution fixture is not a macOS boot claim. Product callers
remain x86 EFI; the fixed v1/v2 entry contracts are not widened.
"""
from __future__ import annotations
import argparse,hashlib,json,platform,subprocess
from pathlib import Path

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime',type=Path,default=Path(__file__).resolve().parents[1]/'runtime')
    ap.add_argument('--work-dir',required=True,type=Path);ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--clang',default='clang');ap.add_argument('--rustc',default='rustc')
    a=ap.parse_args()
    if(platform.system(),platform.machine())!=('Linux','x86_64'):raise SystemExit('Requires Linux x86_64 native fixture host')
    r=a.runtime.resolve(strict=True);w=a.work_dir.resolve();w.mkdir(parents=True,exist_ok=True)
    paths=sorted(p for p in r.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc','.rs','.toml') and 'target' not in p.parts)
    before={str(p.relative_to(r)):digest(p) for p in paths};records=[];passed=False;negatives={}
    def run(cmd,negative_test=None):
        cmd=list(map(str,cmd));p=subprocess.run(cmd,capture_output=True,text=True,timeout=180)
        records.append(dict(command=cmd,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,expected_failure=negative_test is not None))
        if negative_test:
            if p.returncode==0 or negative_test+' ... FAILED' not in p.stdout:raise RuntimeError('Semantic mutation was not detected: '+negative_test)
        elif p.returncode:raise RuntimeError(p.stdout+p.stderr)
    def compile_test(name,source,library,objects):
        exe=w/name;run([a.rustc,'--edition=2021','--test','-Copt-level=2',source,'--extern',f'nextcore_memory_service={library}','-o',exe,*[f'-Clink-arg={o}'for o in objects]])
        return exe
    try:
        lib=w/'libservice.rlib';run([a.rustc,'--edition=2021','--crate-type=rlib','--crate-name=nextcore_memory_service','-Copt-level=2',r/'memory-service/src/lib.rs','-o',lib])
        objects=[]
        for name in ('jit','arch','boot_jit','memory_boot','memory_boot_v2','memory_dynamic','memory_dynamic_layout'):
            obj=w/(name+'.o');run([a.clang,'-std=c11','-D_GNU_SOURCE','-O2','-Wall','-Wextra','-Werror','-c',r/(name+'.c'),'-o',obj]);objects.append(obj)
        exe=compile_test('dynamic_tests',r/'test_dynamic_provider.rs',lib,objects);run([exe,'--nocapture','--test-threads=1'])
        service=w/'service_tests';run([a.rustc,'--edition=2021','--test','-Copt-level=2',r/'memory-service/src/lib.rs','-o',service]);run([service,'--nocapture'])
        # The unchanged v2 tests execute against the same rebuilt JIT/service.
        obj=w/'memory_layout_v2.o';run([a.clang,'-std=c11','-O2','-Wall','-Wextra','-Werror','-c',r/'memory_layout_v2.c','-o',obj])
        v2=compile_test('legacy_v2_tests',r/'test_stage1_provider.rs',lib,[*objects,obj]);run([v2,'--nocapture','--test-threads=1'])
        # Each mutant is an external copy. Production sources remain unchanged.
        mutations={
            'memory_dispatch_bypass':('jit.c','if(provider && memory_family(w))','if(0 && memory_family(w))',
                'generated_x86_crosses_guarded_isb_and_uses_nonidentity_scalar_pair_memory'),
            'cpu_bank_before_commit_ack':('memory_dynamic.inc','vf_dynamic_reply reply={0};','cpu->sctlr=q.candidate.sctlr; vf_dynamic_reply reply={0};',
                'actual_cpu_control_bank_changes_only_after_a_valid_commit_acknowledgement'),
            'isb_without_effective_commit':('memory_dynamic.inc','expected.effective=known.architectural;expected.epoch++;','expected.epoch++;',
                'generated_x86_crosses_guarded_isb_and_uses_nonidentity_scalar_pair_memory'),
        }
        for name,(file,needle,replacement,test) in mutations.items():
            d=w/name;d.mkdir(exist_ok=True);text=(r/file).read_text();assert text.count(needle)==1
            target=d/file;target.write_text(text.replace(needle,replacement))
            if file!='jit.c':
                source=(r/'jit.c').read_text().replace('#include "memory_dynamic.inc"','#include '+json.dumps(str(target)))
                target=d/'jit.c';target.write_text(source)
            obj=d/'jit.o';run([a.clang,'-std=c11','-D_GNU_SOURCE','-O2','-Wall','-Wextra','-Werror','-I',r,'-c',target,'-o',obj])
            exe=compile_test(name+'_tests',r/'test_dynamic_provider.rs',lib,[obj,*objects[1:]])
            run([exe,'--exact',test,'--nocapture'],negative_test=test);negatives[name]=True
        service_mutations={
            'omit_actual_tlbi_invalidation':('walker.invalidate();out.invalidations=generation;','out.invalidations=generation;',
                'stage1::dynamic::tests::enable_splits_architectural_effective_state_then_reuses_real_tlb'),
            'guard_ignores_isb_bytes':('if u32::from_le_bytes(self.inner.ram[index..index+4].try_into().unwrap())!=ISB_WORD',
                'if false && u32::from_le_bytes(self.inner.ram[index..index+4].try_into().unwrap())!=ISB_WORD',
                'stage1::dynamic::tests::guarded_enable_failures_leave_all_state_and_ram_unchanged'),
        }
        service=r/'memory-service/src'
        for name,(needle,replacement,test) in service_mutations.items():
            d=w/name;d.mkdir(exist_ok=True);text=(service/'dynamic.rs').read_text();assert text.count(needle)==1
            target=d/'dynamic.rs';target.write_text(text.replace(needle,replacement).replace('#[path="dynamic_tests.rs"]',
                '#[path='+json.dumps(str(service/'dynamic_tests.rs'))+']'))
            stage=(service/'stage1.rs').read_text().replace('#[path="dynamic.rs"]','#[path='+json.dumps(str(target))+']')
            stage=stage.replace('#[path="stage1_tests.rs"]','#[path='+json.dumps(str(service/'stage1_tests.rs'))+']')
            (d/'stage1.rs').write_text(stage)
            wrapper=(service/'lib.rs').read_text()
            for module in ('abi','abi_v2','dynamic_abi'):
                wrapper=wrapper.replace(f'pub mod {module};',f'#[path={json.dumps(str(service/(module+".rs")))}] pub mod {module};')
            for module in ('mmu','exception_level'):
                wrapper=wrapper.replace(f'"../../preos/src/{module}.rs"',json.dumps(str(r/f'preos/src/{module}.rs')))
            (d/'lib.rs').write_text(wrapper)
            exe=d/'service_tests';run([a.rustc,'--edition=2021','--test','-Copt-level=2',d/'lib.rs','-o',exe])
            run([exe,'--exact',test,'--nocapture'],negative_test=test);negatives[name]=True
        passed=True
    finally:
        after={str(p.relative_to(r)):digest(p) for p in paths};passed=passed and before==after
        receipt=dict(schema='nextcore-dynamic-memory-control-v1-native-proof',passed=passed,source_preserved=before==after,
            actual_c_rust_callbacks=passed,native_generated_x86=passed,negative_controls=negatives,source_sha256=before,records=records,
            scope='Authored one-way stage-1 enable with immutable Normal-NC tables, M=0 Device data; no macOS boot claim')
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(dict(passed=passed,receipt=str(a.output))))
    if not passed:raise SystemExit(1)
if __name__=='__main__':main()
