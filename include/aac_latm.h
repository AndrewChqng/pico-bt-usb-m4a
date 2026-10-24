#ifndef PICO_BT_USB_M4A_AAC_LATM_H
#define PICO_BT_USB_M4A_AAC_LATM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill a 768-byte buffer with a valid 44.1 kHz Stereo AAC-LC silent raw_data_block
 * padded to AAC_FIXED_FRAME_BYTES.
 */
void aac_fill_silent_frame(uint8_t out_frame[AAC_FIXED_FRAME_BYTES]);

/**
 * Pad a raw AAC-LC frame (raw_data_block) of length `raw_len` up to
 * AAC_FIXED_FRAME_BYTES (768 bytes). Returns false if raw_len == 0 or
 * raw_len > AAC_FIXED_FRAME_BYTES.
 */
bool aac_pad_raw_frame(const uint8_t *raw_aac, size_t raw_len,
                       uint8_t out_frame[AAC_FIXED_FRAME_BYTES]);

/**
 * Extract the raw AAC-LC frame (raw_data_block) from an A2DP RTP + RFC 3016
 * MPEG-4 LATM packet (muxConfigPresent = 1) and pad it to AAC_FIXED_FRAME_BYTES.
 *
 * @param rtp_packet    Pointer to complete RTP media packet from BTstack A2DP sink
 * @param packet_len    Total length of RTP packet in bytes
 * @param out_frame     Destination buffer of size AAC_FIXED_FRAME_BYTES (768 bytes)
 * @return              Number of raw AAC bytes extracted before padding, or 0 on error
 */
size_t aac_latm_extract_rtp_to_fixed_frame(const uint8_t *rtp_packet,
                                           size_t packet_len,
                                           uint8_t out_frame[AAC_FIXED_FRAME_BYTES]);

/**
 * Extract the raw AAC-LC frame from an RFC 3016 LATM AudioMuxElement payload
 * (after RTP header has already been stripped) and pad it to AAC_FIXED_FRAME_BYTES.
 */
size_t aac_latm_extract_mux_to_fixed_frame(const uint8_t *latm_buf,
                                           size_t latm_len,
                                           uint8_t out_frame[AAC_FIXED_FRAME_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* PICO_BT_USB_M4A_AAC_LATM_H */
