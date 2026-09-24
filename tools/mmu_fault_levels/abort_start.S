.section .text.start,"ax"
.global _start
_start:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne abort_fatal
    ldr x0, =el2_stack_top
    mov sp, x0
    mov x0, #(1 << 31)
    msr hcr_el2, x0
    msr sctlr_el2, xzr
    adr x0, el2_vectors
    msr vbar_el2, x0
    adr x0, el1_vectors
    msr vbar_el1, x0
    ldr x0, =el1_stack_top
    msr sp_el1, x0
    isb
    bl abort_main
abort_fatal:
    mov x0, #0x20
    adr x1, failed_exit
    hlt #0xf000
    b .
.balign 8
failed_exit: .quad 0x20026, 1

.global enter_el1
enter_el1:
    stp x29, x30, [sp, #-16]!
    adr x9, el1_entry
    msr elr_el2, x9
    mov x9, #0x3c5
    msr spsr_el2, x9
    eret
el1_entry:
    cmp x1, #1
    b.eq write_access
    cmp x1, #2
    b.eq execute_access
.global read_access
read_access:
    ldr x2, [x0]
    b no_fault
.global write_access
write_access:
    str xzr, [x0]
    b no_fault
execute_access:
    blr x0
no_fault:
    mov x3, #0
    hvc #0
    b .
el1_sync:
    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1
    mov x3, #1
    hvc #0
    b .
el2_sync:
    mrs x9, esr_el2
    lsr x9, x9, #26
    cmp x9, #0x16
    b.ne abort_fatal
    adrp x9, abort_result
    add x9, x9, :lo12:abort_result
    stp x0, x1, [x9]
    stp x2, x3, [x9, #16]
    ldp x29, x30, [sp], #16
    ret
.balign 2048
el1_vectors:
.rept 16
    b el1_sync
    .balign 128
.endr
.balign 2048
el2_vectors:
.rept 16
    b el2_sync
    .balign 128
.endr
.section .bss
.balign 16
el2_stack: .skip 32768
el2_stack_top:
el1_stack: .skip 32768
el1_stack_top:
