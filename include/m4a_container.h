#ifndef PICO_BT_USB_M4A_CONTAINER_H
#define PICO_BT_USB_M4A_CONTAINER_H

#include <stddef.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the complete 4096-byte (8-sector) M4A prefix header (`ftyp` + `moov` + `free` + 8B `mdat` header)
 * for the given number of AAC frames.
 *
 * The first AAC frame (mdat payload byte 0) starts immediately at byte offset 4096 (sector 8).
 */
void m4a_build_header_block(uint8_t out_header[M4A_HEADER_BYTES], uint32_t total_frames);

/**
 * Read a single 512-byte sector (0..7) from the 4096-byte M4A header for the default
 * M4A_TOTAL_FRAMES stream.
 */
void m4a_read_header_sector(uint32_t header_sector_idx, uint8_t out_sector[DISK_SECTOR_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* PICO_BT_USB_M4A_CONTAINER_H */
