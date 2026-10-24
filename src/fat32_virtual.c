#include "fat32_virtual.h"
#include "m4a_container.h"
#include "stream_ringbuf.h"
#include <string.h>

static inline void put_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static inline void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

bool fat32_is_live_mdat_lba(uint32_t lba) {
    return (lba >= FAT32_MDAT_PAYLOAD_START_LBA) && (lba < FAT32_FILE_END_LBA);
}

static void build_mbr_sector(uint8_t out[DISK_SECTOR_BYTES]) {
    memset(out, 0, DISK_SECTOR_BYTES);
    /* Disk signature */
    put_u32le(out + 440, 0x5049434FU); /* 'OCIP' */

    /* Partition 1 entry at offset 446 */
    uint8_t *p1 = out + 446;
    p1[0] = 0x80U; /* Active / bootable flag */
    p1[1] = 0x01U; /* Start head */
    p1[2] = 0x01U; /* Start sector/cylinder */
    p1[3] = 0x00U;
    p1[4] = 0x0CU; /* Partition type: FAT32 LBA */
    p1[5] = 0xFEU; /* End head */
    p1[6] = 0xFFU; /* End sector/cylinder */
    p1[7] = 0xFFU;
    put_u32le(p1 + 8, FAT32_PARTITION_START_LBA);
    put_u32le(p1 + 12, FAT32_PARTITION_TOTAL_SECTORS);

    out[510] = 0x55U;
    out[511] = 0xAAU;
}

static void build_bpb_sector(uint8_t out[DISK_SECTOR_BYTES]) {
    memset(out, 0, DISK_SECTOR_BYTES);
    out[0] = 0xEBU;
    out[1] = 0x58U;
    out[2] = 0x90U;
    memcpy(out + 3, "MSWIN4.1", 8);

    put_u16le(out + 11, DISK_SECTOR_BYTES);        /* BytsPerSec = 512 */
    out[13] = FAT32_SECTORS_PER_CLUSTER;           /* SecPerClus = 8 */
    put_u16le(out + 14, FAT32_RESERVED_SECTORS);   /* RsvdSecCnt = 32 */
    out[16] = FAT32_NUM_FATS;                      /* NumFATs = 2 */
    put_u16le(out + 17, 0);                        /* RootEntCnt = 0 (FAT32) */
    put_u16le(out + 19, 0);                        /* TotSec16 = 0 */
    out[21] = 0xF8U;                               /* Media = Fixed/Flash */
    put_u16le(out + 22, 0);                        /* FATSz16 = 0 */
    put_u16le(out + 24, 63U);                      /* SecPerTrk */
    put_u16le(out + 26, 255U);                     /* NumHeads */
    put_u32le(out + 28, FAT32_PARTITION_START_LBA);/* HiddSec */
    put_u32le(out + 32, FAT32_PARTITION_TOTAL_SECTORS); /* TotSec32 */

    /* FAT32 Extended BPB */
    put_u32le(out + 36, FAT32_SECTORS_PER_FAT);    /* FATSz32 */
    put_u16le(out + 40, 0x0000U);                  /* ExtFlags */
    put_u16le(out + 42, 0x0000U);                  /* FSVer = 0.0 */
    put_u32le(out + 44, FAT32_ROOT_DIR_CLUSTER);   /* RootClus = 2 */
    put_u16le(out + 48, 1U);                       /* FSInfo = sector 1 */
    put_u16le(out + 50, 6U);                       /* BkBootSec = sector 6 */
    out[64] = 0x80U;                               /* DrvNum */
    out[65] = 0x00U;                               /* Reserved1 */
    out[66] = 0x29U;                               /* BootSig */
    put_u32le(out + 67, 0x20261024U);              /* VolID */
    memcpy(out + 71, "PICO_BTAUD ", 11);           /* VolLab */
    memcpy(out + 82, "FAT32   ", 8);               /* FilSysType */

    out[510] = 0x55U;
    out[511] = 0xAAU;
}

static void build_fsinfo_sector(uint8_t out[DISK_SECTOR_BYTES]) {
    memset(out, 0, DISK_SECTOR_BYTES);
    put_u32le(out + 0, 0x41615252U);   /* LeadSig "RRaA" */
    put_u32le(out + 484, 0x61417272U); /* StrucSig "rrAa" */

    uint32_t used_clusters = 1U + M4A_FILE_TOTAL_CLUSTERS; /* Root dir + file */
    uint32_t free_clusters = FAT32_TOTAL_CLUSTERS - used_clusters;
    put_u32le(out + 488, free_clusters);
    put_u32le(out + 492, FAT32_FILE_LAST_CLUSTER + 1U);
    put_u32le(out + 508, 0xAA550000U); /* TrailSig */
}

