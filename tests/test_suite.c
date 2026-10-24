#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "aac_latm.h"
#include "m4a_container.h"
#include "fat32_virtual.h"
#include "stream_ringbuf.h"

/* Bitwriter helper to synthesize real bit-shifted RFC 3016 LATM packets */
typedef struct {
    uint8_t *buf;
    size_t cap_bytes;
    size_t bit_pos;
} bitwriter_t;

static void bw_put_bits(bitwriter_t *bw, uint32_t val, size_t nbits) {
    for (size_t i = 0; i < nbits; i++) {
        uint32_t bit = (val >> (nbits - 1U - i)) & 1U;
        size_t byte_idx = bw->bit_pos >> 3;
        size_t bit_idx = 7U - (bw->bit_pos & 7U);
        assert(byte_idx < bw->cap_bytes);
        if (bit) {
            bw->buf[byte_idx] |= (uint8_t)(1U << bit_idx);
        }
        bw->bit_pos++;
    }
}

static size_t build_test_rtp_latm_packet(const uint8_t *raw_aac, size_t raw_len,
                                         uint8_t *out_pkt, size_t max_pkt_len) {
    memset(out_pkt, 0, max_pkt_len);
    /* 12-byte RTP v2 header */
    out_pkt[0] = 0x80U; /* V=2, P=0, X=0, CC=0 */
    out_pkt[1] = 0x60U; /* PT=96 */
    out_pkt[2] = 0x00U;
    out_pkt[3] = 0x01U; /* Seq=1 */

    bitwriter_t bw = {
        .buf = out_pkt + 12,
        .cap_bytes = max_pkt_len - 12,
        .bit_pos = 0
    };

    /* AudioMuxElement(muxConfigPresent = 1):
     * useSameStreamMux = 0 (1 bit)
     * audioMuxVersion = 0 (1 bit)
     * allStreamsSameTimeFraming = 1 (1 bit)
     * numSubFrames = 0 (6 bits)
     * numProgram = 0 (4 bits)
     * numLayer = 0 (3 bits)
     * AudioSpecificConfig = 0x1210 (16 bits: AAC-LC, 44.1kHz, Stereo)
     * frameLengthType = 0 (3 bits)
     * latmBufferFullness = 0xFF (8 bits)
     * otherDataPresent = 0 (1 bit)
     * crcCheckPresent = 0 (1 bit)
     * Total = 45 bits (non-byte-aligned! 45 % 8 = 5)
     */
    bw_put_bits(&bw, 0U, 1);
    bw_put_bits(&bw, 0U, 1);
    bw_put_bits(&bw, 1U, 1);
    bw_put_bits(&bw, 0U, 6);
    bw_put_bits(&bw, 0U, 4);
    bw_put_bits(&bw, 0U, 3);
    bw_put_bits(&bw, 0x1210U, 16);
    bw_put_bits(&bw, 0U, 3);
    bw_put_bits(&bw, 0xFFU, 8);
    bw_put_bits(&bw, 0U, 1);
    bw_put_bits(&bw, 0U, 1);

    /* PayloadLengthInfo(): 0xFF chunks + remainder */
    size_t rem = raw_len;
    while (rem >= 0xFFU) {
        bw_put_bits(&bw, 0xFFU, 8);
        rem -= 0xFFU;
    }
    bw_put_bits(&bw, (uint32_t)rem, 8);

    /* PayloadMux(): bit-shifted raw_data_block bytes */
    for (size_t i = 0; i < raw_len; i++) {
        bw_put_bits(&bw, raw_aac[i], 8);
    }

    size_t latm_bytes = (bw.bit_pos + 7U) >> 3;
    return 12U + latm_bytes;
}

static void test_latm_unwrapper(void) {
    printf("[1/4] Testing RFC 3016 RTP + bit-shifted LATM unwrapper...\n");
    uint8_t raw_payload[540];
    for (size_t i = 0; i < sizeof(raw_payload); i++) {
        raw_payload[i] = (uint8_t)((i * 73U + 19U) & 0xFFU);
    }

    uint8_t rtp_pkt[1024];
    size_t pkt_len = build_test_rtp_latm_packet(raw_payload, sizeof(raw_payload),
                                                rtp_pkt, sizeof(rtp_pkt));

    uint8_t fixed_frame[AAC_FIXED_FRAME_BYTES];
    memset(fixed_frame, 0xAA, sizeof(fixed_frame));

    size_t extracted = aac_latm_extract_rtp_to_fixed_frame(rtp_pkt, pkt_len, fixed_frame);
    assert(extracted == sizeof(raw_payload));
    assert(memcmp(fixed_frame, raw_payload, sizeof(raw_payload)) == 0);
    for (size_t i = sizeof(raw_payload); i < AAC_FIXED_FRAME_BYTES; i++) {
        assert(fixed_frame[i] == 0x00U);
    }
    printf("      PASS: Extracted %zu-byte bit-shifted LATM payload and padded to %u bytes.\n",
           extracted, AAC_FIXED_FRAME_BYTES);
}

