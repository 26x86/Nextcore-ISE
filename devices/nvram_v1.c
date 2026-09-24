/* 26x86 first-party Apple NVRAM v1 env-bank stub. See NVRAM.md for layout evidence.
 * Behaviour adapted from qemu-t8030 hw/nvram/apple_nvram.c (GPL reference only).
 */
#include "nvram_v1.h"

#ifdef VF_EFI_BUILD
static size_t nvram_strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}
static int nvram_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void *nvram_memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
static void *nvram_memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
static int nvram_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q) {
            return (int)*p - (int)*q;
        }
        ++p;
        ++q;
    }
    return 0;
}
static void *nvram_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}
#define strlen nvram_strlen
#define strcmp nvram_strcmp
#define memcpy nvram_memcpy
#define memset nvram_memset
#define memmove nvram_memmove
#define memcmp nvram_memcmp
#else
#include <string.h>
#endif

static const vf_nvram_ns_info g_nvram_ns = {
    VF_NVRAM_NSID,
    VF_NVRAM_NSTYPE,
    "apple-nvram",
};

static void nvram_copy_str(char *dst, size_t dst_len, const char *src) {
    size_t n;

    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= dst_len) {
        n = dst_len - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static unsigned be16_to_host(uint16_t v) {
    return (unsigned)((v >> 8) | ((v & 0xffu) << 8));
}

static uint16_t host_to_be16(unsigned v) {
    return (uint16_t)(((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
}

uint8_t vf_nvram_chrp_checksum(const vf_chrp_part_hdr *hdr) {
    unsigned i, sum;
    const uint8_t *p = (const uint8_t *)hdr;

    sum = p[0];
    for (i = 0; i < 14u; i++) {
        sum += p[2u + i];
        sum = (sum + ((sum & 0xff00u) >> 8)) & 0xffu;
    }
    return (uint8_t)(sum & 0xffu);
}

uint32_t vf_nvram_adler32(uint32_t adler, const uint8_t *buf, size_t len) {
    const uint32_t mod = 65521u;
    uint32_t s1 = adler & 0xffffu;
    uint32_t s2 = (adler >> 16) & 0xffffu;
    size_t i;

    for (i = 0; i < len; i++) {
        s1 = (s1 + buf[i]) % mod;
        s2 = (s2 + s1) % mod;
    }
    return (s2 << 16) | s1;
}

void vf_nvram_get_namespace(const vf_nvram_ns_info **meta) {
    if (meta) {
        *meta = &g_nvram_ns;
    }
}

static vf_nvram_env *nvram_find_env(vf_nvram_v1 *n, const char *name) {
    unsigned i;

    if (!n || !name) {
        return 0;
    }
    for (i = 0; i < n->env_count; i++) {
        if (!strcmp(n->env[i].name, name)) {
            return &n->env[i];
        }
    }
    return 0;
}

static vf_nvram_part *nvram_find_part(vf_nvram_v1 *n, const char *name) {
    unsigned i;

    if (!n || !name) {
        return 0;
    }
    for (i = 0; i < n->part_count; i++) {
        if (!strcmp(n->parts[i].name, name)) {
            return &n->parts[i];
        }
    }
    return 0;
}

static vf_nvram_part *nvram_ensure_common(vf_nvram_v1 *n) {
    vf_nvram_part *part = nvram_find_part(n, "common");

    if (part) {
        return part;
    }
    if (!n || n->part_count >= VF_NVRAM_MAX_PARTS) {
        return 0;
    }
    part = &n->parts[n->part_count++];
    memset(part, 0, sizeof(*part));
    part->sig = VF_NVRAM_COMMON_SIG;
    nvram_copy_str(part->name, sizeof(part->name), "common");
    part->len = VF_NVRAM_COMMON_PART_LEN;
    return part;
}

static unsigned parse_env_uint(const char *value) {
    unsigned base = 10;
    unsigned acc = 0;
    int digits = 0;

    if (!value || !*value) {
        return 0;
    }
    if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        base = 16;
        value += 2;
    }
    while (*value) {
        unsigned digit;
        if (*value >= '0' && *value <= '9') {
            digit = (unsigned)(*value - '0');
        } else if (base == 16 && *value >= 'a' && *value <= 'f') {
            digit = (unsigned)(*value - 'a' + 10);
        } else if (base == 16 && *value >= 'A' && *value <= 'F') {
            digit = (unsigned)(*value - 'A' + 10);
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        acc = acc * base + digit;
        ++digits;
        ++value;
    }
    return digits ? acc : 0;
}

static int nvram_load_env_from_common(vf_nvram_v1 *n, const vf_nvram_part *part) {
    size_t cnt = 0;

    if (!n || !part) {
        return -1;
    }
    n->env_count = 0;

    while (cnt < part->len) {
        const char *name;
        const char *value;
        size_t name_len;
        size_t value_len;

        if (part->data[cnt] == '\0') {
            break;
        }

        name = (const char *)part->data + cnt;
        name_len = 0;
        while ((cnt + name_len) < part->len && name[name_len] != '\0' &&
               name[name_len] != '=') {
            name_len++;
        }
        if ((cnt + name_len) >= part->len || name[name_len] != '=') {
            return -1;
        }
        cnt += name_len + 1u;

        value = (const char *)part->data + cnt;
        value_len = 0;
        while ((cnt + value_len) < part->len && value[value_len] != '\0') {
            value_len++;
        }
        if ((cnt + value_len) >= part->len) {
            return -1;
        }

        {
            char name_buf[VF_NVRAM_ENV_NAME_MAX];
            char value_buf[VF_NVRAM_ENV_VALUE_MAX];

            if (name_len >= sizeof(name_buf) || value_len >= sizeof(value_buf)) {
                return -1;
            }
            memcpy(name_buf, name, name_len);
            name_buf[name_len] = '\0';
            memcpy(value_buf, value, value_len);
            value_buf[value_len] = '\0';
            if (vf_nvram_env_set(n, name_buf, value_buf, 0) != 0) {
                return -1;
            }
        }
        cnt += value_len + 1u;
    }
    return 0;
}

static int nvram_parse_partitions(vf_nvram_v1 *n, const uint8_t *buf, size_t len) {
    size_t offset = 0x20u;

    if (!n || !buf || len < 0x20u) {
        return -1;
    }
    n->part_count = 0;

    while (offset + sizeof(vf_chrp_part_hdr) <= len) {
        const vf_chrp_part_hdr *hdr = (const vf_chrp_part_hdr *)(buf + offset);
        unsigned part_units;
        size_t part_bytes;
        vf_nvram_part *part;

        if (hdr->checksum != vf_nvram_chrp_checksum(hdr)) {
            return -1;
        }
        if (hdr->signature == VF_NVRAM_CHRP_PART_FREE) {
            break;
        }

        part_units = be16_to_host(hdr->len);
        if (part_units < 1u || ((size_t)part_units * 0x10u + offset) > len) {
            return -1;
        }
        part_bytes = (size_t)part_units * 0x10u - 0x10u;
        if (n->part_count >= VF_NVRAM_MAX_PARTS) {
            return -1;
        }

        if (!memcmp(hdr->name, VF_NVRAM_PANIC_PART_NAME, 12u)) {
            offset += (size_t)part_units * 0x10u;
            continue;
        }

        part = &n->parts[n->part_count++];
        memset(part, 0, sizeof(*part));
        part->sig = hdr->signature;
        part->len = part_bytes;
        if (part->len > sizeof(part->data)) {
            return -1;
        }
        memcpy(part->name, hdr->name, sizeof(part->name) - 1u);
        if (offset + 0x10u + part->len > len) {
            return -1;
        }
        memcpy(part->data, buf + offset + 0x10u, part->len);
        offset += (size_t)part_units * 0x10u;
    }
    return 0;
}

int vf_nvram_init(vf_nvram_v1 *n) {
    vf_nvram_part *common;

    if (!n) {
        return -1;
    }
    memset(n, 0, sizeof(*n));
    n->bank_len = VF_NVRAM_BANK_MAX_LEN;
    common = nvram_ensure_common(n);
    if (!common) {
        return -1;
    }
    return 0;
}

const char *vf_nvram_env_get(const vf_nvram_v1 *n, const char *name) {
    vf_nvram_env *v;

    if (!n || !name) {
        return 0;
    }
    v = nvram_find_env((vf_nvram_v1 *)n, name);
    return v ? v->value : 0;
}

unsigned vf_nvram_env_get_uint(const vf_nvram_v1 *n, const char *name,
                               unsigned default_val) {
    vf_nvram_env *v;

    if (!n || !name) {
        return default_val;
    }
    v = nvram_find_env((vf_nvram_v1 *)n, name);
    return v ? v->parsed_uint : default_val;
}

int vf_nvram_env_get_bool(const vf_nvram_v1 *n, const char *name, int default_val) {
    vf_nvram_env *v;

    if (!n || !name) {
        return default_val;
    }
    v = nvram_find_env((vf_nvram_v1 *)n, name);
    if (!v) {
        return default_val;
    }
    if (!strcmp(v->value, "true")) {
        return 1;
    }
    if (v->parsed_uint) {
        return 1;
    }
    return 0;
}

int vf_nvram_env_unset(vf_nvram_v1 *n, const char *name) {
    unsigned i;
    vf_nvram_env *v;

    if (!n || !name) {
        return -1;
    }
    v = nvram_find_env(n, name);
    if (!v) {
        return 0;
    }
    for (i = 0; i < n->env_count; i++) {
        if (&n->env[i] == v) {
            memmove(&n->env[i], &n->env[i + 1u],
                    (size_t)(n->env_count - i - 1u) * sizeof(n->env[0]));
            n->env_count--;
            return 1;
        }
    }
    return 0;
}

int vf_nvram_env_set(vf_nvram_v1 *n, const char *name, const char *value,
                     uint32_t flags) {
    vf_nvram_env *v;

    if (!n || !name || !value) {
        return -1;
    }
    if (name[0] == '\0' || strlen(name) >= VF_NVRAM_ENV_NAME_MAX ||
        strlen(value) >= VF_NVRAM_ENV_VALUE_MAX) {
        return -1;
    }

    vf_nvram_env_unset(n, name);
    if (n->env_count >= VF_NVRAM_MAX_ENV) {
        return -1;
    }

    v = &n->env[n->env_count++];
    memset(v, 0, sizeof(*v));
    nvram_copy_str(v->name, sizeof(v->name), name);
    nvram_copy_str(v->value, sizeof(v->value), value);
    v->flags = flags;
    v->parsed_uint = parse_env_uint(value);
    return 0;
}

int vf_nvram_parse_bank(vf_nvram_v1 *n, const void *buf, size_t len) {
    const vf_apple_nvram_hdr *apple;
    const uint8_t *bytes;
    uint32_t adler;
    const vf_nvram_part *common;

    if (!n || !buf || len < sizeof(vf_apple_nvram_hdr)) {
        return -1;
    }
    bytes = (const uint8_t *)buf;
    apple = (const vf_apple_nvram_hdr *)bytes;

    if (apple->chrp.checksum != vf_nvram_chrp_checksum(&apple->chrp)) {
        return -1;
    }
    adler = vf_nvram_adler32(1u, bytes + 0x14u, len - 0x14u);
    if (adler != apple->adler) {
        return -1;
    }

    memset(n, 0, sizeof(*n));
    n->bank_len = len;
    n->generation = apple->generation;

    if (nvram_parse_partitions(n, bytes, len) != 0) {
        memset(n, 0, sizeof(*n));
        return -1;
    }

    common = nvram_find_part(n, "common");
    if (!common) {
        if (vf_nvram_init(n) != 0) {
            return -1;
        }
        return 0;
    }
    if (nvram_load_env_from_common(n, common) != 0) {
        memset(n, 0, sizeof(*n));
        return -1;
    }
    return 0;
}

int vf_nvram_ingest_bank(vf_nvram_v1 *n, const void *buf, size_t len) {
    if (!n || !buf) {
        return -1;
    }
    if (len == 0 || len > VF_NVRAM_BANK_MAX_LEN) {
        return -1;
    }
    return vf_nvram_parse_bank(n, buf, len);
}

int vf_nvram_load_from_blk(vf_nvram_v1 *n, const vf_nvram_blk_backend *blk) {
    uint8_t buffer[VF_NVRAM_BANK_MAX_LEN];
    int64_t raw_len;
    size_t len;

    if (!n || !blk || !blk->pread || !blk->getlength) {
        return -1;
    }
    raw_len = blk->getlength(blk->opaque);
    if (raw_len <= 0) {
        return -1;
    }
    len = (size_t)raw_len;
    if (len > VF_NVRAM_BANK_MAX_LEN) {
        len = VF_NVRAM_BANK_MAX_LEN;
    }
    memset(buffer, 0, sizeof(buffer));
    if (blk->pread(blk->opaque, 0, buffer, len) != 0) {
        return -1;
    }
    return vf_nvram_ingest_bank(n, buffer, len);
}

int vf_nvram_save_to_blk(const vf_nvram_v1 *n, const vf_nvram_blk_backend *blk) {
    uint8_t buffer[VF_NVRAM_BANK_MAX_LEN];
    int written;

    if (!n || !blk || !blk->pwrite) {
        return -1;
    }
    written = vf_nvram_serialize(n, buffer, sizeof(buffer));
    if (written <= 0) {
        return -1;
    }
    if (blk->pwrite(blk->opaque, 0, buffer, (size_t)written) != 0) {
        return -1;
    }
    return 0;
}

#ifndef VF_EFI_BUILD
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int64_t nvram_file_getlength(void *opaque) {
    const vf_nvram_file_blk *s = (const vf_nvram_file_blk *)opaque;

    if (!s || s->fd < 0) {
        return -1;
    }
    return s->length;
}

static int nvram_file_pread(void *opaque, uint64_t offset, void *buf, size_t len) {
    vf_nvram_file_blk *s = (vf_nvram_file_blk *)opaque;
    ssize_t n;
    size_t got = 0;

    if (!s || s->fd < 0 || !buf || len == 0) {
        return -1;
    }
    if (offset > (uint64_t)s->length ||
        len > (size_t)((uint64_t)s->length - offset)) {
        return -1;
    }
    if (lseek(s->fd, (off_t)offset, SEEK_SET) != (off_t)offset) {
        return -1;
    }
    while (got < len) {
        n = read(s->fd, (uint8_t *)buf + got, len - got);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

static int nvram_file_pwrite(void *opaque, uint64_t offset, const void *buf,
                             size_t len) {
    vf_nvram_file_blk *s = (vf_nvram_file_blk *)opaque;
    ssize_t n;
    size_t put = 0;
    struct stat st;

    if (!s || s->fd < 0 || !buf || len == 0) {
        return -1;
    }
    if (lseek(s->fd, (off_t)offset, SEEK_SET) != (off_t)offset) {
        return -1;
    }
    while (put < len) {
        n = write(s->fd, (const uint8_t *)buf + put, len - put);
        if (n <= 0) {
            return -1;
        }
        put += (size_t)n;
    }
    if (fsync(s->fd) != 0) {
        return -1;
    }
    if (fstat(s->fd, &st) != 0 || st.st_size <= 0) {
        return -1;
    }
    s->length = (int64_t)st.st_size;
    (void)errno;
    return 0;
}

int vf_nvram_blk_attach_path(vf_nvram_blk_backend *out, vf_nvram_file_blk *state,
                             const char *path) {
    struct stat st;
    int fd;

    if (!out || !state || !path || path[0] == '\0') {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memset(state, 0, sizeof(*state));
    state->fd = -1;
    state->length = -1;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return -1;
    }

    state->fd = fd;
    state->length = (int64_t)st.st_size;
    out->opaque = state;
    out->pread = nvram_file_pread;
    out->pwrite = nvram_file_pwrite;
    out->getlength = nvram_file_getlength;
    return 0;
}

int vf_nvram_blk_detach(vf_nvram_blk_backend *blk, vf_nvram_file_blk *state) {
    if (state && state->fd >= 0) {
        close(state->fd);
        state->fd = -1;
        state->length = -1;
    }
    if (blk) {
        memset(blk, 0, sizeof(*blk));
    }
    return 0;
}
#else
int vf_nvram_blk_attach_path(vf_nvram_blk_backend *out, vf_nvram_file_blk *state,
                             const char *path) {
    (void)out;
    (void)state;
    (void)path;
    return -1;
}

int vf_nvram_blk_detach(vf_nvram_blk_backend *blk, vf_nvram_file_blk *state) {
    (void)blk;
    (void)state;
    return -1;
}
#endif

int vf_nvram_boot_args_handoff(const vf_nvram_v1 *n, char *out, size_t out_len) {
    const char *boot_args;
    size_t nlen;

    if (!n || !out || out_len == 0) {
        return -1;
    }
    boot_args = vf_nvram_env_get(n, "boot-args");
    if (!boot_args || boot_args[0] == '\0') {
        out[0] = '\0';
        return 0;
    }
    nlen = strlen(boot_args);
    if (nlen + 1u > out_len) {
        return -1;
    }
    memcpy(out, boot_args, nlen + 1u);
    return (int)nlen;
}

static int nvram_format_env_entry(char *dst, size_t dst_len, const char *name, const char *value) {
    size_t name_len;
    size_t value_len;
    size_t needed;

    if (!dst || !name || !value) {
        return -1;
    }
    name_len = strlen(name);
    value_len = strlen(value);
    needed = name_len + 1u + value_len;
    if (needed >= dst_len) {
        return -1;
    }
    memcpy(dst, name, name_len);
    dst[name_len] = '=';
    memcpy(dst + name_len + 1u, value, value_len);
    dst[needed] = '\0';
    return (int)needed;
}

static int nvram_env_serialize(const vf_nvram_v1 *n, uint8_t *buffer, size_t len) {
    size_t pos = 0;
    unsigned i;

    if (!n || !buffer) {
        return -1;
    }
    for (i = 0; i < n->env_count; i++) {
        const vf_nvram_env *v = &n->env[i];
        int needed = nvram_format_env_entry((char *)buffer + pos, len - pos, v->name, v->value);

        if (needed < 0 || (size_t)needed >= len - pos) {
            return -1;
        }
        pos += (size_t)needed + 1u;
    }
    if (pos < len) {
        buffer[pos] = '\0';
    }
    return (int)pos;
}

static int nvram_prepare_bank(const vf_nvram_v1 *n, uint8_t *buf, size_t bank_len) {
    size_t offset = 0x20u;
    vf_apple_nvram_hdr *apple;
    unsigned i;

    if (!n || !buf || bank_len < 0x40u) {
        return -1;
    }
    memset(buf, 0, bank_len);

    apple = (vf_apple_nvram_hdr *)buf;
    apple->chrp.signature = VF_NVRAM_CHRP_SIG;
    apple->chrp.len = host_to_be16(0x2u);
    memcpy(apple->chrp.name, "nvram", 6u);
    apple->chrp.checksum = vf_nvram_chrp_checksum(&apple->chrp);
    apple->generation = n->generation;

    for (i = 0; i < n->part_count; i++) {
        const vf_nvram_part *part = &n->parts[i];
        vf_chrp_part_hdr *hdr;
        unsigned units;
        size_t total;

        if (part->len == 0) {
            continue;
        }
        if (offset + sizeof(vf_chrp_part_hdr) > bank_len) {
            return -1;
        }

        units = (unsigned)((part->len + 0x10u + 0xfu) / 0x10u);
        total = (size_t)units * 0x10u;
        if (offset + total > bank_len) {
            return -1;
        }

        hdr = (vf_chrp_part_hdr *)(buf + offset);
        hdr->signature = part->sig;
        hdr->len = host_to_be16(units);
        memcpy(hdr->name, part->name, sizeof(hdr->name));
        hdr->checksum = vf_nvram_chrp_checksum(hdr);
        if (part->len > 0) {
            memcpy(buf + offset + 0x10u, part->data, part->len);
        }
        offset += total;
    }

    if (offset + sizeof(vf_chrp_part_hdr) <= bank_len) {
        vf_chrp_part_hdr *free_hdr = (vf_chrp_part_hdr *)(buf + offset);
        unsigned free_units = (unsigned)((bank_len - offset) / 0x10u);

        free_hdr->signature = VF_NVRAM_CHRP_PART_FREE;
        free_hdr->len = host_to_be16(free_units);
        free_hdr->checksum = vf_nvram_chrp_checksum(free_hdr);
    }

    apple->adler = vf_nvram_adler32(1u, buf + 0x14u, bank_len - 0x14u);
    return 0;
}

int vf_nvram_serialize(const vf_nvram_v1 *n, void *buf, size_t buf_len) {
    vf_nvram_v1 scratch;
    vf_nvram_part *common;

    if (!n || !buf || buf_len == 0) {
        return -1;
    }

    scratch = *n;
    common = nvram_ensure_common(&scratch);
    if (!common) {
        return -1;
    }
    if (scratch.bank_len == 0 || scratch.bank_len > buf_len) {
        scratch.bank_len = buf_len;
    }
    if (nvram_env_serialize(&scratch, common->data, common->len) < 0) {
        return -1;
    }
    if (nvram_prepare_bank(&scratch, (uint8_t *)buf, scratch.bank_len) != 0) {
        return -1;
    }
    return (int)scratch.bank_len;
}