static void build_fat_sector(uint32_t fat_sector_idx, uint8_t out[DISK_SECTOR_BYTES]) {
    uint32_t base_cluster = fat_sector_idx * 128U;
    for (uint32_t i = 0; i < 128U; i++) {
        uint32_t cluster = base_cluster + i;
        uint32_t val = 0x00000000U;
        if (cluster == 0U) {
            val = 0x0FFFFFF8U;
        } else if (cluster == 1U) {
            val = 0x0FFFFFFFU;
        } else if (cluster == FAT32_ROOT_DIR_CLUSTER) {
            val = 0x0FFFFFFFU;
        } else if (cluster >= FAT32_FILE_FIRST_CLUSTER && cluster < FAT32_FILE_LAST_CLUSTER) {
            val = cluster + 1U;
        } else if (cluster == FAT32_FILE_LAST_CLUSTER) {
            val = 0x0FFFFFFFU;
        }
        put_u32le(out + (i * 4U), val);
    }
}

static void build_root_dir_sector0(uint8_t out[DISK_SECTOR_BYTES]) {
    memset(out, 0, DISK_SECTOR_BYTES);

    /* Entry 0: Volume Label */
    uint8_t *e0 = out + 0;
    memcpy(e0 + 0, "PICO_BTAUD ", 11);
    e0[11] = 0x08U; /* ATTR_VOLUME_ID */
    put_u16le(e0 + 22, 0x0000U);
    put_u16le(e0 + 24, 0x5558U); /* Date: 2026-10-24 */

    /* Entry 1: BT_AUDIO.M4A (8.3 standard uppercase entry) */
    uint8_t *e1 = out + 32;
    memcpy(e1 + 0, "BT_AUDIO", 8);
    memcpy(e1 + 8, "M4A", 3);
    e1[11] = 0x21U; /* ATTR_ARCHIVE | ATTR_READ_ONLY */
    put_u16le(e1 + 14, 0x0000U); /* Creation time */
    put_u16le(e1 + 16, 0x5558U); /* Creation date */
    put_u16le(e1 + 18, 0x5558U); /* Last access date */
    put_u16le(e1 + 20, (uint16_t)(FAT32_FILE_FIRST_CLUSTER >> 16));
    put_u16le(e1 + 22, 0x0000U); /* Write time */
    put_u16le(e1 + 24, 0x5558U); /* Write date */
    put_u16le(e1 + 26, (uint16_t)(FAT32_FILE_FIRST_CLUSTER & 0xFFFFU));
    put_u32le(e1 + 28, M4A_FILE_TOTAL_BYTES);
}

void fat32_read_sector(uint32_t lba, uint8_t out_sector[DISK_SECTOR_BYTES]) {
    /* 1. Master Boot Record */
    if (lba == 0U) {
        build_mbr_sector(out_sector);
        return;
    }

    /* 2. Partition Boot Sector & Backup Boot Sector */
    if (lba == FAT32_PARTITION_START_LBA || lba == (FAT32_PARTITION_START_LBA + 6U)) {
        build_bpb_sector(out_sector);
        return;
    }

    /* 3. FSInfo & Backup FSInfo */
    if (lba == (FAT32_PARTITION_START_LBA + 1U) || lba == (FAT32_PARTITION_START_LBA + 7U)) {
        build_fsinfo_sector(out_sector);
        return;
    }

    /* 4. FAT1 */
    if (lba >= FAT32_FAT1_START_LBA && lba < (FAT32_FAT1_START_LBA + FAT32_SECTORS_PER_FAT)) {
        build_fat_sector(lba - FAT32_FAT1_START_LBA, out_sector);
        return;
    }

    /* 5. FAT2 (mirror of FAT1) */
    if (lba >= FAT32_FAT2_START_LBA && lba < (FAT32_FAT2_START_LBA + FAT32_SECTORS_PER_FAT)) {
        build_fat_sector(lba - FAT32_FAT2_START_LBA, out_sector);
        return;
    }

    /* 6. Root Directory (Cluster 2, first sector has entries, rest are 0) */
    if (lba == FAT32_ROOT_DIR_START_LBA) {
        build_root_dir_sector0(out_sector);
        return;
    }

    /* 7. BT_AUDIO.M4A Header (First 8 sectors / 4096 bytes of file) */
    if (lba >= FAT32_FILE_START_LBA && lba < FAT32_MDAT_PAYLOAD_START_LBA) {
        m4a_read_header_sector(lba - FAT32_FILE_START_LBA, out_sector);
        return;
    }

    /* 8. BT_AUDIO.M4A Live mdat Payload */
    if (lba >= FAT32_MDAT_PAYLOAD_START_LBA && lba < FAT32_FILE_END_LBA) {
        stream_ringbuf_read_mdat_sector(lba - FAT32_MDAT_PAYLOAD_START_LBA, out_sector);
        return;
    }

    /* All other sectors are zero */
    memset(out_sector, 0, DISK_SECTOR_BYTES);
}
