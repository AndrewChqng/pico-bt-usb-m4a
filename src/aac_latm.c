#include "aac_latm.h"
#include <string.h>

/*
 * Minimal valid 44.1 kHz Stereo AAC-LC silent raw_data_block (6 bytes):
 * ID_CPE (Channel Pair Element) + zeroed ICS + ID_END (0b111) byte-aligned.
 * Verified with FFmpeg native AAC decoder.
 */
static const uint8_t kSilentAacLcStereo44k1[6] = {
    0x21, 0x10, 0x04, 0x60, 0x8C, 0x1C
};

typedef struct {
    const uint8_t *data;
    size_t total_bits;
    size_t bit_pos;
} bitreader_t;

static inline bool br_has_bits(const bitreader_t *br, size_t nbits) {
    return (br->bit_pos + nbits) <= br->total_bits;
}

static uint32_t br_read_bits(bitreader_t *br, size_t nbits) {
    uint32_t val = 0;
    for (size_t i = 0; i < nbits; i++) {
        size_t byte_idx = br->bit_pos >> 3;
        size_t bit_idx = 7U - (br->bit_pos & 7U);
        uint32_t bit = (br->data[byte_idx] >> bit_idx) & 1U;
        val = (val << 1) | bit;
        br->bit_pos++;
    }
    return val;
}

static bool br_skip_bits(bitreader_t *br, size_t nbits) {
    if (!br_has_bits(br, nbits)) {
        return false;
    }
    br->bit_pos += nbits;
    return true;
}

/* ISO/IEC 14496-3 LatmGetValue() helper */
static uint32_t latm_get_value(bitreader_t *br, bool *ok) {
    if (!br_has_bits(br, 2)) {
        *ok = false;
        return 0;
    }
    uint32_t bytes_for_value = br_read_bits(br, 2);
    uint32_t value = 0;
    for (uint32_t i = 0; i <= bytes_for_value; i++) {
        if (!br_has_bits(br, 8)) {
            *ok = false;
            return 0;
        }
        value = (value << 8) | br_read_bits(br, 8);
    }
    return value;
}

/* Parse AudioSpecificConfig inside StreamMuxConfig (when ascLen == 0) */
static bool skip_audio_specific_config(bitreader_t *br) {
    if (!br_has_bits(br, 5)) {
        return false;
    }
    uint32_t audio_object_type = br_read_bits(br, 5);
    if (audio_object_type == 31U) {
        if (!br_has_bits(br, 6)) {
            return false;
        }
        audio_object_type = 32U + br_read_bits(br, 6);
    }

    if (!br_has_bits(br, 4)) {
        return false;
    }
    uint32_t freq_idx = br_read_bits(br, 4);
    if (freq_idx == 0x0FU) {
        if (!br_skip_bits(br, 24)) {
            return false;
        }
    }

    if (!br_has_bits(br, 4)) {
        return false;
    }
    (void)br_read_bits(br, 4); /* channelConfiguration */

    /* GASpecificConfig (for AAC-LC, audioObjectType == 2):
     * frameLengthFlag (1), dependsOnCoreCoder (1), extensionFlag (1)
     */
    if (audio_object_type == 1U || audio_object_type == 2U ||
        audio_object_type == 3U || audio_object_type == 4U ||
        audio_object_type == 6U || audio_object_type == 7U) {
        if (!br_has_bits(br, 3)) {
            return false;
        }
        (void)br_read_bits(br, 1); /* frameLengthFlag */
        uint32_t depends_on_core = br_read_bits(br, 1);
        if (depends_on_core) {
            if (!br_skip_bits(br, 14)) {
                return false;
            }
        }
        uint32_t ext_flag = br_read_bits(br, 1);
        (void)ext_flag;
    }
    return true;
}

void aac_fill_silent_frame(uint8_t out_frame[AAC_FIXED_FRAME_BYTES]) {
    memcpy(out_frame, kSilentAacLcStereo44k1, sizeof(kSilentAacLcStereo44k1));
    memset(out_frame + sizeof(kSilentAacLcStereo44k1), 0,
           AAC_FIXED_FRAME_BYTES - sizeof(kSilentAacLcStereo44k1));
}

bool aac_pad_raw_frame(const uint8_t *raw_aac, size_t raw_len,
                       uint8_t out_frame[AAC_FIXED_FRAME_BYTES]) {
    if (!raw_aac || raw_len == 0 || raw_len > AAC_FIXED_FRAME_BYTES) {
        return false;
    }
    memcpy(out_frame, raw_aac, raw_len);
    if (raw_len < AAC_FIXED_FRAME_BYTES) {
        memset(out_frame + raw_len, 0, AAC_FIXED_FRAME_BYTES - raw_len);
    }
    return true;
}

