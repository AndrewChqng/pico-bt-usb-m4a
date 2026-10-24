#ifndef PICO_BT_USB_M4A_STREAM_RINGBUF_H
#define PICO_BT_USB_M4A_STREAM_RINGBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize/reset the lock-free SPSC AAC frame ring buffer and 3-sector
 * phase-alignment cache.
 */
void stream_ringbuf_init(void);

/**
 * Push one 768-byte padded AAC frame into the ring buffer.
 * Safe to call from producer context (BTstack A2DP callback).
 * Returns false if the ring buffer is full.
 */
bool stream_ringbuf_push_frame(const uint8_t frame[AAC_FIXED_FRAME_BYTES]);

/**
 * Pop one 768-byte padded AAC frame from the ring buffer.
 * Returns false if the ring buffer is empty.
 */
bool stream_ringbuf_pop_frame(uint8_t out_frame[AAC_FIXED_FRAME_BYTES]);

/**
 * Return the number of queued 768-byte AAC frames currently available.
 */
uint32_t stream_ringbuf_available_frames(void);

/**
 * Read a 512-byte sector from the M4A mdat payload region at `mdat_sector_idx`
 * (where sector 0 is byte offset 4096 of BT_AUDIO.M4A).
 *
 * Uses a 4-way phase-coherent pair cache (3 sectors = 1536 bytes = 2 x 768B AAC frames)
 * so that multi-sector or single-sector SCSI READ(10) transfers never tear an AAC frame.
 */
void stream_ringbuf_read_mdat_sector(uint32_t mdat_sector_idx,
                                     uint8_t out_sector[DISK_SECTOR_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* PICO_BT_USB_M4A_STREAM_RINGBUF_H */
