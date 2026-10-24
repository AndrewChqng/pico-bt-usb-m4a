#ifndef PICO_BT_USB_M4A_CONFIG_H
#define PICO_BT_USB_M4A_CONFIG_H

#include <stdint.h>

/*
 * Target Head Unit Specification (Suzuki 84MS0-14E):
 * - USB 1.1/2.0 Full Speed Mass Storage Class (FAT12/16/32, VFAT)
 * - AAC (.m4a): 16k - 320 kbps, 32 / 44.1 / 48 kHz
 */

/* Bluetooth Device Configuration */
#define BT_DEVICE_NAME                  "Pico-USB-Audio"
#define BT_RECONNECT_INTERVAL_MS        5000U

/* Audio & AAC-LC Stream Parameters */
#define AAC_SAMPLE_RATE                 44100U
#define AAC_CHANNELS                    2U
#define AAC_SAMPLES_PER_FRAME           1024U
/* Fixed MP4 sample size in bytes:
 * 768 bytes/frame @ 44.1 kHz (1024 samples/frame) = 264,600 bps (264.6 kbps),
 * safely below the 320 kbps hardware ceiling while fitting 224 kbps A2DP AAC frames.
 * Every 2 frames (1536 bytes) align exactly to 3 x 512-byte USB sectors.
 */
#define AAC_FIXED_FRAME_BYTES           768U
#define AAC_MAX_A2DP_BITRATE            224000U

/* Ring Buffer & USB Read Pacing */
#define AAC_RINGBUF_CAPACITY_FRAMES     64U     /* 64 * 768B = 48 KB (~1.48s max buffer) */
#define USB_READ_PACING_WAIT_MS         28U     /* Max wait for live BT frame before emitting silence */

/* Virtual MP4 (.m4a) Container Layout */
#define M4A_HEADER_BYTES                4096U   /* 8 sectors (1 cluster): [ftyp][moov][free][mdat hdr] */
#define M4A_HEADER_SECTORS              (M4A_HEADER_BYTES / 512U)
#define M4A_TOTAL_FRAMES                2700000U /* ~17h 24m 53.9s; mdat payload = 2,073,600,000 bytes (< 2 GiB) */
#define M4A_MDAT_PAYLOAD_BYTES          ((uint32_t)M4A_TOTAL_FRAMES * AAC_FIXED_FRAME_BYTES)
#define M4A_MDAT_BOX_BYTES              (8U + M4A_MDAT_PAYLOAD_BYTES)
#define M4A_FILE_TOTAL_BYTES            (M4A_HEADER_BYTES + M4A_MDAT_PAYLOAD_BYTES)
#define M4A_FILE_TOTAL_SECTORS          (M4A_FILE_TOTAL_BYTES / 512U)
#define M4A_FILE_TOTAL_CLUSTERS         (M4A_FILE_TOTAL_SECTORS / 8U)

/* Virtual FAT32 Disk Layout (512-byte sectors, 8 sectors / 4096 bytes per cluster) */
#define DISK_SECTOR_BYTES               512U
#define FAT32_SECTORS_PER_CLUSTER       8U
#define FAT32_PARTITION_START_LBA       64U
#define FAT32_RESERVED_SECTORS          32U
#define FAT32_NUM_FATS                  2U
#define FAT32_TOTAL_CLUSTERS            512000U /* >= 65525 required for FAT32; covers 506,251 file clusters */
/* Each FAT sector holds 128 32-bit entries; 512,008 entries / 128 = 4001 sectors per FAT */
#define FAT32_SECTORS_PER_FAT           4008U
#define FAT32_FAT1_START_LBA            (FAT32_PARTITION_START_LBA + FAT32_RESERVED_SECTORS)
#define FAT32_FAT2_START_LBA            (FAT32_FAT1_START_LBA + FAT32_SECTORS_PER_FAT)
#define FAT32_DATA_START_LBA            (FAT32_FAT2_START_LBA + FAT32_SECTORS_PER_FAT)

/* Cluster assignments:
 * Cluster 2: Root Directory (8 sectors)
 * Cluster 3 .. (3 + M4A_FILE_TOTAL_CLUSTERS - 1): BT_AUDIO.M4A
 */
#define FAT32_ROOT_DIR_CLUSTER          2U
#define FAT32_FILE_FIRST_CLUSTER        3U
#define FAT32_FILE_LAST_CLUSTER         (FAT32_FILE_FIRST_CLUSTER + M4A_FILE_TOTAL_CLUSTERS - 1U)

#define FAT32_ROOT_DIR_START_LBA        FAT32_DATA_START_LBA
#define FAT32_FILE_START_LBA            (FAT32_DATA_START_LBA + FAT32_SECTORS_PER_CLUSTER)
#define FAT32_MDAT_PAYLOAD_START_LBA    (FAT32_FILE_START_LBA + M4A_HEADER_SECTORS)
#define FAT32_FILE_END_LBA              (FAT32_FILE_START_LBA + M4A_FILE_TOTAL_SECTORS)

#define FAT32_PARTITION_TOTAL_SECTORS   (FAT32_RESERVED_SECTORS + (FAT32_NUM_FATS * FAT32_SECTORS_PER_FAT) + (FAT32_TOTAL_CLUSTERS * FAT32_SECTORS_PER_CLUSTER))
#define DISK_TOTAL_SECTORS              (FAT32_PARTITION_START_LBA + FAT32_PARTITION_TOTAL_SECTORS)

#endif /* PICO_BT_USB_M4A_CONFIG_H */
