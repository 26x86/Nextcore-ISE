/* SPDX-License-Identifier: BSD-4-Clause; authored EFI service fixture. */
#include "uefi.h"
#include "simd_copy.h"

static nc_fp_bank bank, expected;
static uint64_t x[31], expected_x[31];

static void seed(void) {
    bank.fpcr = UINT32_C(0x00800000);
    bank.fpsr = NC_FP_IOC | NC_FP_UFC | NC_FP_IXC;
    for (unsigned r = 0; r < 32; ++r) {
        bank.v[r][0] = UINT64_C(0x0123456789abcdef) ^
                        ((uint64_t)r * UINT64_C(0x0000000000010101));
        bank.v[r][1] = UINT64_C(0xfedcba9876543210) ^
                        ((uint64_t)r * UINT64_C(0x0000000100010001));
    }
    for (unsigned r = 0; r < 31; ++r)
        x[r] = UINT64_C(0xa5a5a5a5a5a5a5a5) ^
                ((uint64_t)r * UINT64_C(0x0000000000100010));
}

static void snapshot(void) {
    expected.fpcr = bank.fpcr;
    expected.fpsr = bank.fpsr;
    for (unsigned r = 0; r < 32; ++r) {
        expected.v[r][0] = bank.v[r][0];
        expected.v[r][1] = bank.v[r][1];
    }
    for (unsigned r = 0; r < 31; ++r) expected_x[r] = x[r];
}

static int matches(void) {
    if (bank.fpcr != expected.fpcr || bank.fpsr != expected.fpsr) return 0;
    for (unsigned r = 0; r < 32; ++r)
        if (bank.v[r][0] != expected.v[r][0] ||
            bank.v[r][1] != expected.v[r][1]) return 0;
    for (unsigned r = 0; r < 31; ++r)
        if (x[r] != expected_x[r]) return 0;
    return 1;
}

/* Expected values below are literals from authored register examples, not a
 * decoder or lane-shift oracle shared with the implementation. Selector 32
 * here means that no register changes in that bank. */
static int success(uint32_t word, unsigned vd, uint64_t low, uint64_t high,
                    unsigned xd, uint64_t integer) {
    snapshot();
    if (vd < 32) { expected.v[vd][0] = low; expected.v[vd][1] = high; }
    if (xd < 31) expected_x[xd] = integer;
    return nc_simd_execute_copy(&bank,x,word,1,1,UINT64_C(0x00300000),0)
               == NC_FP_OK && matches();
}

static int rejection(uint32_t word, nc_fp_result want, unsigned present,
                      unsigned el, uint64_t cpacr, unsigned higher) {
    snapshot();
    return nc_simd_execute_copy(&bank,x,word,present,el,cpacr,higher)
               == want && matches();
}

static int register_examples(void) {
    seed(); x[0] = UINT64_C(0x0123456789abcdef);
    /* dup v5.2d,x0 */
    if (!success(UINT32_C(0x4e080c05),5,UINT64_C(0x0123456789abcdef),
                 UINT64_C(0x0123456789abcdef),32,0)) return 0;

    seed(); bank.v[7][1] = UINT64_C(0x8000abcd12345678);
    /* dup v11.4h,v7.h[7]: Q=0 still reads the high source half. */
    if (!success(UINT32_C(0x0e1e04eb),11,UINT64_C(0x8000800080008000),0,32,0))
        return 0;

    seed(); x[7] = UINT64_C(0x8877665544332211);
    bank.v[1][0] = UINT64_C(0x1111222233334444);
    /* ins v1.d[1],x7 */
    if (!success(UINT32_C(0x4e181ce1),1,UINT64_C(0x1111222233334444),
                 UINT64_C(0x8877665544332211),32,0)) return 0;

    seed(); bank.v[0][0] = UINT64_C(0x0123456789abcdef);
    bank.v[0][1] = UINT64_C(0xfedcba9876543210);
    /* ins v0.d[0],v0.d[1]: capture the aliased source before writing. */
    if (!success(UINT32_C(0x6e084400),0,UINT64_C(0xfedcba9876543210),
                 UINT64_C(0xfedcba9876543210),32,0)) return 0;

    seed(); bank.v[0][1] = UINT64_C(0x8000000000000000); x[1] = UINT64_MAX;
    /* smov w1,v0.b[15]: sign-extend to W, then zero-extend into X. */
    if (!success(UINT32_C(0x0e1f2c01),32,0,0,1,UINT64_C(0x00000000ffffff80)))
        return 0;

    seed(); bank.v[18][1] = UINT64_C(0xffeeddccbbaa9988);
    /* umov x7,v18.d[1] */
    if (!success(UINT32_C(0x4e183e47),32,0,0,7,UINT64_C(0xffeeddccbbaa9988)))
        return 0;

    seed();
    /* dup v31.16b,wzr: vector 31 is real, integer 31 supplies zero. */
    if (!success(UINT32_C(0x4e010fff),31,0,0,32,0)) return 0;

    seed(); bank.v[31][0] = UINT64_C(0x1122334455667788);
    bank.v[31][1] = UINT64_C(0xdeadbeef01234567);
    /* ins v31.s[3],wzr: only the selected lane is cleared. */
    if (!success(UINT32_C(0x4e1c1fff),31,UINT64_C(0x1122334455667788),
                 UINT64_C(0x0000000001234567),32,0)) return 0;

    seed();
    /* umov wzr,v31.s[3] and smov xzr,v31.s[3] discard their results. */
    if (!success(UINT32_C(0x0e1c3fff),32,0,0,32,0)) return 0;
    if (!success(UINT32_C(0x4e1c2fff),32,0,0,32,0)) return 0;

    seed(); x[1] = UINT64_C(0x11223344556677a7);
    /* DUP GPR's upper imm5 bits are ignored for a byte-sized element. */
    if (!success(UINT32_C(0x4e1f0c25),5,UINT64_C(0xa7a7a7a7a7a7a7a7),
                 UINT64_C(0xa7a7a7a7a7a7a7a7),32,0)) return 0;

    seed(); bank.v[31][1] = UINT64_C(0x7f00000000000000);
    /* dup v0.8b,v31.b[15]: a real vector-31 high lane and upper clearing. */
    if (!success(UINT32_C(0x0e1f07e0),0,UINT64_C(0x7f7f7f7f7f7f7f7f),0,32,0))
        return 0;

    seed(); bank.v[0][1] = UINT64_C(0x8000000000000000);
    /* smov x2,v0.s[3] */
    if (!success(UINT32_C(0x4e1c2c02),32,0,0,2,UINT64_C(0xffffffff80000000)))
        return 0;

    seed(); bank.v[0][1] = UINT64_C(0xffffffff00000000); x[2] = UINT64_MAX;
    /* umov w2,v0.s[3] */
    if (!success(UINT32_C(0x0e1c3c02),32,0,0,2,UINT64_C(0x00000000ffffffff)))
        return 0;

    seed(); bank.v[2][1] = UINT64_C(0xabcd000000000000);
    bank.v[3][0] = UINT64_C(0x1122334455667788);
    bank.v[3][1] = UINT64_C(0x99aabbccddeeff00);
    /* INS H source imm4=15 selects H[7], ignoring its low immediate bit. */
    if (!success(UINT32_C(0x6e027c43),3,UINT64_C(0x112233445566abcd),
                 UINT64_C(0x99aabbccddeeff00),32,0)) return 0;
    return 1;
}

