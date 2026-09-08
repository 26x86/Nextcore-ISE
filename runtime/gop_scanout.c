/* SPDX-License-Identifier: BSD-4-Clause */
#include "gop_scanout.h"

static EFI_GUID gop_guid = {0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

static int valid_buffer(const void *buffer, uint64_t bytes, uint64_t offset,
                        uint32_t width, uint32_t height, uint32_t stride) {
    if (!buffer || !bytes || bytes > UINTPTR_MAX ||
        (uintptr_t)buffer > UINTPTR_MAX - bytes ||
        ((uintptr_t)buffer & 3) || (offset & 3) || (stride & 3) ||
        !width || !height || width > 8192 || height > 8192 ||
        stride < (uint64_t)width * 4 || offset > bytes) return 0;
    /* u32 stride/height make this product representable in u64. Last-row
     * padding is excluded; firmware reads/writes only width pixels per row. */
    uint64_t span = (uint64_t)(height - 1) * stride + (uint64_t)width * 4;
    return span <= (UINT64_C(256) << 20) && span <= bytes - offset;
}

static EFI_STATUS transfer(EFI_BOOT_SERVICES *bs, void *buffer, uint64_t bytes,
                           uint64_t offset, uint32_t width, uint32_t height,
                           uint32_t stride, uint32_t x, uint32_t y, int readback) {
    if (!valid_buffer(buffer, bytes, offset, width, height, stride))
        return EFI_INVALID_PARAMETER;
    if (!bs || !bs->LocateProtocol) return EFI_UNSUPPORTED;
    VF_GOP *gop = 0;
    EFI_STATUS status = bs->LocateProtocol(&gop_guid, 0, (void **)&gop);
    if (status) return status;
    if (!gop || !gop->blt || !gop->mode || !gop->mode->info ||
        gop->mode->info_size < sizeof(VF_GOP_INFO) || !gop->mode->max_mode ||
        gop->mode->mode >= gop->mode->max_mode) return EFI_UNSUPPORTED;
    const VF_GOP_INFO *info = gop->mode->info;
    if (info->version || info->pixel_format >= 4 || !info->width || !info->height)
        return EFI_UNSUPPORTED;
    if (x > info->width || y > info->height ||
        width > info->width - x || height > info->height - y)
        return EFI_INVALID_PARAMETER;
    VF_GOP_PIXEL *pixels = (VF_GOP_PIXEL *)((uint8_t *)buffer + (size_t)offset);
    /* EfiBltBufferToVideo=2, EfiBltVideoToBltBuffer=1. In both cases Delta
     * describes caller memory; the firmware owns the actual video format. */
    if (readback)
        return gop->blt(gop, pixels, 1, x, y, 0, 0, width, height, stride);
    return gop->blt(gop, pixels, 2, 0, 0, x, y, width, height, stride);
}

EFI_STATUS VF_ABI vf_gop_present(EFI_BOOT_SERVICES *bs,
    const uint8_t *guest_ram, uint64_t ram_bytes, uint64_t offset,
    uint32_t width, uint32_t height, uint32_t stride,
    uint32_t destination_x, uint32_t destination_y) {
    /* The public GOP Blt ABI uses an IN/OUT pointer. BufferToVideo consumes it
     * without modifying guest bytes; the cast matches that public contract. */
    return transfer(bs, (void *)guest_ram, ram_bytes, offset, width, height,
                    stride, destination_x, destination_y, 0);
}

EFI_STATUS VF_ABI vf_gop_readback(EFI_BOOT_SERVICES *bs,
    uint8_t *buffer, uint64_t buffer_bytes, uint64_t offset,
    uint32_t width, uint32_t height, uint32_t stride,
    uint32_t source_x, uint32_t source_y) {
    return transfer(bs, buffer, buffer_bytes, offset, width, height,
                    stride, source_x, source_y, 1);
}
