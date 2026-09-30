/* SPDX-License-Identifier: BSD-4-Clause; authored Q memory contract tests. */
#include "simd_memory.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#x); exit(1); \
} } while (0)

/* Test-generated encodings, with an oracle parameter separate from decoding. */
static uint32_t word(unsigned load, unsigned mode, int imm, unsigned rn, unsigned rt) {
    uint32_t base = mode == 4 ? UINT32_C(0x3d800000) : UINT32_C(0x3c800000);
    uint32_t bits = mode == 4 ? (uint32_t)imm << 10 :
        ((uint32_t)imm & 511u) << 12 | mode << 10;
    return base | load << 22 | bits | rn << 5 | rt;
}
static nc_simd_mem_result execute(nc_fp_bank *b, nc_simd_gprs *g,
                                 nc_simd_memory *m, uint32_t w) {
    return nc_simd_execute_q_memory(b,g,m,w,1,1,UINT64_C(0x300000),0);
}
static void seed(nc_fp_bank *b, nc_simd_gprs *g, uint8_t *bytes, size_t size) {
    memset(b,0,sizeof(*b)); b->fpcr = UINT32_C(0x800000); b->fpsr = 0x15;
    for (unsigned i = 0; i < 32; ++i) {
        b->v[i][0] = UINT64_C(0xfedcba9876543210) - i;
        b->v[i][1] = UINT64_C(0x01030507090b0d0f) + i;
    }
    for (unsigned i = 0; i < 31; ++i) g->x[i] = UINT64_C(0x101000);
    g->sp = UINT64_C(0x101000);
    for (size_t i = 0; i < size; ++i) bytes[i] = (uint8_t)(i * 37u + 11u);
}
static void success(unsigned load, unsigned mode, int imm, unsigned rn, unsigned rt) {
    nc_fp_bank b, expected_b; nc_simd_gprs g, expected_g;
    uint8_t bytes[73728], expected_bytes[73728];
    seed(&b,&g,bytes,sizeof(bytes)); expected_b = b; expected_g = g;
    memcpy(expected_bytes,bytes,sizeof(bytes));
    nc_simd_memory m = {bytes,0x100000,sizeof(bytes),1,1,1,0,0};
    int displacement = mode == 4 ? imm * 16 : imm;
    size_t index = (size_t)(4096 + (mode == 1 ? 0 : displacement));
    if (load) {
        /* memcpy is an independent host-endian oracle on the supported x86 host. */
        memcpy(expected_b.v[rt],bytes+index,16);
    } else memcpy(expected_bytes+index,b.v[rt],16);
    if (mode == 1 || mode == 3) {
        uint64_t updated = UINT64_C(0x101000) + (uint64_t)(int64_t)displacement;
        if (rn == 31) expected_g.sp = updated; else expected_g.x[rn] = updated;
    }
    CHECK(execute(&b,&g,&m,word(load,mode,imm,rn,rt)) == NC_SIMD_MEM_OK);
    CHECK(!memcmp(&b,&expected_b,sizeof(b)));
    CHECK(!memcmp(&g,&expected_g,sizeof(g)));
    CHECK(!memcmp(bytes,expected_bytes,sizeof(bytes)));
}
static void all_forms(void) {
    static const unsigned modes[] = {0,1,3,4};
    for (unsigned load = 0; load < 2; ++load)
    for (unsigned t = 0; t < 4; ++t) {
        unsigned mode = modes[t];
        for (unsigned rn = 0; rn < 32; ++rn)
        for (unsigned rt = 0; rt < 32; ++rt) success(load,mode,0,rn,rt);
        int first = mode == 4 ? 0 : -256, last = mode == 4 ? 4095 : 255;
        for (int imm = first; imm <= last; ++imm) success(load,mode,imm,31,31);
    }
}
static void unchanged(nc_fp_bank *b, nc_simd_gprs *g, nc_simd_memory *m,
                       uint32_t w, nc_simd_mem_result want) {
    nc_fp_bank saved_b = *b; nc_simd_gprs saved_g = *g;
    uint8_t saved[256]; CHECK(m->length <= sizeof(saved));
    memcpy(saved,m->bytes,m->length);
    CHECK(execute(b,g,m,w) == want);
    CHECK(!memcmp(b,&saved_b,sizeof(*b)) && !memcmp(g,&saved_g,sizeof(*g)));
    CHECK(!memcmp(m->bytes,saved,m->length));
}
static void faults(void) {
    nc_fp_bank b; nc_simd_gprs g; uint8_t bytes[256];
    seed(&b,&g,bytes,sizeof(bytes));
    nc_simd_memory m = {bytes,0x101000,sizeof(bytes),1,1,1,0,0};
    for (unsigned load = 0; load < 2; ++load)
    for (unsigned mode = 0; mode < 4; ++mode) if (mode != 2) {
        uint32_t w = word(load,mode,16,31,31);
        for (unsigned size = 1; size < (mode == 1 ? 16u : 32u); ++size) {
            m.length = size; unchanged(&b,&g,&m,w,NC_SIMD_MEM_DATA_ABORT);
        }
        m.length = sizeof(bytes);
        if (load) m.readable = 0; else m.writable = 0;
        unchanged(&b,&g,&m,w,NC_SIMD_MEM_DATA_ABORT);
        m.readable = m.writable = 1;
        m.guest_base++; unchanged(&b,&g,&m,word(load,mode,0,31,31),NC_SIMD_MEM_DATA_ABORT);
        m.guest_base--;
        g.sp++; unchanged(&b,&g,&m,w,NC_SIMD_MEM_SP_ALIGNMENT);
        /* Old misaligned SP remains a fault even when pre-index aligns the address. */
        unchanged(&b,&g,&m,word(load,3,15,31,31),NC_SIMD_MEM_SP_ALIGNMENT);
        m.sp_alignment = 0; m.data_alignment = 1;
        unchanged(&b,&g,&m,w,NC_SIMD_MEM_DATA_ALIGNMENT);
        m.data_alignment = 0;
        CHECK(execute(&b,&g,&m,word(load,0,0,31,31)) == NC_SIMD_MEM_OK);
        g.sp = 0x101000; m.sp_alignment = 1;
        /* Aligned old SP may produce an unaligned new SP after a valid write. */
        CHECK(execute(&b,&g,&m,word(load,3,1,31,31)) == NC_SIMD_MEM_OK);
        CHECK(g.sp == UINT64_C(0x101001)); g.sp = 0x101000;
    }
    for (unsigned load = 0; load < 2; ++load) {
        m.guest_base = UINT64_MAX - 255; m.sp_alignment = 0;
        g.x[0] = UINT64_MAX - 15;
        CHECK(execute(&b,&g,&m,word(load,0,0,0,31)) == NC_SIMD_MEM_OK);
        g.x[0]++; unchanged(&b,&g,&m,word(load,3,0,0,31),NC_SIMD_MEM_DATA_ABORT);
        m.guest_base = 0; g.x[0] = UINT64_MAX - 7;
        CHECK(execute(&b,&g,&m,word(load,3,8,0,31)) == NC_SIMD_MEM_OK);
        CHECK(g.x[0] == 0);
        m.guest_base = UINT64_MAX - 255; g.x[0] = 0;
        CHECK(execute(&b,&g,&m,word(load,3,-16,0,31)) == NC_SIMD_MEM_OK);
        CHECK(g.x[0] == UINT64_MAX - 15);
    }
}
static void access_and_decode(void) {
    nc_fp_bank b; nc_simd_gprs g; uint8_t bytes[64];
    seed(&b,&g,bytes,sizeof(bytes)); nc_simd_memory m = {bytes,0x101000,64,1,1,0,0,0};
    for (unsigned load = 0; load < 2; ++load) {
        uint32_t w = word(load,3,1,31,31);
        for (unsigned present = 0; present < 2; ++present)
        for (unsigned el = 0; el < 4; ++el)
        for (unsigned fpen = 0; fpen < 4; ++fpen)
        for (unsigned higher = 0; higher < 2; ++higher) {
            seed(&b,&g,bytes,sizeof(bytes)); nc_fp_bank sb = b; nc_simd_gprs sg = g;
            uint8_t saved[64]; memcpy(saved,bytes,64);
            nc_simd_mem_result want = !present ? NC_SIMD_MEM_UNDEFINED :
                el > 1 || higher ? NC_SIMD_MEM_UNSUPPORTED :
                fpen == 0 || fpen == 2 || (fpen == 1 && el == 0) ?
                NC_SIMD_MEM_TRAP_EL1 : NC_SIMD_MEM_OK;
            CHECK(nc_simd_execute_q_memory(&b,&g,&m,w,present,el,(uint64_t)fpen<<20,higher) == want);
            if (want != NC_SIMD_MEM_OK) {
                CHECK(!memcmp(&b,&sb,sizeof(b)) && !memcmp(&g,&sg,sizeof(g)));
                CHECK(!memcmp(bytes,saved,64));
            }
        }
    }
    seed(&b,&g,bytes,sizeof(bytes));
    static const uint32_t reject[] = {
        0x3c800800,0x3cc00800, /* Reserved/unprivileged modes. */
        0x3ce06800,0x9c000000,0xad400000, /* Register, literal, pair. */
        0x3d400000,0xfd400000,0xbd400000,0x39400000,0,UINT32_MAX
    };
    for (unsigned i = 0; i < sizeof(reject)/sizeof(reject[0]); ++i)
        unchanged(&b,&g,&m,reject[i],NC_SIMD_MEM_UNSUPPORTED);
    uint32_t w = word(0,3,1,0,31);
    CHECK(nc_simd_execute_q_memory(&b,&g,&m,0,0,3,UINT64_MAX,1) == NC_SIMD_MEM_UNSUPPORTED);
    for (unsigned bit = 0; bit < 32; ++bit) if (bit != 24 && (UINT32_C(0xffa00000)&(1u<<bit)))
        unchanged(&b,&g,&m,w^(1u<<bit),NC_SIMD_MEM_UNSUPPORTED);
    CHECK(execute(NULL,&g,&m,w) == NC_SIMD_MEM_INVALID);
    CHECK(execute(&b,NULL,&m,w) == NC_SIMD_MEM_INVALID);
    CHECK(execute(&b,&g,NULL,w) == NC_SIMD_MEM_INVALID);
    CHECK(nc_simd_execute_q_memory(&b,&g,&m,w,2,1,0x300000,0) == NC_SIMD_MEM_INVALID);
    CHECK(nc_simd_execute_q_memory(&b,&g,&m,w,1,4,0x300000,0) == NC_SIMD_MEM_INVALID);
    CHECK(nc_simd_execute_q_memory(&b,&g,&m,w,1,1,0x300000,2) == NC_SIMD_MEM_INVALID);
    CHECK(nc_simd_execute_q_memory(&b,&g,&m,w,1,1,UINT64_MAX,0) == NC_SIMD_MEM_UNSUPPORTED);
    b.fpcr |= 1; unchanged(&b,&g,&m,w,NC_SIMD_MEM_INVALID);
    CHECK(nc_simd_execute_q_memory(&b,&g,&m,w,0,1,0,0) == NC_SIMD_MEM_UNDEFINED);
    b.fpcr &= ~1u; b.fpsr |= 32; unchanged(&b,&g,&m,w,NC_SIMD_MEM_INVALID); b.fpsr &= ~32u;
    m.big_endian = 1; unchanged(&b,&g,&m,w,NC_SIMD_MEM_UNSUPPORTED); m.big_endian = 0;
    unsigned *controls[] = {&m.readable,&m.writable,&m.sp_alignment,&m.data_alignment,&m.big_endian};
    for (unsigned i = 0; i < 5; ++i) {
        unsigned old = *controls[i]; *controls[i] = 2;
        unchanged(&b,&g,&m,w,NC_SIMD_MEM_INVALID); *controls[i] = old;
    }
    m.guest_base = UINT64_MAX; unchanged(&b,&g,&m,w,NC_SIMD_MEM_INVALID); m.guest_base = 0x101000;
    m.length = 0; CHECK(execute(&b,&g,&m,w) == NC_SIMD_MEM_INVALID); m.length = 64;
    m.bytes = NULL; CHECK(execute(&b,&g,&m,w) == NC_SIMD_MEM_INVALID);
    m.bytes = (uint8_t *)&b; CHECK(execute(&b,&g,&m,w) == NC_SIMD_MEM_INVALID);
    m.bytes = (uint8_t *)&g; CHECK(execute(&b,&g,&m,w) == NC_SIMD_MEM_INVALID);
    m.bytes = (uint8_t *)&m; CHECK(execute(&b,&g,&m,w) == NC_SIMD_MEM_INVALID);
    m.bytes = (uint8_t *)(UINTPTR_MAX-7); CHECK(execute(&b,&g,&m,w) == NC_SIMD_MEM_INVALID);
    m.bytes = bytes;
    nc_fp_bank sb = b; nc_simd_gprs sg = g;
    CHECK(execute(&b,(nc_simd_gprs *)&b,&m,w) == NC_SIMD_MEM_INVALID);
    CHECK(!memcmp(&b,&sb,sizeof(b)) && !memcmp(&g,&sg,sizeof(g)));
}
static void guarded_span(void) {
    long page = sysconf(_SC_PAGESIZE); CHECK(page >= 16);
    uint8_t *p = mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p != MAP_FAILED); CHECK(mprotect(p+page,(size_t)page,PROT_NONE) == 0);
    nc_fp_bank b; nc_simd_gprs g; seed(&b,&g,p+page-16,16);
    nc_simd_memory m = {p+page-16,0x101000,16,1,1,0,0,0};
    for (unsigned load = 0; load < 2; ++load) {
        CHECK(execute(&b,&g,&m,word(load,0,0,0,31)) == NC_SIMD_MEM_OK);
        unchanged(&b,&g,&m,word(load,3,1,0,31),NC_SIMD_MEM_DATA_ABORT);
        m.length = 15; unchanged(&b,&g,&m,word(load,3,0,0,31),NC_SIMD_MEM_DATA_ABORT); m.length = 16;
    }
    CHECK(munmap(p,(size_t)page*2) == 0);
}
static int perms(void *p, size_t n, int executable, void *unused) {
    (void)unused; return mprotect(p,n,PROT_READ|(executable?PROT_EXEC:PROT_WRITE));
}
static void admission(void) {
    vf_code code = {mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes != MAP_FAILED);
    static const unsigned modes[] = {0,1,3,4};
    for (unsigned load = 0; load < 2; ++load)
    for (unsigned i = 0; i < 4; ++i) for (unsigned el = 0; el < 4; ++el) {
        vf_cpu cpu; vf_cpu_reset(&cpu,el); cpu.sp = 0xabc; cpu.retired = 123;
        nc_fp_bank fp = cpu.fp; uint64_t x[32]; memcpy(x,cpu.x,sizeof(x));
        uint8_t ram[32] = {1,2,3}, saved[32]; memcpy(saved,ram,32);
        uint32_t w = word(load,modes[i],1,1,1);
        CHECK(vf_run(&cpu,(const uint8_t *)&w,4,ram,32,&code,1,perms,0) == VF_UNDEFINED_INSTRUCTION);
        CHECK(!memcmp(&cpu.fp,&fp,sizeof(fp)) && !memcmp(cpu.x,x,sizeof(x)));
        CHECK(!memcmp(ram,saved,32) && cpu.sp == 0xabc && cpu.retired == 123 && cpu.pc == 0);
    }
    CHECK(VF_PFR0_SCALAR_PROFILE == UINT64_C(0x00ff0011));
    CHECK(munmap(code.bytes,4096) == 0);
}
int main(void) {
    uint16_t endian = 1; CHECK(*(uint8_t *)&endian == 1);
    all_forms(); faults(); access_and_decode(); guarded_span(); admission();
    printf("{\"passed\":true,\"assertions\":%u}\n",checks);
}
