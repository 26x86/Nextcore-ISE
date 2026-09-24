/* Independently authored AArch64 pair-memory oracle. No OS payload. */
.section .text
.global _start
_start:
    adr x20, scratch
    add x20, x20, #512
    adr x9, vectors
    msr vbar_el1, x9
    isb
    mov x19, #0
    movz x4, #0x4444
    movk x4, #0x3333, lsl #16
    movk x4, #0x2222, lsl #32
    movk x4, #0x1111, lsl #48
    movz x5, #0x8888
    movk x5, #0x7777, lsl #16
    movk x5, #0x6666, lsl #32
    movk x5, #0x5555, lsl #48
    stp x4, x5, [x20, #-512]
    ldp x6, x7, [x20, #-512]
    cmp x6, x4
    b.ne fail
    cmp x7, x5
    b.ne fail
    stp w4, w5, [x20, #252]
    ldp w6, w7, [x20, #252]
    uxtw x9, w4
    cmp x6, x9
    b.ne fail
    uxtw x9, w5
    cmp x7, x9
    b.ne fail
    mov sp, x20
    stp x4, x5, [sp, #-16]!
    sub x9, x20, #16
    mov x10, sp
    cmp x10, x9
    b.ne fail
    ldp x6, x7, [sp], #16
    mov x10, sp
    cmp x10, x20
    b.ne fail
    cmp x6, x4
    b.ne fail
    cmp x7, x5
    b.ne fail
    mov x8, x20
    stp w4, w5, [x8], #8
    add x9, x20, #8
    cmp x8, x9
    b.ne fail
    ldp w6, w7, [x8, #-8]!
    cmp x8, x20
    b.ne fail
    cmp w6, w4
    b.ne fail
    cmp w7, w5
    b.ne fail
    stp xzr, xzr, [x20]
    ldp x6, xzr, [x20]
    cbnz x6, fail
    stp x4, x4, [x20]
    ldp x6, x7, [x20]
    cmp x6, x4
    b.ne fail
    cmp x7, x4
    b.ne fail
    /* QEMU omits gen_check_sp_alignment; its CPU model is not an oracle
       for SA/SA0. Those checks retain separate specification-based tests. */
    /* Ordinary alignment fault retains WnR and the distinct data class. */
    mov sp, x20
    add x9, x20, #1
    mov x10, #2
    msr sctlr_el1, x10
    isb
    mov x18, #0x96000000
    add x18, x18, #0x61
    stp x4, x5, [x9]
    cmp x19, #1
    b.ne fail
    msr sctlr_el1, xzr
    isb
    adr x1, success
    mov x0, #4
    hlt #0xf000
    mov x9, #0
    b exit
exception:
    mrs x16, esr_el1
    cmp x16, x18
    b.ne fail
    add x19, x19, #1
    mrs x16, elr_el1
    add x16, x16, #4
    msr elr_el1, x16
    eret
fail:
    adr x1, failure
    mov x0, #4
    hlt #0xf000
    mov x9, #1
exit:
    adr x1, exit_block
    str x9, [x1, #8]
    mov x0, #0x20
    hlt #0xf000
    b .
success: .asciz "PAIR_ORACLE: PASS widths,modes,sp,zr,alignment\n"
failure: .asciz "PAIR_ORACLE: FAIL\n"
.balign 8
exit_block: .quad 0x20026, 0
.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
.balign 16
scratch: .zero 1024
