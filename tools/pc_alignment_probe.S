/* Authored AArch64 indirect-branch alignment oracle; no operating-system code. */
#ifndef EXPECTED_ESR
#define EXPECTED_ESR 0x8a000000
#endif
.section .text
.global _start
_start:
    adr x9, vectors
    msr vbar_el1, x9
    isb
    adr x20, target
    add x20, x20, #2
    br x20
    b fail
target:
    b fail
exception:
    mrs x0, esr_el1
    mov x1, #EXPECTED_ESR
    cmp x0, x1
    b.ne fail
    mrs x0, far_el1
    cmp x0, x20
    b.ne fail
    mrs x0, elr_el1
    cmp x0, x20
    b.ne fail
    adr x1, success
    mov x0, #4
    hlt #0xf000
    mov x9, #0
    b exit
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
success: .asciz "PC_ALIGNMENT_ORACLE: PASS ESR=8a000000 FAR=target ELR=target\n"
failure: .asciz "PC_ALIGNMENT_ORACLE: FAIL\n"
.balign 8
exit_block: .quad 0x20026, 0
.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
