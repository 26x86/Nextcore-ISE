/* Authored Arm physical-width oracle; no OS payload. */
.section .text
.global _start
_start:
    adr x20, tables
    mov x9, #0x40000000
    orr x9, x9, #0x400
    orr x9, x9, #1
    str x9, [x20, #8]
    str x9, [x20, #256]
    mov x21, #0x100000000
    mov x9, #0x100000000
    orr x9, x9, #1
    orr x9, x9, #0x400
    str x9, [x20]
    movz x22, #25
    movk x22, #32921, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_address_size
    adr x1, marker1
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #1
    orr x9, x9, #0x400
    str x9, [x20]
    movz x22, #25
    movk x22, #32921, lsl #16
    movk x22, #1, lsl #32
    bl enable
    mov x0, #0
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_pa
    adr x1, marker2
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #3
    str x9, [x20]
    movz x22, #25
    movk x22, #32921, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_address_size
    adr x1, marker3
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #1
    orr x9, x9, #0x400
    str x9, [x20]
    movz x22, #25
    movk x22, #32793, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0xffffff8000000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_address_size
    adr x1, marker4
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #1
    orr x9, x9, #0x400
    str x9, [x20]
    movz x22, #32796
    movk x22, #16540, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_address_size
    adr x1, marker5
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #1
    orr x9, x9, #0x400
    str x9, [x20]
    movz x22, #32796
    movk x22, #16540, lsl #16
    movk x22, #1, lsl #32
    bl enable
    mov x0, #0
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_pa
    adr x1, marker6
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #3
    str x9, [x20]
    movz x22, #32796
    movk x22, #16540, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_address_size
    adr x1, marker7
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0x100000000
    orr x9, x9, #1
    orr x9, x9, #0x400
    str x9, [x20]
    movz x22, #32796
    movk x22, #16412, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0xfffffff000000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_address_size
    adr x1, marker8
    mov x0, #4
    hlt #0xf000
    bl disable
    adr x9, page_l2
    orr x9, x9, #3
    str x9, [x20]
    adr x9, page_l3
    orr x9, x9, #3
    adr x10, page_l2
    str x9, [x10]
    adr x10, page_l3
    add x10, x10, #8
    mov x9, #0x100000000
    add x9, x9, #0x1000
    orr x9, x9, #3
    orr x9, x9, #0x400
    str x9, [x10]
    movz x22, #25
    movk x22, #32921, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0x1234
    bl expect_address_size
    adr x1, marker9
    mov x0, #4
    hlt #0xf000
    bl disable
    adr x9, page_l2
    orr x9, x9, #3
    str x9, [x20]
    adr x9, page_l3
    orr x9, x9, #3
    adr x10, page_l2
    str x9, [x10]
    adr x10, page_l3
    add x10, x10, #8
    mov x9, #0x100000000
    add x9, x9, #0x1000
    orr x9, x9, #3
    orr x9, x9, #0x400
    str x9, [x10]
    movz x22, #25
    movk x22, #32921, lsl #16
    movk x22, #1, lsl #32
    bl enable
    mov x0, #0x1234
    bl expect_pa
    adr x1, marker10
    mov x0, #4
    hlt #0xf000
    bl disable
    adr x9, page_l2
    orr x9, x9, #3
    str x9, [x20]
    adr x10, page_l2
    mov x9, #0x100000000
    orr x9, x9, #3
    orr x9, x9, #0x400
    str x9, [x10]
    movz x22, #32796
    movk x22, #16540, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0x1234
    bl expect_address_size
    adr x1, marker11
    mov x0, #4
    hlt #0xf000
    bl disable
    adr x9, page_l2
    orr x9, x9, #3
    str x9, [x20]
    adr x10, page_l2
    mov x9, #0x100000000
    orr x9, x9, #3
    orr x9, x9, #0x400
    str x9, [x10]
    movz x22, #32796
    movk x22, #16540, lsl #16
    movk x22, #1, lsl #32
    bl enable
    mov x0, #0x1234
    bl expect_pa
    adr x1, marker12
    mov x0, #4
    hlt #0xf000
    bl disable
    /* DS=0 reserves 16 KiB L1 block descriptors. Keep code mapped by
       a valid L2 table at L1[0], and probe the reserved block at L1[1]. */
    adr x9, page_l2
    orr x9, x9, #3
    str x9, [x20]
    mov x9, #0x401
    str x9, [x20, #8]
    adr x10, page_l2
    mov x9, #0x40000000
    orr x9, x9, #0x400
    orr x9, x9, #1
    str x9, [x10, #256]
    movz x22, #32793
    movk x22, #16537, lsl #16
    movk x22, #0, lsl #32
    bl enable
    mov x0, #0x1000000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_translation
    adr x1, marker13
    mov x0, #4
    hlt #0xf000
    mov x0, #0x1100000000
    add x0, x0, #0x1000
    add x0, x0, #0x234
    bl expect_translation
    adr x1, marker14
    mov x0, #4
    hlt #0xf000
    bl disable
    mov x9, #0
    b exit
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
disable:
    msr sctlr_el1, xzr
    isb
    tlbi vmalle1
    dsb sy
    isb
    ret
expect_address_size:
    at s1e1r, x0
    isb
    mrs x9, par_el1
    tbz x9, #0, fail
    ubfx x10, x9, #1, #6
    cmp x10, #3
    b.hi fail
    ret
expect_translation:
    at s1e1r, x0
    isb
    mrs x9, par_el1
    tbz x9, #0, fail
    ubfx x10, x9, #1, #6
    cmp x10, #4
    b.lo fail
    cmp x10, #7
    b.hi fail
    ret
expect_pa:
    at s1e1r, x0
    isb
    mrs x9, par_el1
    tbnz x9, #0, fail
    and x9, x9, #0x0000fffffffff000
    mov x10, #0x100000000
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
marker1: .asciz "MMU_PA_ORACLE: 4K_LEAF32_FAULT\n"
marker2: .asciz "MMU_PA_ORACLE: 4K_LEAF36_OK\n"
marker3: .asciz "MMU_PA_ORACLE: 4K_TABLE32_FAULT\n"
marker4: .asciz "MMU_PA_ORACLE: 4K_ROOT32_FAULT\n"
marker5: .asciz "MMU_PA_ORACLE: 16K_LEAF32_FAULT\n"
marker6: .asciz "MMU_PA_ORACLE: 16K_LEAF36_OK\n"
marker7: .asciz "MMU_PA_ORACLE: 16K_TABLE32_FAULT\n"
marker8: .asciz "MMU_PA_ORACLE: 16K_ROOT32_FAULT\n"
marker9: .asciz "MMU_PA_ORACLE: 4K_PAGE32_FAULT\n"
marker10: .asciz "MMU_PA_ORACLE: 4K_PAGE36_OK\n"
marker11: .asciz "MMU_PA_ORACLE: 16K_PAGE32_FAULT\n"
marker12: .asciz "MMU_PA_ORACLE: 16K_PAGE36_OK\n"
marker13: .asciz "MMU_PA_ORACLE: 16K_L1_LOW32_TRANSLATION\n"
marker14: .asciz "MMU_PA_ORACLE: 16K_L1_HIGH32_TRANSLATION\n"
failure: .asciz "MMU_PA_ORACLE: FAIL\n"
.balign 8
exit_block: .quad 0x20026, 0
.balign 16384
tables: .zero 16384

.balign 16384
page_l2: .zero 16384
.balign 16384
page_l3: .zero 16384
