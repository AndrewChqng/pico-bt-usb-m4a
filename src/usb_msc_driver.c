#include "tusb.h"
#include "config.h"
#include "fat32_virtual.h"
#include "stream_ringbuf.h"
#include "pico/stdlib.h"
#include <string.h>

static bool g_ejected = false;

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    const char vid[] = "PICO-WH ";
    const char pid[] = "BT-M4A-STREAMER ";
    const char rev[] = "1.0 ";

    memcpy(vendor_id, vid, 8);
    memcpy(product_id, pid, 16);
    memcpy(product_rev, rev, 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    if (g_ejected) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = DISK_TOTAL_SECTORS;
    *block_size  = DISK_SECTOR_BYTES;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;
    if (load_eject) {
        if (!start) {
            g_ejected = true;
        } else {
            g_ejected = false;
        }
    }
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return false;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
    (void)lun;
    if (lba >= DISK_TOTAL_SECTORS) {
        return -1;
    }

    /*
     * Real-time SCSI READ(10) pacing:
     * When the car stereo reads live audio sectors from BT_AUDIO.M4A's mdat box
     * and our ring buffer is empty, wait briefly (up to USB_READ_PACING_WAIT_MS)
     * on Core 1 for Core 0's BTstack A2DP callback to push a live AAC frame.
     * If no frame arrives before the deadline, fat32_read_sector() emits a valid
     * silent AAC-LC frame so the USB host never times out.
     */
    if (offset == 0 && fat32_is_live_mdat_lba(lba) &&
        stream_ringbuf_available_frames() == 0) {
        absolute_time_t deadline = make_timeout_time_ms(USB_READ_PACING_WAIT_MS);
        while (stream_ringbuf_available_frames() == 0 &&
               !time_reached(deadline)) {
            busy_wait_us_32(250);
        }
    }

    uint8_t *out = (uint8_t *)buffer;
    uint32_t remaining = bufsize;
    uint32_t cur_lba = lba + (offset / DISK_SECTOR_BYTES);
    uint32_t cur_off = offset % DISK_SECTOR_BYTES;

    while (remaining > 0) {
        if (cur_lba >= DISK_TOTAL_SECTORS) {
            memset(out, 0, remaining);
            break;
        }

        if (cur_off == 0 && remaining >= DISK_SECTOR_BYTES) {
            fat32_read_sector(cur_lba, out);
            out += DISK_SECTOR_BYTES;
            remaining -= DISK_SECTOR_BYTES;
            cur_lba++;
        } else {
            uint8_t sector_tmp[DISK_SECTOR_BYTES];
            fat32_read_sector(cur_lba, sector_tmp);
            uint32_t chunk = DISK_SECTOR_BYTES - cur_off;
            if (chunk > remaining) {
                chunk = remaining;
            }
            memcpy(out, sector_tmp + cur_off, chunk);
            out += chunk;
            remaining -= chunk;
            cur_off = 0;
            cur_lba++;
        }
    }

    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    (void)lba;
    (void)offset;
    (void)buffer;
    /* Virtual disk is read-only; absorb any host metadata writes gracefully */
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;

    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            return 0;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}
