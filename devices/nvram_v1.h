/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_NVRAM_V1_H
#define VENFIRE_NVRAM_V1_H
#include <stddef.h>
#include <stdint.h>

/* Apple NVRAM is an NVMe namespace child (Golden Gate investigation values). */
#define VF_NVRAM_NSID              5u
#define VF_NVRAM_NSTYPE            5u

#define VF_NVRAM_BANK_MAX_LEN      0x2000u
#define VF_NVRAM_COMMON_PART_LEN   0x7f0u
#define VF_NVRAM_ENV_NAME_MAX      64u
#define VF_NVRAM_ENV_VALUE_MAX     256u
#define VF_NVRAM_MAX_ENV           32u
#define VF_NVRAM_MAX_PARTS         8u

#define VF_NVRAM_CHRP_SIG          0x5au
#define VF_NVRAM_CHRP_PART_SYSTEM  0x70u
#define VF_NVRAM_CHRP_PART_FREE    0x7fu
#define VF_NVRAM_COMMON_SIG        0x70u

#define VF_NVRAM_PANIC_PART_NAME   "APL,OSXPani"

#pragma pack(push, 1)
typedef struct {
    uint8_t signature;
    uint8_t checksum;
    uint16_t len; /* big-endian, length in 16-byte units */
    char name[12];
} vf_chrp_part_hdr;

typedef struct {
    vf_chrp_part_hdr chrp;
    uint32_t adler;
    uint32_t generation;
    uint8_t padding[8];
} vf_apple_nvram_hdr;
#pragma pack(pop)

typedef struct {
    char name[VF_NVRAM_ENV_NAME_MAX];
    char value[VF_NVRAM_ENV_VALUE_MAX];
    uint32_t flags;
    unsigned parsed_uint;
} vf_nvram_env;

typedef struct {
    uint8_t sig;
    char name[16];
    size_t len;
    uint8_t data[VF_NVRAM_COMMON_PART_LEN];
} vf_nvram_part;

typedef struct {
    vf_nvram_env env[VF_NVRAM_MAX_ENV];
    unsigned env_count;
    vf_nvram_part parts[VF_NVRAM_MAX_PARTS];
    unsigned part_count;
    size_t bank_len;
    uint32_t generation;
} vf_nvram_v1;

typedef struct {
    uint32_t nsid;
    uint32_t nstype;
    const char *qom_type;
} vf_nvram_ns_info;

int vf_nvram_init(vf_nvram_v1 *);
void vf_nvram_get_namespace(const vf_nvram_ns_info **meta);

const char *vf_nvram_env_get(const vf_nvram_v1 *, const char *name);
unsigned vf_nvram_env_get_uint(const vf_nvram_v1 *, const char *name,
                               unsigned default_val);
int vf_nvram_env_get_bool(const vf_nvram_v1 *, const char *name, int default_val);
int vf_nvram_env_set(vf_nvram_v1 *, const char *name, const char *value,
                     uint32_t flags);
int vf_nvram_env_unset(vf_nvram_v1 *, const char *name);

/* Parse an on-disk bank buffer; fails closed on bad checksum/adler/layout. */
int vf_nvram_parse_bank(vf_nvram_v1 *, const void *buf, size_t len);
/*
 * Ingest a caller-supplied on-disk bank image (NVMe namespace-5 layout).
 * Rejects empty or oversize buffers; otherwise identical fail-closed rules as
 * vf_nvram_parse_bank. Synthetic/fixture banks are allowed when marked by the
 * caller; never invent Apple proprietary bank blobs.
 */
int vf_nvram_ingest_bank(vf_nvram_v1 *, const void *buf, size_t len);
/*
 * Clean-room NVMe namespace-5 block backend ops (behavior reference:
 * qemu-t8030 apple_nvram_load/save blk_getlength + blk_pread + blk_pwrite).
 * No QEMU/GLib. pread/pwrite return 0 on success, -1 on failure.
 * getlength returns size or <0. pwrite may be NULL for read-only backends.
 */
typedef int (*vf_nvram_blk_pread_fn)(void *opaque, uint64_t offset,
                                     void *buf, size_t len);
typedef int (*vf_nvram_blk_pwrite_fn)(void *opaque, uint64_t offset,
                                      const void *buf, size_t len);
typedef int64_t (*vf_nvram_blk_getlength_fn)(void *opaque);

typedef struct vf_nvram_blk_backend {
    void *opaque;
    vf_nvram_blk_pread_fn pread;
    vf_nvram_blk_pwrite_fn pwrite;
    vf_nvram_blk_getlength_fn getlength;
} vf_nvram_blk_backend;

/*
 * Host research-image file state for BlockBackend-style attach (POSIX fd).
 * Unavailable under VF_EFI_BUILD (attach APIs fail closed).
 */
typedef struct vf_nvram_file_blk {
    int fd;
    int64_t length;
} vf_nvram_file_blk;

/*
 * Load on-disk bank via blk backend at offset 0, capped to VF_NVRAM_BANK_MAX_LEN.
 * Fail-closed if blk/ops missing, length <= 0, pread fails, or ingest fails.
 * Does not invent bank contents when blk is absent — use SYNTHETIC_FIXTURE
 * ingest via vf_nvram_ingest_bank instead.
 */
int vf_nvram_load_from_blk(vf_nvram_v1 *, const vf_nvram_blk_backend *blk);
/*
 * Serialize env bank and pwrite @0 (qemu-t8030 apple_nvram_save behavior).
 * Fail-closed if blk/pwrite missing or write fails. Not macOS UART proof.
 */
int vf_nvram_save_to_blk(const vf_nvram_v1 *, const vf_nvram_blk_backend *blk);
/*
 * Open a research NVRAM bank image path into out/state (O_RDWR). Fail-closed
 * if path missing/empty, open fails, or length <= 0. Does not invent contents.
 * Detach closes the fd. Host/test only — EFI builds return -1.
 */
int vf_nvram_blk_attach_path(vf_nvram_blk_backend *out, vf_nvram_file_blk *state,
                             const char *path);
int vf_nvram_blk_detach(vf_nvram_blk_backend *blk, vf_nvram_file_blk *state);
/*
 * Honest boot-args handoff surface: copy "boot-args" into out (NUL-terminated).
 * Returns byte length excluding NUL, 0 if unset/empty, or -1 on error/overflow.
 * Does not claim macOS/iBoot consumed the string.
 */
int vf_nvram_boot_args_handoff(const vf_nvram_v1 *, char *out, size_t out_len);
/* Serialize env bank into caller buffer; returns bytes written or -1. */
int vf_nvram_serialize(const vf_nvram_v1 *, void *buf, size_t buf_len);

uint8_t vf_nvram_chrp_checksum(const vf_chrp_part_hdr *hdr);
uint32_t vf_nvram_adler32(uint32_t adler, const uint8_t *buf, size_t len);

#endif