size_t aac_latm_extract_mux_to_fixed_frame(const uint8_t *latm_buf,
                                           size_t latm_len,
                                           uint8_t out_frame[AAC_FIXED_FRAME_BYTES]) {
    if (!latm_buf || latm_len < 2 || !out_frame) {
        return 0;
    }

    bitreader_t br = {
        .data = latm_buf,
        .total_bits = latm_len * 8U,
        .bit_pos = 0
    };

    /* AudioMuxElement(muxConfigPresent = 1) */
    uint32_t use_same_stream_mux = br_read_bits(&br, 1);
    if (!use_same_stream_mux) {
        /* StreamMuxConfig() */
        if (!br_has_bits(&br, 1)) {
            return 0;
        }
        uint32_t audio_mux_version = br_read_bits(&br, 1);
        uint32_t audio_mux_version_a = 0;
        if (audio_mux_version == 1U) {
            if (!br_has_bits(&br, 1)) {
                return 0;
            }
            audio_mux_version_a = br_read_bits(&br, 1);
        }
        if (audio_mux_version_a != 0U) {
            return 0;
        }

        bool ok = true;
        if (audio_mux_version == 1U) {
            (void)latm_get_value(&br, &ok); /* taraBufferFullness */
            if (!ok) {
                return 0;
            }
        }

        if (!br_has_bits(&br, 1 + 6 + 4 + 3)) {
            return 0;
        }
        uint32_t all_same_time_framing = br_read_bits(&br, 1);
        uint32_t num_sub_frames = br_read_bits(&br, 6);
        uint32_t num_program = br_read_bits(&br, 4);
        uint32_t num_layer = br_read_bits(&br, 3);

        if (!all_same_time_framing || num_sub_frames != 0U ||
            num_program != 0U || num_layer != 0U) {
            return 0;
        }

        if (audio_mux_version == 0U) {
            if (!skip_audio_specific_config(&br)) {
                return 0;
            }
        } else {
            uint32_t asc_len = latm_get_value(&br, &ok);
            if (!ok || !br_skip_bits(&br, asc_len)) {
                return 0;
            }
        }

        if (!br_has_bits(&br, 3)) {
            return 0;
        }
        uint32_t frame_length_type = br_read_bits(&br, 3);
        if (frame_length_type != 0U) {
            return 0;
        }
        /* latmBufferFullness (8 bits) */
        if (!br_skip_bits(&br, 8)) {
            return 0;
        }

        /* otherDataPresent (1 bit) */
        if (!br_has_bits(&br, 1)) {
            return 0;
        }
        uint32_t other_data_present = br_read_bits(&br, 1);
        if (other_data_present) {
            if (audio_mux_version == 1U) {
                uint32_t other_data_len = latm_get_value(&br, &ok);
                if (!ok || !br_skip_bits(&br, other_data_len)) {
                    return 0;
                }
            } else {
                uint32_t esc = 0;
                do {
                    if (!br_has_bits(&br, 9)) {
                        return 0;
                    }
                    esc = br_read_bits(&br, 1);
                    (void)br_read_bits(&br, 8);
                } while (esc);
            }
        }

        /* crcCheckPresent (1 bit) */
        if (!br_has_bits(&br, 1)) {
            return 0;
        }
        uint32_t crc_present = br_read_bits(&br, 1);
        if (crc_present) {
            if (!br_skip_bits(&br, 8)) {
                return 0;
            }
        }
    }

    /* PayloadLengthInfo() for frameLengthType == 0 */
    size_t payload_len = 0;
    uint32_t tmp = 0;
    do {
        if (!br_has_bits(&br, 8)) {
            return 0;
        }
        tmp = br_read_bits(&br, 8);
        payload_len += tmp;
        if (payload_len > AAC_FIXED_FRAME_BYTES) {
            return 0;
        }
    } while (tmp == 0xFFU);

    if (payload_len == 0 || !br_has_bits(&br, payload_len * 8U)) {
        return 0;
    }

    /* PayloadMux(): read payload_len bytes of raw_data_block() */
    for (size_t i = 0; i < payload_len; i++) {
        out_frame[i] = (uint8_t)br_read_bits(&br, 8);
    }
    if (payload_len < AAC_FIXED_FRAME_BYTES) {
        memset(out_frame + payload_len, 0, AAC_FIXED_FRAME_BYTES - payload_len);
    }
    return payload_len;
}

size_t aac_latm_extract_rtp_to_fixed_frame(const uint8_t *rtp_packet,
                                           size_t packet_len,
                                           uint8_t out_frame[AAC_FIXED_FRAME_BYTES]) {
    if (!rtp_packet || packet_len <= 12U || !out_frame) {
        return 0;
    }

    /* Validate RTP v2 header and compute header size including CSRCs & extension */
    uint8_t v_p_x_cc = rtp_packet[0];
    if ((v_p_x_cc >> 6) != 2U) {
        return 0;
    }
    size_t rtp_hdr_len = 12U + (size_t)(v_p_x_cc & 0x0FU) * 4U;
    if (packet_len <= rtp_hdr_len) {
        return 0;
    }

    /* Check RTP extension bit (X) */
    if (v_p_x_cc & 0x10U) {
        if (packet_len < rtp_hdr_len + 4U) {
            return 0;
        }
        uint16_t ext_words = ((uint16_t)rtp_packet[rtp_hdr_len + 2] << 8) |
                             rtp_packet[rtp_hdr_len + 3];
        rtp_hdr_len += 4U + (size_t)ext_words * 4U;
    }

    /* Check RTP padding bit (P) */
    size_t payload_end = packet_len;
    if (v_p_x_cc & 0x20U) {
        uint8_t pad_bytes = rtp_packet[packet_len - 1];
        if (pad_bytes == 0 || rtp_hdr_len + pad_bytes >= packet_len) {
            return 0;
        }
        payload_end -= pad_bytes;
    }

    if (payload_end <= rtp_hdr_len) {
        return 0;
    }

    return aac_latm_extract_mux_to_fixed_frame(
        rtp_packet + rtp_hdr_len,
        payload_end - rtp_hdr_len,
        out_frame
    );
}
