/* SPDX-License-Identifier: BSD-4-Clause; independently authored protocol fixture. */
#include "gop_scanout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static EFI_BOOT_SERVICES bs;
static VF_GOP gop;
static VF_GOP_MODE mode;
static VF_GOP_INFO info;
static _Alignas(4) uint8_t video[32 + 16 * 16 * 4 + 32];
static EFI_STATUS locate_status, blt_status;
static unsigned locate_calls, blt_calls;
static int return_null;

static EFI_STATUS VF_ABI mock_blt(VF_GOP *self, VF_GOP_PIXEL *pixels, uint32_t op,
    uint64_t sx, uint64_t sy, uint64_t dx, uint64_t dy,
    uint64_t width, uint64_t height, uint64_t delta) {
    assert(self == &gop && pixels && (op == 1 || op == 2));
    assert(width && height && delta >= width * 4);
    blt_calls++;
    if (blt_status) return blt_status;
    uint8_t *buffer = (uint8_t *)pixels;
    for (uint64_t y = 0; y < height; y++) {
        for (uint64_t x = 0; x < width; x++) {
            if (op == 2) {
                assert(sx == 0 && sy == 0 && dx + width <= 16 && dy + height <= 16);
                memcpy(video + 32 + ((dy + y) * 16 + dx + x) * 4,
                       buffer + y * delta + x * 4, 4);
            } else {
                assert(dx == 0 && dy == 0 && sx + width <= 16 && sy + height <= 16);
                memcpy(buffer + y * delta + x * 4,
                       video + 32 + ((sy + y) * 16 + sx + x) * 4, 4);
            }
        }
    }
    return 0;
}
static EFI_STATUS VF_ABI mock_locate(EFI_GUID *guid, void *registration, void **out) {
    static const uint8_t tail[8] = {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a};
    assert(guid->a == 0x9042a9de && guid->b == 0x23dc && guid->c == 0x4a38);
    assert(!memcmp(guid->d, tail, 8) && registration == 0);
    locate_calls++;
    *out = return_null ? 0 : &gop;
    return locate_status;
}
static void reset(void) {
    memset(&bs, 0, sizeof(bs));
    info = (VF_GOP_INFO){.width=16, .height=16, .pixel_format=1, .pixels_per_scanline=16};
    mode = (VF_GOP_MODE){.max_mode=1, .info=&info, .info_size=sizeof(info)};
    gop = (VF_GOP){.blt=mock_blt, .mode=&mode};
    bs.LocateProtocol = mock_locate;
    locate_status = blt_status = locate_calls = blt_calls = return_null = 0;
    memset(video, 0x5a, sizeof(video));
}
static void invalid_source(const uint8_t *memory, uint64_t size, uint64_t offset,
                           uint32_t width, uint32_t height, uint32_t stride) {
    reset();
    assert(vf_gop_present(&bs,memory,size,offset,width,height,stride,0,0)==EFI_INVALID_PARAMETER);
    assert(locate_calls==0 && blt_calls==0);
}
int main(void) {
    _Alignas(4) uint8_t source[16 + 3 * 24 + 16];
    _Alignas(4) uint8_t output[sizeof(source)];
    memset(source, 0xa5, sizeof(source));
    for (unsigned y=0;y<3;y++) for (unsigned x=0;x<4;x++) {
        uint8_t *pixel=source+16+y*24+x*4;
        pixel[0]=(uint8_t)(20+x); pixel[1]=(uint8_t)(40+y);
        pixel[2]=(uint8_t)(70+x+y); pixel[3]=0;
    }
    uint8_t original[sizeof(source)];
    memcpy(original,source,sizeof(source));
    reset();
    /* Deliberately exclude final-row padding: only actual pixels are readable. */
    assert(vf_gop_present(&bs,source,16+2*24+16,16,4,3,24,2,5)==0);
    assert(locate_calls==1 && blt_calls==1);
    for (unsigned y=0;y<16;y++) for (unsigned x=0;x<16;x++) {
        const uint8_t *pixel=video+32+(y*16+x)*4;
        if (x>=2 && x<6 && y>=5 && y<8)
            assert(!memcmp(pixel,source+16+(y-5)*24+(x-2)*4,4));
        else for (unsigned c=0;c<4;c++) assert(pixel[c]==0x5a);
    }
    for (unsigned i=0;i<32;i++) assert(video[i]==0x5a && video[sizeof(video)-32+i]==0x5a);
    assert(!memcmp(source,original,sizeof(source)));
    memset(output,0xc3,sizeof(output));
    assert(vf_gop_readback(&bs,output,16+2*24+16,16,4,3,24,2,5)==0);
    for (unsigned i=0;i<sizeof(output);i++) {
        int is_pixel=i>=16 && i<16+3*24 && (i-16)%24<16;
        assert(output[i]==(is_pixel?source[i]:0xc3));
    }
    /* BltOnly need not publish a directly mapped physical framebuffer. */
    info.pixel_format=3; info.pixels_per_scanline=0;
    assert(vf_gop_present(&bs,source,sizeof(source),16,4,3,24,0,0)==0);

    invalid_source(0,16,0,1,1,4);
    invalid_source(source,0,0,1,1,4);
    invalid_source(source+1,sizeof(source)-1,0,1,1,4);
    invalid_source(source,sizeof(source),1,1,1,4);
    invalid_source(source,sizeof(source),0,1,1,5);
    invalid_source(source,sizeof(source),0,0,1,4);
    invalid_source(source,sizeof(source),0,1,0,4);
    invalid_source(source,sizeof(source),0,8193,1,32772);
    invalid_source(source,sizeof(source),0,1,8193,4);
    invalid_source(source,sizeof(source),0,4,1,12);
    invalid_source(source,sizeof(source),UINT64_MAX-3,1,1,4);
    invalid_source(source,16+2*24+15,16,4,3,24);
    invalid_source((const uint8_t *)(UINTPTR_MAX-7),16,0,1,1,4);
    invalid_source(source,UINT64_MAX,0,1,1,4);

    for (unsigned kind=0;kind<10;kind++) {
        reset();
        switch(kind) {
            case 0:return_null=1;break;
            case 1:gop.blt=0;break;
            case 2:gop.mode=0;break;
            case 3:mode.info=0;break;
            case 4:mode.info_size=sizeof(info)-1;break;
            case 5:mode.max_mode=0;break;
            case 6:mode.mode=1;break;
            case 7:info.version=1;break;
            case 8:info.pixel_format=4;break;
            case 9:info.width=0;break;
        }
        assert(vf_gop_present(&bs,source,sizeof(source),16,4,3,24,0,0)==EFI_UNSUPPORTED);
        assert(locate_calls==1 && blt_calls==0);
    }
    reset();
    assert(vf_gop_present(0,source,sizeof(source),16,4,3,24,0,0)==EFI_UNSUPPORTED);
    bs.LocateProtocol=0;
    assert(vf_gop_present(&bs,source,sizeof(source),16,4,3,24,0,0)==EFI_UNSUPPORTED);
    reset();
    for (unsigned i=0;i<3;i++) {
        uint32_t x=i==0?13:i==1?UINT32_MAX:0;
        uint32_t y=i==2?14:0;
        assert(vf_gop_present(&bs,source,sizeof(source),16,4,3,24,x,y)==EFI_INVALID_PARAMETER);
        assert(vf_gop_readback(&bs,output,sizeof(output),16,4,3,24,x,y)==EFI_INVALID_PARAMETER);
        assert(blt_calls==0);
    }
    const EFI_STATUS statuses[]={EFI_NOT_FOUND,EFI_ABORTED,1};
    for (unsigned i=0;i<3;i++) {
        reset();locate_status=statuses[i];
        assert(vf_gop_present(&bs,source,sizeof(source),16,4,3,24,0,0)==statuses[i]);
        assert(blt_calls==0);
        reset();blt_status=statuses[i];
        assert(vf_gop_present(&bs,source,sizeof(source),16,4,3,24,0,0)==statuses[i]);
        assert(blt_calls==1);
        assert(vf_gop_readback(&bs,output,sizeof(output),16,4,3,24,0,0)==statuses[i]);
        assert(blt_calls==2);
    }
    puts("{\"gop_protocol_host_tests_passed\":true,\"physical_display_verified\":false,\"metal_verified\":false}");
    return 0;
}
