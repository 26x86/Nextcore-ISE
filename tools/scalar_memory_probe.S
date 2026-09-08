/* Independently authored unsigned-offset integer scalar oracle. */
.section .text
.global _start
_start:
    adr x20, scratch
    mov sp, x20
    adr x9, vectors
    msr vbar_el1, x9
    isb
    mov x19, #0
    movz x4, #0xfe80
    movk x4, #0x8765, lsl #16
    movk x4, #0xba98, lsl #32
    movk x4, #0xfedc, lsl #48
    strb w4, [x20, #4095]
    ldrb w6, [x20, #4095]
    cmp x6, #128
    b.ne fail
    ldrsb x6, [x20, #4095]
    mov x10, #-128
    cmp x6, x10
    b.ne fail
    ldrsb w6, [x20, #4095]
    mov w10, #-128
    cmp x6, x10
    b.ne fail
    strh w4, [x20, #8190]
    ldrh w6, [x20, #8190]
    mov x10, #0xfe80
    cmp x6, x10
    b.ne fail
    ldrsh x6, [x20, #8190]
    mov x10, #-384
    cmp x6, x10
    b.ne fail
    ldrsh w6, [x20, #8190]
    mov w10, #-384
    cmp x6, x10
    b.ne fail
    str w4, [x20, #16380]
    ldr w6, [x20, #16380]
    movz x10, #0xfe80
    movk x10, #0x8765, lsl #16
    cmp x6, x10
    b.ne fail
    ldrsw x6, [x20, #16380]
    movk x10, #0xffff, lsl #32
    movk x10, #0xffff, lsl #48
    cmp x6, x10
    b.ne fail
    str x4, [x20, #32760]
    ldr x6, [x20, #32760]
    cmp x6, x4
    b.ne fail
    str x4, [sp, #8]
    ldr x6, [sp, #8]
    cmp x6, x4
    b.ne fail
    str xzr, [x20, #8]
    ldr x6, [x20, #8]
    cbnz x6, fail
    ldr xzr, [sp, #8]
    mov x6, sp
    cmp x6, x20
    b.ne fail
    str x4, [x20, #8]
    mov x8, x20
    ldr x8, [x8, #8]
    cmp x8, x4
    b.ne fail
    /* Positive sign boundary and upper-register clearing. */
    mov x4, #127
    strb w4, [x20]
    mov x6, #-1
    ldrsb w6, [x20]
    cmp x6, #127
    b.ne fail
    /* Scalar alignment: A=1 is separately modeled by the oracle. */
    mov x10, #2
    msr sctlr_el1, x10
    isb
    add x9, x20, #1
    mov x18, #0x96000000
    add x18, x18, #0x61
    str w4, [x9]
    mov x18, #0x96000000
    add x18, x18, #0x21
    ldrsh x6, [x9]
    cmp x19, #2
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
success: .asciz "SCALAR_ORACLE: PASS 13forms,imm12,sign,sp,zr,alignment\n"
failure: .asciz "SCALAR_ORACLE: FAIL\n"
.balign 8
exit_block: .quad 0x20026, 0
.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
.balign 16
scratch: .zero 32768
