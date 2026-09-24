#include "ans_pci_v1.h"
#include "aic_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test-visible host buffer MSI sink (DMA callback opaque). */
typedef struct {
    uint8_t buf[4];
    uint64_t last_addr;
    uint16_t last_data;
    int writes;
    int fail_next;
} msi_sink_t;

static int msi_sink_write(void *opaque, uint64_t addr, uint16_t data) {
    msi_sink_t *sink = (msi_sink_t *)opaque;
    if (!sink) return -1;
    if (sink->fail_next) {
        sink->fail_next = 0;
        return -1;
    }
    sink->buf[0] = (uint8_t)(data & 0xffu);
    sink->buf[1] = (uint8_t)((data >> 8) & 0xffu);
    sink->last_addr = addr;
    sink->last_data = data;
    sink->writes++;
    return 0;
}

/* Pattern-matched STORAGE→AIC line sync via vf_aic_set_line. */
static int aic_storage_sink(void *opaque, unsigned irq, int high) {
    vf_aic_v1 *aic = (vf_aic_v1 *)opaque;
    if (!aic) return -1;
    if (irq != VF_ANS_PCI_AIC_STORAGE_IRQ_LINE) return -1;
    return vf_aic_set_line(aic, irq, high);
}

int main(void) {
    vf_ans_pci_v1 s;
    uint64_t v = 0;
    msi_sink_t sink;
    uint32_t expect_flags = VF_ANS_PCI_FLAG_PRESENT |
                            VF_ANS_PCI_FLAG_MMIO_CONTAINER |
                            VF_ANS_PCI_FLAG_IOPORT_CONTAINER |
                            VF_ANS_PCI_FLAG_MSI_CAPABLE |
                            VF_ANS_PCI_FLAG_MSIX_CAPABLE;
    uint16_t enable = (uint16_t)(VF_ANS_PCI_COMMAND_MEMORY |
                                 VF_ANS_PCI_COMMAND_MASTER);

    assert(!vf_ans_pci_init(&s));
    assert(vf_ans_pci_status(&s) == VF_ANS_PCI_STATUS_BOOTSTRAP);
    assert(vf_ans_pci_flags(&s) == expect_flags);
    assert(vf_ans_pci_command(&s) == 0);
    assert(!vf_ans_pci_bus_master_enabled(&s));
    assert(!vf_ans_pci_config_write_seen(&s));
    assert(vf_ans_pci_last_config_write(&s) == 0);
    assert(!vf_ans_pci_msi_enabled(&s));
    assert(!vf_ans_pci_msi_pending(&s));
    assert(!vf_ans_pci_msix_enabled(&s));
    assert(!vf_ans_pci_msix_pending(&s));
    assert(vf_ans_pci_msix_table_bir(&s) == 0);
    assert(!vf_ans_pci_delivery_pending(&s));
    assert(vf_ans_pci_msi_addr(&s) == 0);
    assert(vf_ans_pci_msi_data(&s) == 0);
    assert(!vf_ans_pci_msi_message_written(&s));
    assert(!vf_ans_pci_aic_sync_seen(&s));
    assert(!vf_ans_pci_aic_line_high(&s));
    assert(VF_ANS_PCI_AIC_STORAGE_IRQ_LINE == 1u);

    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_STATUS, 32, &v) &&
           v == VF_ANS_PCI_STATUS_BOOTSTRAP);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_FLAGS, 32, &v) &&
           v == expect_flags);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_TIER, 32, &v) &&
           v == VF_ANS_PCI_TIER_HOST);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_IOPORT_SIZE, 32, &v) &&
           v == VF_ANS_PCI_IOPORT_SIZE);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MMCFG_SIZE, 32, &v) &&
           v == VF_ANS_PCI_MMCFG_SIZE);

    /* Config probe: COMMAND Memory|BusMaster store/read; other bits drop. */
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 64, &v) &&
           v == 0);
    /* IO Space (bit0) and high garbage must not latch. */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_CONFIG, 64,
                             0xdeadbeef00000001ull));
    assert(vf_ans_pci_config_write_seen(&s) == 1);
    assert(vf_ans_pci_last_config_write(&s) == 0xdeadbeef00000001ull);
    assert(vf_ans_pci_command(&s) == 0);
    assert(!vf_ans_pci_bus_master_enabled(&s));
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 64, &v) &&
           v == 0);

    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_CONFIG, 64,
                             VF_ANS_PCI_COMMAND_MEMORY));
    assert(vf_ans_pci_command(&s) == VF_ANS_PCI_COMMAND_MEMORY);
    assert(!vf_ans_pci_bus_master_enabled(&s));
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 64, &v) &&
           v == VF_ANS_PCI_COMMAND_MEMORY);

    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_CONFIG, 64, enable));
    assert(vf_ans_pci_command(&s) == enable);
    assert(vf_ans_pci_bus_master_enabled(&s) == 1);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 64, &v) &&
           v == enable);

    /* Host OR helper (mailbox start path): idempotent Memory|BusMaster latch. */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_CONFIG, 64, 0));
    assert(!vf_ans_pci_bus_master_enabled(&s));
    assert(!vf_ans_pci_enable_memory_bus_master(&s));
    assert(vf_ans_pci_command(&s) == enable);
    assert(vf_ans_pci_bus_master_enabled(&s) == 1);
    assert(vf_ans_pci_enable_memory_bus_master(NULL) == -1);

    /* Pin/INTx pending latch when MSI disabled (apple_ans_set_irq honesty).
     * Unbound AIC: pending latches but no STORAGE line sync (fail-closed). */
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_irq_pending(&s) == 1);
    assert(!vf_ans_pci_msi_pending(&s));
    assert(!vf_ans_pci_msix_pending(&s));
    assert(vf_ans_pci_delivery_pending(&s) == 1);
    assert(!vf_ans_pci_aic_sync_seen(&s));
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_delivery_pending(&s));
    assert(vf_ans_pci_set_irq(NULL, 1) == -1);
    assert(vf_ans_pci_irq_pending(NULL) == 0);
    assert(vf_ans_pci_delivery_pending(NULL) == 0);

    /* MSI prepare + enable: address/data store; pending latch; INTx suppressed.
     * Unbound: pending latches but no message write (fail-closed). */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 64,
                             0xfee00000ull));
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_DATA, 32, 0x4041u));
    assert(vf_ans_pci_msi_addr(&s) == 0xfee00000ull);
    assert(vf_ans_pci_msi_data(&s) == 0x4041u);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 64, &v) &&
           v == 0xfee00000ull);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSI_DATA, 32, &v) &&
           v == 0x4041u);
    /* High data bits drop (lo16 only). */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_DATA, 32, 0xabcd4042u));
    assert(vf_ans_pci_msi_data(&s) == 0x4042u);
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_irq_pending(&s) == 1);
    assert(!vf_ans_pci_msi_pending(&s));
    /* Enable while INTx pending → route into MSI pending; clear INTx;
     * unbound → still no message write. */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32,
                             VF_ANS_PCI_MSI_ENABLE | 0x100u));
    assert(vf_ans_pci_msi_enabled(&s) == 1);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32, &v) &&
           v == VF_ANS_PCI_MSI_ENABLE);
    assert(vf_ans_pci_msi_pending(&s) == 1);
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_msi_message_written(&s));
    /* set_irq while MSI enabled updates MSI pending only (still unbound). */
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(!vf_ans_pci_msi_pending(&s));
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_msi_pending(&s) == 1);
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_msi_message_written(&s));
    assert(vf_ans_pci_msi_addr(&s) == 0xfee00000ull);
    assert(vf_ans_pci_msi_data(&s) == 0x4042u);

    /* GPA→host buffer translate: Message Address as GPA into bound window. */
    {
        uint8_t gpa_page[16];
        assert(vf_ans_pci_msi_gpa_bound(NULL) == -1);
        assert(vf_ans_pci_msi_gpa_bound(&s) == 0);
        assert(vf_ans_pci_bind_msi_gpa(NULL, gpa_page, sizeof(gpa_page),
                                      0xfee00000ull) == -1);
        /* Short window (< 2) fail-closed (does not bind). */
        assert(vf_ans_pci_bind_msi_gpa(&s, gpa_page, 1u, 0xfee00000ull)
               == -1);
        assert(vf_ans_pci_msi_gpa_bound(&s) == 0);
        memset(gpa_page, 0xaa, sizeof(gpa_page));
        assert(!vf_ans_pci_bind_msi_gpa(&s, gpa_page, sizeof(gpa_page),
                                        0xfee00000ull));
        assert(vf_ans_pci_msi_gpa_bound(&s) == 1);
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(vf_ans_pci_msi_pending(&s) == 1);
        assert(vf_ans_pci_msi_message_written(&s) == 1);
        assert(gpa_page[0] == 0x42u && gpa_page[1] == 0x40u);
        assert(gpa_page[2] == 0xaa); /* bytes beyond message untouched */
        assert(vf_ans_pci_last_msi_message_addr(&s) == 0xfee00000ull);
        assert(vf_ans_pci_last_msi_message_data(&s) == 0x4042u);
        /* Offset within window: addr = base+4. */
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 64,
                                 0xfee00004ull));
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(gpa_page[4] == 0x42u && gpa_page[5] == 0x40u);
        /* OOB: addr below base → fail-closed (pending ok, no write). */
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 64,
                                 0xfedffffeull));
        gpa_page[0] = 0x55;
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(vf_ans_pci_msi_pending(&s) == 1);
        assert(gpa_page[0] == 0x55); /* OOB did not clobber */
        /* OOB: past end of window (need 2 bytes). */
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 64,
                                 0xfee0000full));
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(gpa_page[15] == 0xaa);
        /* Restore in-window addr; GPA preferred over DMA callback. */
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 64,
                                 0xfee00000ull));
        memset(&sink, 0, sizeof(sink));
        assert(!vf_ans_pci_bind_msi_message(&s, msi_sink_write, &sink));
        memset(gpa_page, 0, sizeof(gpa_page));
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(gpa_page[0] == 0x42u && gpa_page[1] == 0x40u);
        assert(sink.writes == 0); /* GPA path skipped callback */
        /* Unbind GPA → fail-closed unless callback remains. */
        assert(!vf_ans_pci_bind_msi_gpa(&s, NULL, 0, 0));
        assert(vf_ans_pci_msi_gpa_bound(&s) == 0);
        assert(!vf_ans_pci_msi_message_written(&s));
        /* Callback still bound: falls back to DMA sink. */
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(vf_ans_pci_msi_message_written(&s) == 1);
        assert(sink.writes == 1);
        assert(sink.last_addr == 0xfee00000ull);
        assert(sink.last_data == 0x4042u);
        assert(sink.buf[0] == 0x42u && sink.buf[1] == 0x40u);
    }

    /* Bind host-buffer DMA callback alone: assert writes prepared addr/data. */
    memset(&sink, 0, sizeof(sink));
    assert(vf_ans_pci_bind_msi_message(NULL, msi_sink_write, &sink) == -1);
    assert(!vf_ans_pci_bind_msi_message(&s, msi_sink_write, &sink));
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_msi_pending(&s) == 1);
    assert(vf_ans_pci_msi_message_written(&s) == 1);
    assert(sink.writes == 1);
    assert(sink.last_addr == 0xfee00000ull);
    assert(sink.last_data == 0x4042u);
    assert(sink.buf[0] == 0x42u && sink.buf[1] == 0x40u);
    assert(vf_ans_pci_last_msi_message_addr(&s) == 0xfee00000ull);
    assert(vf_ans_pci_last_msi_message_data(&s) == 0x4042u);
    /* Callback fail: pending still latches; write-seen stays from prior. */
    sink.fail_next = 1;
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_msi_pending(&s) == 1);
    assert(sink.writes == 1);
    assert(vf_ans_pci_msi_message_written(&s) == 1);
    /* Unbind → fail-closed again (pending ok, no new write). */
    assert(!vf_ans_pci_bind_msi_message(&s, NULL, NULL));
    assert(!vf_ans_pci_msi_message_written(&s));
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_msi_pending(&s) == 1);
    assert(sink.writes == 1);
    assert(!vf_ans_pci_msi_message_written(&s));
    /* Disable MSI → pending returns to INTx. */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32, 0));
    assert(!vf_ans_pci_msi_enabled(&s));
    assert(!vf_ans_pci_msi_pending(&s));
    assert(vf_ans_pci_irq_pending(&s) == 1);

    /* MSI-X Enable + Table BIR prepare: pending latch; INTx/MSI suppressed.
     * No PBA/table MMIO or message write at this stub. */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSIX_TABLE_BIR, 32,
                             0x12u)); /* BIR=2; high bits drop */
    assert(vf_ans_pci_msix_table_bir(&s) == 0x2u);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSIX_TABLE_BIR, 32, &v) &&
           v == 0x2u);
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSIX_CTRL, 32,
                             VF_ANS_PCI_MSIX_ENABLE | 0x100u));
    assert(vf_ans_pci_msix_enabled(&s) == 1);
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSIX_CTRL, 32, &v) &&
           v == VF_ANS_PCI_MSIX_ENABLE);
    assert(vf_ans_pci_msix_pending(&s) == 1);
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_msi_pending(&s));
    assert(vf_ans_pci_delivery_pending(&s) == 1);
    /* set_irq while MSI-X enabled updates MSI-X pending only. */
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(!vf_ans_pci_msix_pending(&s));
    assert(!vf_ans_pci_set_irq(&s, 1));
    assert(vf_ans_pci_msix_pending(&s) == 1);
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_msi_pending(&s));
    /* MSI Enable while MSI-X on is prepare-only (does not steal pending). */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32,
                             VF_ANS_PCI_MSI_ENABLE));
    assert(vf_ans_pci_msi_enabled(&s) == 1);
    assert(vf_ans_pci_msix_pending(&s) == 1);
    assert(!vf_ans_pci_msi_pending(&s));
    /* Disable MSI-X → pending returns to MSI (still enabled). */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSIX_CTRL, 32, 0));
    assert(!vf_ans_pci_msix_enabled(&s));
    assert(!vf_ans_pci_msix_pending(&s));
    assert(vf_ans_pci_msi_pending(&s) == 1);
    assert(!vf_ans_pci_irq_pending(&s));
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32, 0));
    assert(vf_ans_pci_irq_pending(&s) == 1);
    assert(!vf_ans_pci_set_irq(&s, 0));
    assert(vf_ans_pci_msix_enabled(NULL) == 0);
    assert(vf_ans_pci_msix_pending(NULL) == 0);
    assert(vf_ans_pci_msix_table_bir(NULL) == 0);
    assert(vf_ans_pci_msi_enabled(NULL) == 0);
    assert(vf_ans_pci_msi_pending(NULL) == 0);
    assert(vf_ans_pci_msi_addr(NULL) == 0);
    assert(vf_ans_pci_msi_data(NULL) == 0);
    assert(vf_ans_pci_msi_message_written(NULL) == 0);
    assert(vf_ans_pci_last_msi_message_addr(NULL) == 0);
    assert(vf_ans_pci_last_msi_message_data(NULL) == 0);
    assert(vf_ans_pci_msi_gpa_bound(NULL) == -1);

    /* AIC STORAGE line bind: INTx|MSI|MSI-X pending mirrors into aic_v1 line 1.
     * Pattern-matched to M1GuestBus STORAGE_SOURCE; not window 0x4 ownership. */
    {
        vf_aic_v1 aic;
        uint32_t hw = 0;
        assert(!vf_aic_init(&aic, 8, 1));
        /* Lines start masked; unmask STORAGE so vf_aic_pending can observe. */
        assert(!vf_aic_write(&aic, 0, 0x4180, 4,
                             1u << VF_ANS_PCI_AIC_STORAGE_IRQ_LINE));
        assert(!vf_aic_pending(&aic, 0));
        assert(vf_ans_pci_bind_aic(NULL, aic_storage_sink, &aic) == -1);
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(vf_ans_pci_irq_pending(&s) == 1);
        assert(!vf_ans_pci_aic_sync_seen(&s)); /* still unbound */
        assert(!vf_ans_pci_bind_aic(&s, aic_storage_sink, &aic));
        assert(vf_ans_pci_aic_sync_seen(&s) == 1);
        assert(vf_ans_pci_aic_line_high(&s) == 1);
        assert(vf_aic_pending(&aic, 0) == 1); /* STORAGE line 1 unmasked */
        assert(!vf_aic_read(&aic, 0, 0x4200, 4, &hw));
        assert((hw & (1u << VF_ANS_PCI_AIC_STORAGE_IRQ_LINE)) != 0u);
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_ans_pci_delivery_pending(&s));
        assert(!vf_ans_pci_aic_line_high(&s));
        assert(!vf_aic_pending(&aic, 0));
        /* MSI pending also mirrors the same STORAGE line. */
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32,
                                 VF_ANS_PCI_MSI_ENABLE));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(vf_ans_pci_msi_pending(&s) == 1);
        assert(!vf_ans_pci_irq_pending(&s));
        assert(vf_ans_pci_delivery_pending(&s) == 1);
        assert(vf_ans_pci_aic_line_high(&s) == 1);
        assert(vf_aic_pending(&aic, 0) == 1);
        /* MSI-X pending also mirrors the same STORAGE line. */
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSIX_CTRL, 32,
                                 VF_ANS_PCI_MSIX_ENABLE));
        assert(vf_ans_pci_msix_pending(&s) == 1);
        assert(!vf_ans_pci_msi_pending(&s));
        assert(vf_ans_pci_aic_line_high(&s) == 1);
        assert(vf_aic_pending(&aic, 0) == 1);
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSIX_CTRL, 32, 0));
        assert(vf_ans_pci_msi_pending(&s) == 1);
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(!vf_aic_pending(&aic, 0));
        assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 32, 0));
        /* Unbind: fail-closed; pending may latch without AIC sync. */
        assert(!vf_ans_pci_bind_aic(&s, NULL, NULL));
        assert(!vf_ans_pci_aic_sync_seen(&s));
        assert(!vf_ans_pci_aic_line_high(&s));
        assert(!vf_ans_pci_set_irq(&s, 1));
        assert(vf_ans_pci_delivery_pending(&s) == 1);
        assert(!vf_aic_pending(&aic, 0)); /* no sync after unbind */
        assert(!vf_ans_pci_set_irq(&s, 0));
        assert(vf_ans_pci_aic_sync_seen(NULL) == 0);
        assert(vf_ans_pci_aic_line_high(NULL) == 0);
    }

    /* Clear BusMaster; Memory may stay or clear with a full mask write. */
    assert(!vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_CONFIG, 64, 0));
    assert(vf_ans_pci_command(&s) == 0);
    assert(!vf_ans_pci_bus_master_enabled(&s));
    assert(!vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 64, &v) &&
           v == 0);

    /* Status never advances to ACTIVE (would imply real PCIe / DMA). */
    assert(vf_ans_pci_status(&s) == VF_ANS_PCI_STATUS_BOOTSTRAP);

    /* Fail-closed: wrong width, RO writes, OOB, NULL. */
    assert(vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 32, &v) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_CONFIG, 32, 1) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_STATUS, 32, 1) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_FLAGS, 32, 1) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_IOPORT_SIZE, 32, 1) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MMCFG_SIZE, 32, 1) == -1);
    assert(vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 64, &v) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSI_CTRL, 64, 1) == -1);
    assert(vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSI_ADDR, 32, &v) == -1);
    assert(vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_MSIX_CTRL, 64, &v) == -1);
    assert(vf_ans_pci_write(&s, VF_ANS_PCI_MMIO_MSIX_TABLE_BIR, 64, 1) == -1);
    assert(vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_SIZE, 32, &v) == -1);
    assert(vf_ans_pci_read(&s, 0x00Cu, 32, &v) == -1);
    assert(vf_ans_pci_init(NULL) == -1);
    assert(vf_ans_pci_status(NULL) == VF_ANS_PCI_STATUS_ABSENT);
    assert(vf_ans_pci_flags(NULL) == 0);
    assert(vf_ans_pci_command(NULL) == 0);
    assert(vf_ans_pci_bus_master_enabled(NULL) == 0);
    assert(vf_ans_pci_read(NULL, VF_ANS_PCI_MMIO_CONFIG, 64, &v) == -1);
    assert(vf_ans_pci_read(&s, VF_ANS_PCI_MMIO_CONFIG, 64, NULL) == -1);
    assert(vf_ans_pci_write(NULL, VF_ANS_PCI_MMIO_CONFIG, 64, 1) == -1);

    puts("PASS ANS pci v1: present/bootstrap, containers+MMCFG sizes, "
         "COMMAND Memory|BusMaster store/read + enable helper, "
         "INTx pending latch + MSI capability/pending + MSI-X Enable/BIR "
         "pending + GPA MSI translate + bound message write + bound AIC "
         "STORAGE line pending mirror, no ACTIVE/DT IRQ");
    return 0;
}
