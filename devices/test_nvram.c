#define _POSIX_C_SOURCE 200809L
#include "nvram_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * SYNTHETIC_FIXTURE: banks built here are first-party serialize() output for
 * unit tests only. They are not Apple NVRAM dumps and must not be treated as
 * firmware evidence.
 */
static void build_minimal_bank(uint8_t *buf, size_t len, const char *boot_args) {
    vf_nvram_v1 n;
    const char *got;

    assert(!vf_nvram_init(&n));
    assert(!vf_nvram_env_set(&n, "boot-args", boot_args, 0));
    assert(vf_nvram_serialize(&n, buf, len) == (int)len);
    assert(!vf_nvram_parse_bank(&n, buf, len));
    got = vf_nvram_env_get(&n, "boot-args");
    assert(got && !strcmp(got, boot_args));
}

typedef struct {
    const uint8_t *data;
    size_t len;
    int fail_pread;
} synth_mem_blk;

static int64_t synth_mem_getlength(void *opaque) {
    const synth_mem_blk *m = (const synth_mem_blk *)opaque;

    if (!m) {
        return -1;
    }
    return (int64_t)m->len;
}

static int synth_mem_pread(void *opaque, uint64_t offset, void *buf, size_t len) {
    const synth_mem_blk *m = (const synth_mem_blk *)opaque;

    if (!m || !buf || m->fail_pread) {
        return -1;
    }
    if (offset > m->len || len > m->len - (size_t)offset) {
        return -1;
    }
    memcpy(buf, m->data + (size_t)offset, len);
    return 0;
}