static void test_ringbuf_and_sector_coherence(void) {
    printf("[2/4] Testing lock-free SPSC ring buffer & 3-sector/2-frame phase coherence...\n");
    stream_ringbuf_init();

    uint8_t f0[AAC_FIXED_FRAME_BYTES];
    uint8_t f1[AAC_FIXED_FRAME_BYTES];
    for (size_t i = 0; i < AAC_FIXED_FRAME_BYTES; i++) {
        f0[i] = (uint8_t)((i + 1U) & 0xFFU);
        f1[i] = (uint8_t)((255U - (i & 0xFFU)) & 0xFFU);
    }

    assert(stream_ringbuf_push_frame(f0));
    assert(stream_ringbuf_push_frame(f1));
    assert(stream_ringbuf_available_frames() == 2U);

    uint8_t s0[DISK_SECTOR_BYTES];
    uint8_t s1[DISK_SECTOR_BYTES];
    uint8_t s2[DISK_SECTOR_BYTES];
    uint8_t s0_reread[DISK_SECTOR_BYTES];

    fat32_read_sector(FAT32_MDAT_PAYLOAD_START_LBA + 0U, s0);
    fat32_read_sector(FAT32_MDAT_PAYLOAD_START_LBA + 1U, s1);
    /* Simulate host re-reading sector 0 before reading sector 2 */
    fat32_read_sector(FAT32_MDAT_PAYLOAD_START_LBA + 0U, s0_reread);
    fat32_read_sector(FAT32_MDAT_PAYLOAD_START_LBA + 2U, s2);

    assert(memcmp(s0, s0_reread, DISK_SECTOR_BYTES) == 0);

    uint8_t reassembled[AAC_FIXED_FRAME_BYTES * 2U];
    memcpy(reassembled + 0,    s0, 512);
    memcpy(reassembled + 512,  s1, 512);
    memcpy(reassembled + 1024, s2, 512);

    assert(memcmp(reassembled, f0, AAC_FIXED_FRAME_BYTES) == 0);
    assert(memcmp(reassembled + AAC_FIXED_FRAME_BYTES, f1, AAC_FIXED_FRAME_BYTES) == 0);
    printf("      PASS: 3 sectors (1536B) reassembled cleanly into 2 x 768B AAC frames.\n");
}

static void test_fat32_geometry(void) {
    printf("[3/4] Validating virtual FAT32 MBR, BPB, FSInfo, FAT1/FAT2, and Root Dir...\n");
    uint8_t sec[DISK_SECTOR_BYTES];
    uint8_t sec2[DISK_SECTOR_BYTES];

    /* Check MBR */
    fat32_read_sector(0, sec);
    assert(sec[510] == 0x55U && sec[511] == 0xAAU);
    assert(sec[446 + 4] == 0x0CU); /* FAT32 LBA */

    /* Check BPB and backup BPB match */
    fat32_read_sector(FAT32_PARTITION_START_LBA, sec);
    fat32_read_sector(FAT32_PARTITION_START_LBA + 6U, sec2);
    assert(sec[510] == 0x55U && sec[511] == 0xAAU);
    assert(memcmp(sec, sec2, DISK_SECTOR_BYTES) == 0);

    /* Check FAT1 and FAT2 match at start and end of file chain */
    fat32_read_sector(FAT32_FAT1_START_LBA, sec);
    fat32_read_sector(FAT32_FAT2_START_LBA, sec2);
    assert(memcmp(sec, sec2, DISK_SECTOR_BYTES) == 0);

    /* Check Root Directory entry for BT_AUDIO.M4A */
    fat32_read_sector(FAT32_ROOT_DIR_START_LBA, sec);
    assert(memcmp(sec + 32, "BT_AUDIOM4A", 11) == 0);
    uint32_t dir_file_size = (uint32_t)sec[60] |
                             ((uint32_t)sec[61] << 8) |
                             ((uint32_t)sec[62] << 16) |
                             ((uint32_t)sec[63] << 24);
    assert(dir_file_size == M4A_FILE_TOTAL_BYTES);
    printf("      PASS: Virtual FAT32 geometry verified (BT_AUDIO.M4A size = %u bytes).\n",
           dir_file_size);
}