static int rejection_examples(void) {
    seed();
    if (!rejection(UINT32_C(0x0e080c05),NC_FP_UNSUPPORTED,1,1,0x300000,0))
        return 0; /* Q=0 doubleword DUP is reserved. */
    if (!rejection(UINT32_C(0x4e000c05),NC_FP_UNSUPPORTED,1,1,0x300000,0))
        return 0; /* Zero imm5 has no element size. */
    if (!rejection(UINT32_C(0xd503201f),NC_FP_UNSUPPORTED,0,3,UINT64_MAX,1))
        return 0; /* Decode precedes access. */
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_UNDEFINED,0,3,UINT64_MAX,1))
        return 0;
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_TRAP_EL1,1,0,0,0)) return 0;
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_UNSUPPORTED,1,2,0x300000,0))
        return 0;
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_INVALID,2,1,0x300000,0))
        return 0;
    bank.fpcr |= UINT32_C(1);
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_INVALID,1,1,0x300000,0))
        return 0;
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_TRAP_EL1,1,0,0,0)) return 0;
    seed(); bank.fpsr |= UINT32_C(0x20);
    if (!rejection(UINT32_C(0x4e080c05),NC_FP_INVALID,1,1,0x300000,0))
        return 0;
    seed(); snapshot();
    if (nc_simd_execute_copy((nc_fp_bank *)0,x,UINT32_C(0x4e080c05),
                             1,1,0x300000,0) != NC_FP_INVALID || !matches())
        return 0;
    if (nc_simd_execute_copy(&bank,(uint64_t *)0,UINT32_C(0x4e080c05),
                             1,1,0x300000,0) != NC_FP_INVALID || !matches())
        return 0;
    return 1;
}

static void print_marker(EFI_SYSTEM_TABLE *system, const char *message) {
    CHAR16 wide[64];
    unsigned n = 0;
    while (message[n] && n < 61) { wide[n] = (CHAR16)(uint8_t)message[n]; ++n; }
    wide[n++] = '\r'; wide[n++] = '\n'; wide[n] = 0;
    if (system && system->ConOut && system->ConOut->OutputString)
        (void)system->ConOut->OutputString(system->ConOut,wide);
#ifdef VF_QEMU_TEST
    for (unsigned i = 0; message[i]; ++i)
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)message[i]),
                          "Nd"((uint16_t)0xe9));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)'\n'),
                      "Nd"((uint16_t)0xe9));
#endif
}

EFI_STATUS VF_ABI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system) {
    (void)image;
    int passed = register_examples() && rejection_examples();
    print_marker(system,passed ? "NEXTCORE: SIMD_COPY_EFI_OK"
                               : "NEXTCORE: SIMD_COPY_EFI_FAIL");
#ifdef VF_QEMU_TEST
    __asm__ volatile("outl %0, %1" : : "a"((uint32_t)(passed ? 0x10 : 0x11)),
                      "Nd"((uint16_t)0xf4));
#endif
    return passed ? 0 : EFI_ABORTED;
}
