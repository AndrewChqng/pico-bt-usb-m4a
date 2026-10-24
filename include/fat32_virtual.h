#ifndef PICO_BT_USB_M4A_FAT32_VIRTUAL_H
#define PICO_BT_USB_M4A_FAT32_VIRTUAL_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns true if the given LBA falls within the live streaming mdat payload
 * region of BT_AUDIO.M4A (used by the USB MSC layer to decide whether to apply
 * real-time SCSI read pacing).
 */
bool fat32_is_live_mdat_lba(uint32_t lba);

/**
 * Synthesize a single 512-byte sector at `lba` from the virtual FAT32 disk.
 * Metadata sectors (MBR, BPB, FSInfo, FAT1/2, Root Dir, M4A header) are generated
 * procedurally with zero RAM overhead; live mdat sectors pull from the AAC ring buffer.
 */
void fat32_read_sector(uint32_t lba, uint8_t out_sector[DISK_SECTOR_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* PICO_BT_USB_M4A_FAT32_VIRTUAL_H */
