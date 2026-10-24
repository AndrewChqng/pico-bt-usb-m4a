#include "m4a_container.h"
#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t pos;
    size_t cap;
} box_writer_t;

static void bw_u8(box_writer_t *w, uint8_t v) {
    if (w->pos < w->cap) {
        w->buf[w->pos++] = v;
    }
}

static void bw_u16be(box_writer_t *w, uint16_t v) {
    bw_u8(w, (uint8_t)(v >> 8));
    bw_u8(w, (uint8_t)(v & 0xFFU));
}

static void bw_u24be(box_writer_t *w, uint32_t v) {
    bw_u8(w, (uint8_t)((v >> 16) & 0xFFU));
    bw_u8(w, (uint8_t)((v >> 8) & 0xFFU));
    bw_u8(w, (uint8_t)(v & 0xFFU));
}

static void bw_u32be(box_writer_t *w, uint32_t v) {
    bw_u8(w, (uint8_t)((v >> 24) & 0xFFU));
    bw_u8(w, (uint8_t)((v >> 16) & 0xFFU));
    bw_u8(w, (uint8_t)((v >> 8) & 0xFFU));
    bw_u8(w, (uint8_t)(v & 0xFFU));
}

static void bw_fourcc(box_writer_t *w, const char fourcc[4]) {
    for (size_t i = 0; i < 4; i++) {
        bw_u8(w, (uint8_t)fourcc[i]);
    }
}

static void bw_zeros(box_writer_t *w, size_t count) {
    for (size_t i = 0; i < count; i++) {
        bw_u8(w, 0);
    }
}

static size_t bw_begin_box(box_writer_t *w, const char fourcc[4]) {
    size_t offset = w->pos;
    bw_u32be(w, 0); /* Placeholder for box size */
    bw_fourcc(w, fourcc);
    return offset;
}

static void bw_end_box(box_writer_t *w, size_t box_start) {
    uint32_t box_size = (uint32_t)(w->pos - box_start);
    w->buf[box_start + 0] = (uint8_t)((box_size >> 24) & 0xFFU);
    w->buf[box_start + 1] = (uint8_t)((box_size >> 16) & 0xFFU);
    w->buf[box_start + 2] = (uint8_t)((box_size >> 8) & 0xFFU);
    w->buf[box_start + 3] = (uint8_t)(box_size & 0xFFU);
}

static void bw_unity_matrix(box_writer_t *w) {
    bw_u32be(w, 0x00010000U); bw_u32be(w, 0);           bw_u32be(w, 0);
    bw_u32be(w, 0);           bw_u32be(w, 0x00010000U); bw_u32be(w, 0);
    bw_u32be(w, 0);           bw_u32be(w, 0);           bw_u32be(w, 0x40000000U);
}

