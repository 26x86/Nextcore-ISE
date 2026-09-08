/* Independently authored architectural test; no operating-system payload. */
.section .text
.global _start
_start:
    adr x20, lower
    adr x21, upper
    mov x9, #0x401
    str x9, [x20]
    mov x9, #0x40000000
    orr x9, x9, #0x400
    orr x9, x9, #1
    str x9, [x20, #8]
    mov x9, #0x80000000
    orr x9, x9, #0x400
    orr x9, x9, #1
    str x9, [x21]
    movz x22, #25
    movk x22, #0x8019, lsl #16 /* T1SZ=25, TG1=10; TG0=00 */
    movk x22, #5, lsl #32
    bl enable
    mov x0, #0xffffff8000000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl translate
    adr x1, pass4
    mov x0, #4
    hlt #0xf000
    bl disable
    /* T1SZ=26: the L1 initial index is only eight bits. Canonical bit38
     * must not select entry256 instead of the programmed entry0. */
    movk x22, #0x801a, lsl #16
    bl enable
    mov x0, #0xffffffc000000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl translate
    adr x1, pass4partial
    mov x0, #4
    hlt #0xf000
    bl disable
    /* 16 KiB L2 roots: code RAM index32, UART region index4. */
    mov x9, #0x40000000
    orr x9, x9, #0x400
    orr x9, x9, #1
    str x9, [x20, #256]
    mov x9, #0x08000000
    orr x9, x9, #0x400
    orr x9, x9, #1
    str x9, [x20, #32]
    movz x22, #0x801c /* T0SZ=28, TG0=10 */
    movk x22, #0x401c, lsl #16 /* T1SZ=28, TG1=01 */
    movk x22, #5, lsl #32
    bl enable
    mov x0, #0xfffffff000000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl translate
    adr x1, pass16
    mov x0, #4
    hlt #0xf000
    bl disable
    /* T1SZ=29: the L2 initial index is only ten bits. Canonical bit35
     * must not select entry1024 instead of the programmed entry0. */
    movk x22, #0x401d, lsl #16
    bl enable
    mov x0, #0xfffffff800000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl translate
    adr x1, pass16partial
    mov x0, #4
    hlt #0xf000
    mov x9, #0
    b exit

disable:
    msr sctlr_el1, xzr
    isb
    tlbi vmalle1
    dsb sy
    isb
    ret

enable:
    dsb sy
    msr ttbr0_el1, x20
    msr ttbr1_el1, x21
    msr tcr_el1, x22
    mov x9, #0xff
    msr mair_el1, x9
    isb
    mov x9, #1
    msr sctlr_el1, x9
    isb
    ret

translate:
    at s1e1r, x0
    isb
    mrs x9, par_el1
    tbnz x9, #0, fail
    and x9, x9, #0x0000fffffffff000
    mov x10, #0x80000000
    add x10, x10, #0x1000
    cmp x9, x10
    b.ne fail
    ret

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
pass4: .asciz "MMU_ORACLE: 4K_TTBR1_PA_OK\n"
pass4partial: .asciz "MMU_ORACLE: 4K_TTBR1_PARTIAL_PA_OK\n"
pass16: .asciz "MMU_ORACLE: 16K_TTBR1_PA_OK\n"
pass16partial: .asciz "MMU_ORACLE: 16K_TTBR1_PARTIAL_PA_OK\n"
failure: .asciz "MMU_ORACLE: FAIL\n"
.balign 8
exit_block: .quad 0x20026, 0
.balign 16384
lower: .zero 16384
upper: .zero 16384