static int write_research_image(const char *path, const uint8_t *bank, size_t len) {
    FILE *fp = fopen(path, "wb");

    if (!fp) {
        return -1;
    }
    if (fwrite(bank, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

int main(void) {
    vf_nvram_v1 n;
    const vf_nvram_ns_info *meta = 0;
    uint8_t bank[0x2000];
    const char *val;
    char handoff[VF_NVRAM_ENV_VALUE_MAX];
    int handoff_len;
    synth_mem_blk mem;
    vf_nvram_blk_backend blk;
    vf_nvram_file_blk file;
    char path[128];
    int path_n;

    vf_nvram_get_namespace(&meta);
    assert(meta);
    assert(meta->nsid == VF_NVRAM_NSID);
    assert(meta->nstype == VF_NVRAM_NSTYPE);
    assert(meta->qom_type && !strcmp(meta->qom_type, "apple-nvram"));

    assert(!vf_nvram_init(&n));
    assert(!vf_nvram_env_set(&n, "boot-args", "-v keepsyms=1", 0));
    assert(!vf_nvram_env_set(&n, "auto-boot", "true", 0));
    assert(vf_nvram_env_get_bool(&n, "auto-boot", 0));
    assert(vf_nvram_env_get_bool(&n, "missing-flag", 1));
    assert(vf_nvram_env_get_uint(&n, "missing-num", 42u) == 42u);

    val = vf_nvram_env_get(&n, "boot-args");
    assert(val && !strcmp(val, "-v keepsyms=1"));

    assert(vf_nvram_env_unset(&n, "auto-boot") == 1);
    assert(!vf_nvram_env_get(&n, "auto-boot"));

    memset(bank, 0, sizeof(bank));
    assert(vf_nvram_serialize(&n, bank, sizeof(bank)) == (int)sizeof(bank));

    assert(!vf_nvram_parse_bank(&n, bank, sizeof(bank)));
    val = vf_nvram_env_get(&n, "boot-args");
    assert(val && !strcmp(val, "-v keepsyms=1"));

    build_minimal_bank(bank, sizeof(bank), "debug=0x144");

    /* fail closed on truncated bank / bad adler */
    assert(vf_nvram_parse_bank(&n, bank, 0x10u) == -1);
    bank[0x14] ^= 0x01u;
    assert(vf_nvram_parse_bank(&n, bank, sizeof(bank)) == -1);

    /* bounds: oversize name rejected */
    assert(!vf_nvram_init(&n));
    {
        char long_name[VF_NVRAM_ENV_NAME_MAX + 4];
        memset(long_name, 'a', sizeof(long_name) - 1u);
        long_name[sizeof(long_name) - 1u] = '\0';
        assert(vf_nvram_env_set(&n, long_name, "x", 0) == -1);
    }

    /* On-disk bank ingest + boot-args handoff (synthetic fixture bank). */
    build_minimal_bank(bank, sizeof(bank), "serial=3 -v keepsyms=1");
    assert(vf_nvram_ingest_bank(&n, bank, 0) == -1);
    assert(vf_nvram_ingest_bank(&n, bank, VF_NVRAM_BANK_MAX_LEN + 1u) == -1);
    assert(!vf_nvram_ingest_bank(&n, bank, sizeof(bank)));
    handoff_len = vf_nvram_boot_args_handoff(&n, handoff, sizeof(handoff));
    assert(handoff_len == (int)strlen("serial=3 -v keepsyms=1"));
    assert(!strcmp(handoff, "serial=3 -v keepsyms=1"));
    assert(vf_nvram_boot_args_handoff(&n, handoff, 4u) == -1);

    /* NVMe-ns blk backend path: fail-closed without ops; synthetic mem blk OK. */
    assert(vf_nvram_load_from_blk(&n, 0) == -1);
    memset(&blk, 0, sizeof(blk));
    assert(vf_nvram_load_from_blk(&n, &blk) == -1);
    assert(vf_nvram_save_to_blk(&n, &blk) == -1);

    build_minimal_bank(bank, sizeof(bank), "blk=1 serial=3");
    mem.data = bank;
    mem.len = sizeof(bank);
    mem.fail_pread = 0;
    blk.opaque = &mem;
    blk.pread = synth_mem_pread;
    blk.pwrite = 0;
    blk.getlength = synth_mem_getlength;
    assert(!vf_nvram_init(&n));
    assert(!vf_nvram_load_from_blk(&n, &blk));
    handoff_len = vf_nvram_boot_args_handoff(&n, handoff, sizeof(handoff));
    assert(handoff_len == (int)strlen("blk=1 serial=3"));
    assert(!strcmp(handoff, "blk=1 serial=3"));
    assert(vf_nvram_save_to_blk(&n, &blk) == -1);

    mem.fail_pread = 1;
    assert(vf_nvram_load_from_blk(&n, &blk) == -1);
    mem.len = 0;
    mem.fail_pread = 0;
    assert(vf_nvram_load_from_blk(&n, &blk) == -1);

    /* Research-image BlockBackend-style attach: fail-closed if missing. */
    assert(vf_nvram_blk_attach_path(&blk, &file, 0) == -1);
    assert(vf_nvram_blk_attach_path(&blk, &file, "") == -1);
    assert(vf_nvram_blk_attach_path(&blk, &file,
                                    "/tmp/nextcore-nvram-research-MISSING.bin")
           == -1);

    path_n = snprintf(path, sizeof(path), "/tmp/nextcore-nvram-research-%d",
                      (int)getpid());
    assert(path_n > 0 && (size_t)path_n < sizeof(path));
    unlink(path);
    build_minimal_bank(bank, sizeof(bank), "attach=1 serial=3");
    assert(!write_research_image(path, bank, sizeof(bank)));
    assert(!vf_nvram_blk_attach_path(&blk, &file, path));
    assert(!vf_nvram_init(&n));
    assert(!vf_nvram_load_from_blk(&n, &blk));
    handoff_len = vf_nvram_boot_args_handoff(&n, handoff, sizeof(handoff));
    assert(handoff_len == (int)strlen("attach=1 serial=3"));
    assert(!strcmp(handoff, "attach=1 serial=3"));

    /* Honest read/write path: mutate, save, detach, re-attach, reload. */
    assert(!vf_nvram_env_set(&n, "boot-args", "attach=2 saved=1", 0));
    assert(!vf_nvram_save_to_blk(&n, &blk));
    assert(!vf_nvram_blk_detach(&blk, &file));
    assert(!vf_nvram_blk_attach_path(&blk, &file, path));
    assert(!vf_nvram_init(&n));
    assert(!vf_nvram_load_from_blk(&n, &blk));
    handoff_len = vf_nvram_boot_args_handoff(&n, handoff, sizeof(handoff));
    assert(handoff_len == (int)strlen("attach=2 saved=1"));
    assert(!strcmp(handoff, "attach=2 saved=1"));
    assert(!vf_nvram_blk_detach(&blk, &file));
    unlink(path);

    puts("PASS NVRAM v1 namespace-5 env bank: nsid/nstype, boot-args, parse, ingest, blk, attach, save, handoff, serialize, bounds");
    return 0;
}