void m4a_build_header_block(uint8_t out_header[M4A_HEADER_BYTES], uint32_t total_frames) {
    memset(out_header, 0, M4A_HEADER_BYTES);
    box_writer_t w = {
        .buf = out_header,
        .pos = 0,
        .cap = M4A_HEADER_BYTES
    };

    const uint32_t duration_samples = total_frames * AAC_SAMPLES_PER_FRAME;
    const uint32_t container_bitrate =
        (AAC_FIXED_FRAME_BYTES * 8U * AAC_SAMPLE_RATE) / AAC_SAMPLES_PER_FRAME; /* 264,600 bps */

    /* 1. ftyp box (28 bytes) */
    size_t ftyp = bw_begin_box(&w, "ftyp");
    bw_fourcc(&w, "M4A ");
    bw_u32be(&w, 0);
    bw_fourcc(&w, "M4A ");
    bw_fourcc(&w, "mp42");
    bw_fourcc(&w, "isom");
    bw_end_box(&w, ftyp);

    /* 2. moov box */
    size_t moov = bw_begin_box(&w, "moov");
    {
        /* 2.1 mvhd (Movie Header Box, v0) */
        size_t mvhd = bw_begin_box(&w, "mvhd");
        bw_u8(&w, 0);          /* version 0 */
        bw_u24be(&w, 0);       /* flags */
        bw_u32be(&w, 0);       /* creation_time */
        bw_u32be(&w, 0);       /* modification_time */
        bw_u32be(&w, AAC_SAMPLE_RATE);
        bw_u32be(&w, duration_samples);
        bw_u32be(&w, 0x00010000U); /* rate = 1.0 */
        bw_u16be(&w, 0x0100U);     /* volume = 1.0 */
        bw_zeros(&w, 10);          /* reserved */
        bw_unity_matrix(&w);
        bw_zeros(&w, 24);          /* pre_defined */
        bw_u32be(&w, 2U);          /* next_track_ID */
        bw_end_box(&w, mvhd);

        /* 2.2 trak (Track Box) */
        size_t trak = bw_begin_box(&w, "trak");
        {
            /* 2.2.1 tkhd (Track Header Box, v0) */
            size_t tkhd = bw_begin_box(&w, "tkhd");
            bw_u8(&w, 0);          /* version 0 */
            bw_u24be(&w, 0x000007U); /* flags: enabled | in_movie | in_preview */
            bw_u32be(&w, 0);       /* creation_time */
            bw_u32be(&w, 0);       /* modification_time */
            bw_u32be(&w, 1U);      /* track_ID */
            bw_u32be(&w, 0);       /* reserved */
            bw_u32be(&w, duration_samples);
            bw_zeros(&w, 8);       /* reserved */
            bw_u16be(&w, 0);       /* layer */
            bw_u16be(&w, 0);       /* alternate_group */
            bw_u16be(&w, 0x0100U); /* volume = 1.0 (audio track) */
            bw_u16be(&w, 0);       /* reserved */
            bw_unity_matrix(&w);
            bw_u32be(&w, 0);       /* width = 0 */
            bw_u32be(&w, 0);       /* height = 0 */
            bw_end_box(&w, tkhd);

            /* 2.2.2 mdia (Media Box) */
            size_t mdia = bw_begin_box(&w, "mdia");
            {
                /* mdhd (Media Header Box, v0) */
                size_t mdhd = bw_begin_box(&w, "mdhd");
                bw_u8(&w, 0);
                bw_u24be(&w, 0);
                bw_u32be(&w, 0);
                bw_u32be(&w, 0);
                bw_u32be(&w, AAC_SAMPLE_RATE);
                bw_u32be(&w, duration_samples);
                bw_u16be(&w, 0x55C4U); /* language = 'und' */
                bw_u16be(&w, 0);       /* pre_defined */
                bw_end_box(&w, mdhd);

                /* hdlr (Handler Reference Box) */
                size_t hdlr = bw_begin_box(&w, "hdlr");
                bw_u8(&w, 0);
                bw_u24be(&w, 0);
                bw_u32be(&w, 0);       /* pre_defined */
                bw_fourcc(&w, "soun"); /* handler_type */
                bw_zeros(&w, 12);      /* reserved */
                const char hdlr_name[] = "SoundHandler";
                for (size_t i = 0; i < sizeof(hdlr_name); i++) {
                    bw_u8(&w, (uint8_t)hdlr_name[i]);
                }
                bw_end_box(&w, hdlr);

                /* minf (Media Information Box) */
                size_t minf = bw_begin_box(&w, "minf");
                {
                    /* smhd (Sound Media Header Box) */
                    size_t smhd = bw_begin_box(&w, "smhd");
                    bw_u8(&w, 0);
                    bw_u24be(&w, 0);
                    bw_u16be(&w, 0); /* balance */
                    bw_u16be(&w, 0); /* reserved */
                    bw_end_box(&w, smhd);

                    /* dinf -> dref -> url */
                    size_t dinf = bw_begin_box(&w, "dinf");
                    {
                        size_t dref = bw_begin_box(&w, "dref");
                        bw_u8(&w, 0);
                        bw_u24be(&w, 0);
                        bw_u32be(&w, 1U); /* entry_count = 1 */
                        {
                            size_t url = bw_begin_box(&w, "url ");
                            bw_u8(&w, 0);
                            bw_u24be(&w, 0x000001U); /* self-contained */
                            bw_end_box(&w, url);
                        }
                        bw_end_box(&w, dref);
                    }
                    bw_end_box(&w, dinf);

                    /* stbl (Sample Table Box) */
                    size_t stbl = bw_begin_box(&w, "stbl");
                    {
                        /* stsd -> mp4a -> esds */
                        size_t stsd = bw_begin_box(&w, "stsd");
                        bw_u8(&w, 0);
                        bw_u24be(&w, 0);
                        bw_u32be(&w, 1U); /* entry_count = 1 */
                        {
                            size_t mp4a = bw_begin_box(&w, "mp4a");
                            bw_zeros(&w, 6);                /* reserved */
                            bw_u16be(&w, 1U);               /* data_reference_index = 1 */
                            bw_zeros(&w, 8);                /* reserved */
                            bw_u16be(&w, AAC_CHANNELS);     /* channelcount = 2 */
                            bw_u16be(&w, 16U);              /* samplesize = 16 */
                            bw_u16be(&w, 0);                /* pre_defined */
                            bw_u16be(&w, 0);                /* reserved */
                            bw_u32be(&w, AAC_SAMPLE_RATE << 16);

                            /* esds (Elementary Stream Descriptor Box) */
                            size_t esds = bw_begin_box(&w, "esds");
                            bw_u8(&w, 0);  /* version 0 */
                            bw_u24be(&w, 0); /* flags */

                            /* ES_Descriptor (tag 0x03, length 25) */
                            bw_u8(&w, 0x03U);
                            bw_u8(&w, 25U);
                            bw_u16be(&w, 1U); /* ES_ID = 1 */
                            bw_u8(&w, 0x00U); /* flags */

                            /* DecoderConfigDescriptor (tag 0x04, length 17) */
                            bw_u8(&w, 0x04U);
                            bw_u8(&w, 17U);
                            bw_u8(&w, 0x40U); /* objectTypeIndication = 0x40 (MPEG-4 Audio) */
                            bw_u8(&w, 0x15U); /* streamType = 5 (AudioStream) << 2 | 1 */
                            bw_u24be(&w, AAC_FIXED_FRAME_BYTES * 2U); /* bufferSizeDB */
                            bw_u32be(&w, container_bitrate); /* maxBitrate */
                            bw_u32be(&w, container_bitrate); /* avgBitrate */

                            /* DecoderSpecificInfo (tag 0x05, length 2) */
                            bw_u8(&w, 0x05U);
                            bw_u8(&w, 2U);
                            /* AudioSpecificConfig: AAC-LC (2), 44.1 kHz (index 4), Stereo (2):
                             * 00010 0100 0010 000 = 0x12 0x10
                             */
                            bw_u8(&w, 0x12U);
                            bw_u8(&w, 0x10U);

                            /* SLConfigDescriptor (tag 0x06, length 1) */
                            bw_u8(&w, 0x06U);
                            bw_u8(&w, 1U);
                            bw_u8(&w, 0x02U); /* predefined = MP4 */

                            bw_end_box(&w, esds);
                            bw_end_box(&w, mp4a);
                        }
                        bw_end_box(&w, stsd);

                        /* stts (Decoding Time to Sample Box) */
                        size_t stts = bw_begin_box(&w, "stts");
                        bw_u8(&w, 0);
                        bw_u24be(&w, 0);
                        bw_u32be(&w, 1U); /* entry_count = 1 */
                        bw_u32be(&w, total_frames);
                        bw_u32be(&w, AAC_SAMPLES_PER_FRAME); /* 1024 */
                        bw_end_box(&w, stts);

                        /* stsc (Sample To Chunk Box) */
                        size_t stsc = bw_begin_box(&w, "stsc");
                        bw_u8(&w, 0);
                        bw_u24be(&w, 0);
                        bw_u32be(&w, 1U); /* entry_count = 1 */
                        bw_u32be(&w, 1U); /* first_chunk = 1 */
                        bw_u32be(&w, total_frames); /* samples_per_chunk = all */
                        bw_u32be(&w, 1U); /* sample_description_index = 1 */
                        bw_end_box(&w, stsc);

                        /* stsz (Sample Size Box - constant sample size = 768 bytes) */
                        size_t stsz = bw_begin_box(&w, "stsz");
                        bw_u8(&w, 0);
                        bw_u24be(&w, 0);
                        bw_u32be(&w, AAC_FIXED_FRAME_BYTES); /* constant sample_size = 768 */
                        bw_u32be(&w, total_frames);          /* sample_count */
                        bw_end_box(&w, stsz);

                        /* stco (Chunk Offset Box) */
                        size_t stco = bw_begin_box(&w, "stco");
                        bw_u8(&w, 0);
                        bw_u24be(&w, 0);
                        bw_u32be(&w, 1U);               /* entry_count = 1 */
                        bw_u32be(&w, M4A_HEADER_BYTES); /* chunk 1 starts at byte 4096 */
                        bw_end_box(&w, stco);
                    }
                    bw_end_box(&w, stbl);
                }
                bw_end_box(&w, minf);
            }
            bw_end_box(&w, mdia);
        }
        bw_end_box(&w, trak);
    }
    bw_end_box(&w, moov);

    /* 3. Pad with a 'free' box up to byte 4088 (so 8-byte mdat header sits at 4088..4095) */
    const size_t mdat_hdr_offset = M4A_HEADER_BYTES - 8U;
    size_t free_box = bw_begin_box(&w, "free");
    while (w.pos < mdat_hdr_offset) {
        bw_u8(&w, 0);
    }
    bw_end_box(&w, free_box);

    /* 4. 8-byte mdat box header at 4088..4095 */
    uint32_t mdat_box_size = 8U + (total_frames * AAC_FIXED_FRAME_BYTES);
    bw_u32be(&w, mdat_box_size);
    bw_fourcc(&w, "mdat");
}

void m4a_read_header_sector(uint32_t header_sector_idx, uint8_t out_sector[DISK_SECTOR_BYTES]) {
    static uint8_t cached_header[M4A_HEADER_BYTES];
    static int initialized = 0;

    if (!initialized) {
        m4a_build_header_block(cached_header, M4A_TOTAL_FRAMES);
        initialized = 1;
    }

    if (header_sector_idx < M4A_HEADER_SECTORS) {
        memcpy(out_sector, cached_header + (header_sector_idx * DISK_SECTOR_BYTES), DISK_SECTOR_BYTES);
    } else {
        memset(out_sector, 0, DISK_SECTOR_BYTES);
    }
}
