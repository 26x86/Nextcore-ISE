/* Bounded MMFR2 policy: expose no unaligned single-copy atomicity or large VA under the scalar gate. */
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
    vf_code code = {mmap(0, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), 4096, 0};
    CHECK(code.bytes != MAP_FAILED);
    CHECK(((UINT32_C(0xd5380753) >> 5) & 0x7fff) == VF_SYSREG_KEY_ID_AA64MMFR2_EL1);
    for (unsigned rd = 0; rd < 32; rd++) {
        vf_cpu cpu;
        vf_cpu_reset(&cpu, VF_EL1);
        cpu.pstate = UINT64_C(0xf00003c5);
        cpu.sp = UINT64_C(0x800);
        for (unsigned j = 0; j < 32; j++) cpu.x[j] = UINT64_C(0x100) + j;
        uint64_t expected[32];
        memcpy(expected, cpu.x, sizeof(expected));
        if (rd != 31) expected[rd] = 0;
        uint32_t word = UINT32_C(0xd5380740) | rd;
        uint8_t ram[8] = {1, 2, 3};
        uint8_t before[8];
        memcpy(before, ram, sizeof(ram));
        CHECK(vf_run(&cpu, (uint8_t *)&word, 4, ram, sizeof(ram), &code, 1, perms, 0) == VF_BUDGET);
        CHECK(!memcmp(cpu.x, expected, sizeof(expected)) && !memcmp(ram, before, sizeof(ram)));
        CHECK(cpu.pc == 4 && cpu.retired == 1 && cpu.sp == 0x800 && cpu.pstate == 0xf00003c5);
    }
    for (unsigned selector = 0; selector < 5; selector++) {
        vf_cpu cpu;
        vf_cpu_reset(&cpu, selector == 0 ? VF_EL0 : selector == 1 ? VF_EL2 : VF_EL1);
        if (selector == 2) cpu.hcr_el2 = 1;
        if (selector == 3) cpu.scr_el3 = 1;
        if (selector == 4) cpu.hcr_el2 = UINT64_C(1) << 63;
        uint64_t value = UINT64_C(0xfeed);
        CHECK(vf_cpu_read_sysreg(&cpu, VF_SYSREG_KEY_ID_AA64MMFR2_EL1, &value) == VF_SYSREG_UNKNOWN);
        CHECK(value == UINT64_C(0xfeed));
        uint32_t word = UINT32_C(0xd5380753);
        uint8_t ram[8] = {1, 2, 3};
        uint8_t before[8];
        memcpy(before, ram, sizeof(ram));
        uint64_t registers[32];
        memcpy(registers, cpu.x, sizeof(registers));
        CHECK(vf_run(&cpu, (uint8_t *)&word, 4, ram, sizeof(ram), &code, 1, perms, 0) == VF_SYSTEM_REGISTER_TRAP);
        CHECK(cpu.pc == 0 && cpu.retired == 0 && !memcmp(cpu.x, registers, sizeof(registers)) && !memcmp(ram, before, sizeof(ram)));
    }
    vf_cpu cpu;
    vf_cpu_reset(&cpu, VF_EL1);
    uint64_t value = UINT64_C(0xfeed);
    CHECK(vf_cpu_read_sysreg(&cpu, VF_SYSREG_KEY_ID_AA64MMFR2_EL1, &value) == VF_SYSREG_OK && value == 0);
    CHECK(vf_cpu_write_sysreg(&cpu, VF_SYSREG_KEY_ID_AA64MMFR2_EL1, 1) == VF_SYSREG_READ_ONLY);
    uint32_t word = UINT32_C(0xd5380753);
    uint8_t ram[8] = {0};
    CHECK(perms(code.bytes, 4096, 0, 0) == 0);
    CHECK(vf_translate_cpu(&code, &cpu, (uint8_t *)&word, 4, 0, 1) == VF_NEXT);
    CHECK(perms(code.bytes, 4096, 1, 0) == 0);
    cpu.hcr_el2 = 1;
    cpu.x[19] = UINT64_C(0xfeed);
    CHECK(((vf_entry)(void *)code.bytes)(&cpu, ram, sizeof(ram)) == VF_SYSTEM_REGISTER_TRAP);
    CHECK(cpu.x[19] == UINT64_C(0xfeed) && cpu.pc == 0 && cpu.retired == 0);
    vf_cpu_reset(&cpu, VF_EL1);
    cpu.x[19] = UINT64_C(0xfeed);
    CHECK(((vf_entry)(void *)code.bytes)(&cpu, ram, sizeof(ram)) == VF_NEXT);
    CHECK(cpu.x[19] == 0 && cpu.pc == 4 && cpu.retired == 1);
    CHECK(munmap(code.bytes, 4096) == 0);
    printf("{\"passed\":true,\"assertions\":%u}\n", checks);
}