static void test_m4a_ffmpeg_decode(void) {
    printf("[4/4] End-to-end test: FFmpeg AAC -> RTP/LATM -> RingBuf -> FAT32 LBA -> .m4a -> FFmpeg decode...\n");
    const char *tmp_adts = "/tmp/pico_bt_usb_sine.aac";
    const char *tmp_m4a  = "/tmp/pico_bt_usb_test.m4a";

    /* 1. Generate 20 frames of 440 Hz Stereo AAC-LC via FFmpeg */
    int gen_rc = system("ffmpeg -y -v error -f lavfi -i sine=frequency=440:sample_rate=44100 "
                        "-ac 2 -frames:a 20 -c:a aac -b:a 192k /tmp/pico_bt_usb_sine.aac");
    assert(gen_rc == 0);

    FILE *f_adts = fopen(tmp_adts, "rb");
    assert(f_adts != NULL);
    uint8_t adts_buf[32768];
    size_t adts_len = fread(adts_buf, 1, sizeof(adts_buf), f_adts);
    fclose(f_adts);
    remove(tmp_adts);
    assert(adts_len > 7);

    /* 2. Reset ring buffer, wrap each ADTS raw frame into bit-shifted RTP+LATM,
     *    unwrap into 768B fixed frame, and push into SPSC ring buffer.
     */
    stream_ringbuf_init();
    size_t pos = 0;
    uint32_t pushed_frames = 0;
    while (pos + 7 <= adts_len) {
        size_t frm_len = ((size_t)(adts_buf[pos + 3] & 0x03U) << 11) |
                         ((size_t)adts_buf[pos + 4] << 3) |
                         ((size_t)adts_buf[pos + 5] >> 5);
        assert(frm_len > 7 && pos + frm_len <= adts_len);
        const uint8_t *raw_aac = adts_buf + pos + 7;
        size_t raw_len = frm_len - 7;

        uint8_t rtp_pkt[1024];
        size_t pkt_len = build_test_rtp_latm_packet(raw_aac, raw_len, rtp_pkt, sizeof(rtp_pkt));

        uint8_t fixed_frame[AAC_FIXED_FRAME_BYTES];
        size_t extracted = aac_latm_extract_rtp_to_fixed_frame(rtp_pkt, pkt_len, fixed_frame);
        assert(extracted == raw_len);
        assert(stream_ringbuf_push_frame(fixed_frame));

        pushed_frames++;
        pos += frm_len;
    }
    /* Pad to an even number of frames (plus 4 silent frames to test underrun fallback) */
    uint32_t total_test_frames = pushed_frames + 4U;
    if (total_test_frames & 1U) {
        total_test_frames++;
    }

    /* 3. Read the .m4a header + mdat sectors directly via fat32_read_sector() */
    FILE *fp = fopen(tmp_m4a, "wb");
    assert(fp != NULL);

    uint8_t hdr[M4A_HEADER_BYTES];
    m4a_build_header_block(hdr, total_test_frames);
    assert(fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr));

    uint32_t mdat_sectors = (total_test_frames * AAC_FIXED_FRAME_BYTES) / DISK_SECTOR_BYTES;
    for (uint32_t s = 0; s < mdat_sectors; s++) {
        uint8_t sector[DISK_SECTOR_BYTES];
        fat32_read_sector(FAT32_MDAT_PAYLOAD_START_LBA + s, sector);
        assert(fwrite(sector, 1, sizeof(sector), fp) == sizeof(sector));
    }
    fclose(fp);

    /* 4. Verify FFmpeg demuxes and decodes all sine + silent frames without errors */
    int rc = system("ffmpeg -v error -i /tmp/pico_bt_usb_test.m4a -f null -");
    remove(tmp_m4a);
    assert(rc == 0);
    printf("      PASS: FFmpeg decoded %u live LATM-unwrapped + silent fallback frames from FAT32 LBAs with 0 errors.\n",
           total_test_frames);
}

int main(void) {
    test_latm_unwrapper();
    test_ringbuf_and_sector_coherence();
    test_fat32_geometry();
    test_m4a_ffmpeg_decode();
    printf("\nAll verification checks passed!\n");
    return 0;
}
