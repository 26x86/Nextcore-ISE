#include "sart_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_sart_v1 s;
    uint32_t v = 0;
    uint32_t addr_pages = 0;
    uint32_t size_pages = 0;
    uint32_t flags = 0;
    uint64_t pa = 0;
    unsigned region = 99u;

    assert(!vf_sart_init(&s, VF_SART_VERSION_1));
    assert(!vf_sart_write(&s, VF_SART_REG_SIZE_OFF(0), 4, 0x0007FFFFu));
    assert(!vf_sart_write(&s, VF_SART_REG_ADDR_OFF(0), 4, 0x00123456u));
    assert(!vf_sart_read(&s, VF_SART_REG_SIZE_OFF(0), 4, &v) && v == 0x0007FFFFu);
    assert(!vf_sart_read(&s, VF_SART_REG_ADDR_OFF(0), 4, &v) && v == 0x00123456u);

    assert(!vf_sart_init(&s, VF_SART_VERSION_3));
    assert(!vf_sart_write(&s, VF_SART_REG_FLAGS_OFF(0), 4, 0x00000001u));
    assert(!vf_sart_write(&s, VF_SART_REG_ADDR_OFF(0), 4, 0x0ABCDEF0u));
    assert(!vf_sart_write(&s, VF_SART_REG_SIZE_V3_OFF(0), 4, 0x00001000u));
    assert(!vf_sart_read(&s, VF_SART_REG_SIZE_V3_OFF(0), 4, &v) && v == 0x00001000u);
    assert(vf_sart_generation(&s) == 3u);

    assert(vf_sart_read(&s, VF_SART_REG_SIZE, 4, &v) == -1);
    assert(vf_sart_write(&s, 0x001u, 4, 0u) == -1);
    assert(vf_sart_read(&s, 0x002u, 4, &v) == -1);
    assert(vf_sart_init(&s, 99u) == -1);

    /* Version-1: size in low bits, flags = word & ~size_mask; identity hit. */
    assert(!vf_sart_init(&s, VF_SART_VERSION_1));
    assert(!vf_sart_write(&s, VF_SART_REG_SIZE_OFF(0), 4, 0x10000010u)); /* flags + size=0x10 */
    assert(!vf_sart_write(&s, VF_SART_REG_ADDR_OFF(0), 4, 0x00000100u));
    assert(!vf_sart_region_decode(&s, 0, &addr_pages, &size_pages, &flags));
    assert(addr_pages == 0x100u && size_pages == 0x10u && flags == 0x10000000u);
    assert(vf_sart_translate(&s, 0x100000ull + 0x123u, &pa, &region) == 1);
    assert(pa == 0x100000ull && region == 0u);
    assert(vf_sart_translate(&s, 0x200000ull, &pa, &region) == 0);
    assert(pa == 0x200000ull && region == VF_SART_NUM_REGIONS);
    assert(vf_sart_generation(&s) == 2u);

    /* Version-2: wider size mask; zero flags → miss even inside span. */
    assert(!vf_sart_init(&s, VF_SART_VERSION_2));
    assert(!vf_sart_write(&s, VF_SART_REG_SIZE_OFF(1), 4, 0x00000020u)); /* size only */
    assert(!vf_sart_write(&s, VF_SART_REG_ADDR_OFF(1), 4, 0x00000200u));
    assert(!vf_sart_region_decode(&s, 1, &addr_pages, &size_pages, &flags));
    assert(addr_pages == 0x200u && size_pages == 0x20u && flags == 0u);
    assert(vf_sart_translate(&s, 0x200000ull, &pa, &region) == 0);

    /* Version-3: separate flags / size banks. */
    assert(!vf_sart_init(&s, VF_SART_VERSION_3));
    assert(!vf_sart_write(&s, VF_SART_REG_FLAGS_OFF(2), 4, 0x3u));
    assert(!vf_sart_write(&s, VF_SART_REG_ADDR_OFF(2), 4, 0x00000400u));
    assert(!vf_sart_write(&s, VF_SART_REG_SIZE_V3_OFF(2), 4, 0x00000008u));
    assert(vf_sart_translate(&s, 0x407000ull, &pa, &region) == 1);
    assert(pa == 0x407000ull && region == 2u);
    assert(vf_sart_translate(&s, 1ull << VF_SART_MAX_VA_BITS, &pa, &region) == -1);
    assert(vf_sart_translate(&s, 0, 0, 0) == -1);

    puts("PASS SART v1 region table + translate stub");
    return 0;
}
