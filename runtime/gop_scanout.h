/* SPDX-License-Identifier: BSD-4-Clause; independent public UEFI GOP binding. */
#ifndef NEXTCORE_GOP_SCANOUT_H
#define NEXTCORE_GOP_SCANOUT_H
#include "uefi.h"

/* B,G,R,reserved byte order; the same as a little-endian XRGB8888 guest pixel. */
typedef struct { uint8_t blue, green, red, reserved; } VF_GOP_PIXEL;
typedef struct {
    uint32_t version, width, height, pixel_format;
    uint32_t red_mask, green_mask, blue_mask, reserved_mask;
    uint32_t pixels_per_scanline;
} VF_GOP_INFO;
typedef struct {
    uint32_t max_mode, mode;
    VF_GOP_INFO *info;
    uint64_t info_size, framebuffer_base, framebuffer_size;
} VF_GOP_MODE;
typedef struct VF_GOP VF_GOP;
struct VF_GOP {
    void *query_mode, *set_mode;
    EFI_STATUS (VF_ABI *blt)(VF_GOP *, VF_GOP_PIXEL *, uint32_t,
        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    VF_GOP_MODE *mode;
};

_Static_assert(sizeof(VF_GOP_PIXEL) == 4, "GOP pixel ABI");
_Static_assert(sizeof(VF_GOP_INFO) == 36, "GOP mode info ABI");
_Static_assert(sizeof(VF_GOP_MODE) == 40, "GOP mode ABI");
_Static_assert(offsetof(VF_GOP, blt) == 16, "GOP Blt ABI");
_Static_assert(offsetof(VF_GOP, mode) == 24, "GOP Mode ABI");

/* Boot Services must be active. Memory belongs to the caller and guest execution
 * must be serialized with these synchronous calls. Nonzero EFI statuses (including
 * warnings) are preserved. No mode change or GPU shader operation is performed. */
EFI_STATUS VF_ABI vf_gop_present(EFI_BOOT_SERVICES *bs,
    const uint8_t *guest_ram, uint64_t ram_bytes, uint64_t offset,
    uint32_t width, uint32_t height, uint32_t stride,
    uint32_t destination_x, uint32_t destination_y);
EFI_STATUS VF_ABI vf_gop_readback(EFI_BOOT_SERVICES *bs,
    uint8_t *buffer, uint64_t buffer_bytes, uint64_t offset,
    uint32_t width, uint32_t height, uint32_t stride,
    uint32_t source_x, uint32_t source_y);
#endif
