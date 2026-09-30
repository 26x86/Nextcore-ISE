/* Bounded scalar PFR profile for the diagnostic EL1 research path. */
#include "jit.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int perms(void *p, size_t n, int executable, void *unused) {
    (void)unused;
    return mprotect(p, n, PROT_READ | (executable ? PROT_EXEC : PROT_WRITE));
}

int main(void) {
    /* Reset owns the complete appended bank and cannot leak another CPU's
     * state. This storage is independent of guest feature admission. */
    vf_cpu owner, other;
    memset(&owner, 0xa5, sizeof(owner));
    vf_cpu_reset(&owner, VF_EL1);
    vf_cpu_reset(&other, VF_EL1);
    const nc_fp_bank zero = {0};
    CHECK(!memcmp(&owner.fp, &zero, sizeof(zero)));
    owner.fp.v[31][0] = UINT64_C(0x123456789abcdef0);
    owner.fp.v[31][1] = UINT64_MAX;
    owner.fp.fpcr = UINT32_C(2) << 22;
    owner.fp.fpsr = NC_FP_DZC | NC_FP_IXC;
    CHECK(!memcmp(&other.fp, &zero, sizeof(zero)));
    CHECK(vf_cpu_set_current_el(&owner, VF_EL0) == 0);
    CHECK(owner.fp.v[31][0] == UINT64_C(0x123456789abcdef0) &&
          owner.fp.v[31][1] == UINT64_MAX && owner.fp.fpsr == (NC_FP_DZC | NC_FP_IXC));
    vf_cpu_reset(&owner, VF_EL1);
    CHECK(!memcmp(&owner.fp, &zero, sizeof(zero)));
    vf_code code = {mmap(0, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), 4096, 0};
    CHECK(code.bytes != MAP_FAILED);
    const uint32_t keys[3] = {VF_SYSREG_KEY_ID_AA64PFR0_EL1, VF_SYSREG_KEY_ID_AA64PFR1_EL1, VF_SYSREG_KEY_ID_AA64PFR2_EL1};
    const uint32_t words[3] = {UINT32_C(0xd5380400), UINT32_C(0xd5380420), UINT32_C(0xd5380440)};
    const uint64_t values[3] = {VF_PFR0_SCALAR_PROFILE, 0, 0};
    CHECK(VF_PFR0_SCALAR_PROFILE == UINT64_C(0x00ff0011));
    for (unsigned id = 0; id < 3; id++) {
        CHECK(((words[id] >> 5) & 0x7fff) == keys[id]);
        for (unsigned rd = 0; rd < 32; rd++) {
            vf_cpu cpu;
            vf_cpu_reset(&cpu, VF_EL1);
            cpu.pstate = UINT64_C(0xf00003c5);
            cpu.sp = UINT64_C(0x800);
            for (unsigned j = 0; j < 32; j++) cpu.x[j] = UINT64_C(0x100) + j;
            uint64_t expected[32];
            memcpy(expected, cpu.x, sizeof(expected));
            if (rd != 31) expected[rd] = values[id];
            uint32_t word = words[id] | rd;
            uint8_t ram[8] = {1, 2, 3}, before[8];
            memcpy(before, ram, sizeof(ram));
            CHECK(vf_run(&cpu, (uint8_t *)&word, 4, ram, sizeof(ram), &code, 1, perms, 0) == VF_BUDGET);
            CHECK(!memcmp(cpu.x, expected, sizeof(expected)) && !memcmp(ram, before, sizeof(ram)));
            CHECK(cpu.pc == 4 && cpu.retired == 1 && cpu.sp == 0x800 && cpu.pstate == UINT64_C(0xf00003c5));
        }
        vf_cpu cpu;
        vf_cpu_reset(&cpu, VF_EL1);
        uint64_t value = UINT64_C(0xfeed);
        CHECK(vf_cpu_read_sysreg(&cpu, keys[id], &value) == VF_SYSREG_OK && value == values[id]);
        CHECK(vf_cpu_write_sysreg(&cpu, keys[id], 1) == VF_SYSREG_READ_ONLY);
        uint32_t write = (words[id] & ~UINT32_C(0x00200000)) | 19;
        uint8_t write_ram[8] = {0};
        CHECK(vf_run(&cpu, (uint8_t *)&write, 4, write_ram, sizeof(write_ram), &code, 1, perms, 0) == VF_SYSTEM_REGISTER_TRAP);
        CHECK(cpu.pc == 0 && cpu.retired == 0);
        for (unsigned selector = 0; selector < 4; selector++) {
            vf_cpu_reset(&cpu, selector == 0 ? VF_EL0 : selector == 1 ? VF_EL2 : VF_EL1);
            if (selector == 2) cpu.hcr_el2 = 1;
            if (selector == 3) cpu.scr_el3 = 1;
            value = UINT64_C(0xfeed);
            CHECK(vf_cpu_read_sysreg(&cpu, keys[id], &value) == VF_SYSREG_UNKNOWN);
            CHECK(value == UINT64_C(0xfeed));
            uint32_t word = words[id] | 19;
            uint8_t ram[8] = {1, 2, 3};
            uint64_t before[32];
            memcpy(before, cpu.x, sizeof(before));
            CHECK(vf_run(&cpu, (uint8_t *)&word, 4, ram, sizeof(ram), &code, 1, perms, 0) == VF_SYSTEM_REGISTER_TRAP);
            CHECK(cpu.pc == 0 && cpu.retired == 0 && !memcmp(cpu.x, before, sizeof(before)));
        }
    }
    vf_cpu cpu;
    vf_cpu_reset(&cpu, VF_EL1);
    uint32_t word = UINT32_C(0xd5380453);
    uint8_t ram[8] = {0};
    CHECK(perms(code.bytes, 4096, 0, 0) == 0);
    CHECK(vf_translate_cpu(&code, &cpu, (uint8_t *)&word, 4, 0, 1) == VF_NEXT);
    CHECK(perms(code.bytes, 4096, 1, 0) == 0);
    cpu.hcr_el2 = 1;
    cpu.x[19] = UINT64_C(0xfeed);
    CHECK(((vf_entry)(void *)code.bytes)(&cpu, ram, sizeof(ram)) == VF_SYSTEM_REGISTER_TRAP);
    CHECK(cpu.x[19] == UINT64_C(0xfeed) && cpu.pc == 0 && cpu.retired == 0);
    vf_cpu_reset(&cpu, VF_EL1);
    CHECK(((vf_entry)(void *)code.bytes)(&cpu, ram, sizeof(ram)) == VF_NEXT);
    CHECK(cpu.x[19] == 0 && cpu.pc == 4 && cpu.retired == 1);
    vf_cpu_reset(&cpu, VF_EL1);
    uint64_t unknown = UINT64_C(0xfeed);
    CHECK(vf_cpu_read_sysreg(&cpu, UINT32_C(0x4023), &unknown) == VF_SYSREG_UNKNOWN);
    CHECK(unknown == UINT64_C(0xfeed));
    uint32_t neighbor = UINT32_C(0xd5380473);
    CHECK(vf_run(&cpu, (uint8_t *)&neighbor, 4, ram, sizeof(ram), &code, 1, perms, 0) == VF_SYSTEM_REGISTER_TRAP);
    CHECK(cpu.pc == 0 && cpu.retired == 0);
    vf_cpu_reset(&cpu, VF_EL1);
    uint32_t fpcr = UINT32_C(0xd53b4408);
    cpu.x[8] = UINT64_C(0xfeed);
    CHECK(vf_cpu_read_sysreg(&cpu, VF_SYSREG_KEY_FPCR, &unknown) == VF_SYSREG_UNDEFINED);
    CHECK(vf_run(&cpu, (uint8_t *)&fpcr, 4, ram, sizeof(ram), &code, 1, perms, 0) == VF_UNDEFINED_INSTRUCTION);
    CHECK(cpu.x[8] == UINT64_C(0xfeed) && cpu.pc == 0 && cpu.retired == 0);
    for (unsigned el=0; el<4; el++) for (unsigned reg=0; reg<2; reg++)
    for (unsigned write=0; write<2; write++) for (unsigned rt=0; rt<32; rt++) {
        vf_cpu_reset(&cpu, el);
        cpu.hcr_el2=UINT64_MAX;cpu.scr_el3=UINT64_MAX;
        cpu.x[rt]=UINT64_C(0xfeed);cpu.sp=0x800;
        for (unsigned vr=0; vr<32; vr++) {
            cpu.fp.v[vr][0]=UINT64_C(0x123456789abcdef0)+vr;
            cpu.fp.v[vr][1]=UINT64_MAX-vr;
        }
        cpu.fp.fpcr=UINT32_C(3)<<22;cpu.fp.fpsr=NC_FP_DZC|NC_FP_IXC;
        nc_fp_bank saved_fp=cpu.fp;
        uint64_t before[32], pstate=cpu.pstate;
        memcpy(before,cpu.x,sizeof(before));
        uint64_t sentinel=UINT64_C(0xface);
        uint32_t key=reg?VF_SYSREG_KEY_FPSR:VF_SYSREG_KEY_FPCR;
        uint32_t instruction=(write?UINT32_C(0xd5000000):UINT32_C(0xd5200000))|(key<<5)|rt;
        CHECK(vf_cpu_read_sysreg(&cpu,key,&sentinel)==VF_SYSREG_UNDEFINED && sentinel==UINT64_C(0xface));
        CHECK(vf_cpu_write_sysreg(&cpu,key,UINT64_MAX)==VF_SYSREG_UNDEFINED);
        CHECK(!memcmp(&cpu.fp,&saved_fp,sizeof(saved_fp)));
        CHECK(vf_run(&cpu,(uint8_t *)&instruction,4,ram,sizeof(ram),&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.pc==0 && cpu.retired==0 && cpu.sp==0x800 && cpu.pstate==pstate && !memcmp(before,cpu.x,sizeof(before)));
        CHECK(cpu.instruction==instruction && (cpu.exception_syndrome>>26)==VF_ESR_EC_UNKNOWN);
        CHECK(!memcmp(&cpu.fp,&saved_fp,sizeof(saved_fp)));
    }
    CHECK(munmap(code.bytes, 4096) == 0);
    printf("{\"passed\":true,\"assertions\":%u}\n", checks);
}
