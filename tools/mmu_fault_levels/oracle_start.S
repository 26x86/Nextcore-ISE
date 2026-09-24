.section .text.start,"ax"
.global _start
_start:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne bad_el
    ldr x0, =stack_top
    mov sp, x0
    mov x0, #(1 << 31)
    msr hcr_el2, x0
    msr sctlr_el2, xzr
    msr sctlr_el1, xzr
    isb
    bl oracle_main
bad_el:
    mov x0, #0x20
    adr x1, failed_exit
    hlt #0xf000
    b .
.balign 8
failed_exit: .quad 0x20026, 1
.section .bss
.balign 16
stack: .skip 65536
stack_top:
