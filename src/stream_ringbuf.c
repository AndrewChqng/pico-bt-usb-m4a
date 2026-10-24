#include "stream_ringbuf.h"
#include "aac_latm.h"
#include <string.h>

#define PAIR_CACHE_SLOTS 4U
#define PAIR_BYTES       (AAC_FIXED_FRAME_BYTES * 2U) /* 1536 bytes = 3 sectors */

typedef struct {
    bool valid;
    uint32_t pair_idx;
    uint8_t data[PAIR_BYTES];
} pair_cache_entry_t;

static uint8_t g_ring_frames[AAC_RINGBUF_CAPACITY_FRAMES][AAC_FIXED_FRAME_BYTES];
static volatile uint32_t g_write_idx = 0;
static volatile uint32_t g_read_idx = 0;
static pair_cache_entry_t g_pair_cache[PAIR_CACHE_SLOTS];

void stream_ringbuf_init(void) {
    __atomic_store_n(&g_write_idx, 0U, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_read_idx, 0U, __ATOMIC_SEQ_CST);
    memset(g_pair_cache, 0, sizeof(g_pair_cache));
}

uint32_t stream_ringbuf_available_frames(void) {
    uint32_t w = __atomic_load_n(&g_write_idx, __ATOMIC_ACQUIRE);
    uint32_t r = __atomic_load_n(&g_read_idx, __ATOMIC_ACQUIRE);
    return w - r;
}

bool stream_ringbuf_push_frame(const uint8_t frame[AAC_FIXED_FRAME_BYTES]) {
    uint32_t w = __atomic_load_n(&g_write_idx, __ATOMIC_RELAXED);
    uint32_t r = __atomic_load_n(&g_read_idx, __ATOMIC_ACQUIRE);
    if ((w - r) >= AAC_RINGBUF_CAPACITY_FRAMES) {
        return false;
    }
    uint32_t slot = w % AAC_RINGBUF_CAPACITY_FRAMES;
    memcpy(g_ring_frames[slot], frame, AAC_FIXED_FRAME_BYTES);
    __atomic_store_n(&g_write_idx, w + 1U, __ATOMIC_RELEASE);
    return true;
}

bool stream_ringbuf_pop_frame(uint8_t out_frame[AAC_FIXED_FRAME_BYTES]) {
    uint32_t r = __atomic_load_n(&g_read_idx, __ATOMIC_RELAXED);
    uint32_t w = __atomic_load_n(&g_write_idx, __ATOMIC_ACQUIRE);
    if (r == w) {
        return false;
    }
    uint32_t slot = r % AAC_RINGBUF_CAPACITY_FRAMES;
    memcpy(out_frame, g_ring_frames[slot], AAC_FIXED_FRAME_BYTES);
    __atomic_store_n(&g_read_idx, r + 1U, __ATOMIC_RELEASE);
    return true;
}

static void pop_or_silence(uint8_t out_frame[AAC_FIXED_FRAME_BYTES]) {
    if (!stream_ringbuf_pop_frame(out_frame)) {
        aac_fill_silent_frame(out_frame);
    }
}

void stream_ringbuf_read_mdat_sector(uint32_t mdat_sector_idx,
                                     uint8_t out_sector[DISK_SECTOR_BYTES]) {
    uint32_t pair_idx = mdat_sector_idx / 3U;
    uint32_t phase = mdat_sector_idx % 3U;
    uint32_t cache_slot = pair_idx % PAIR_CACHE_SLOTS;

    pair_cache_entry_t *entry = &g_pair_cache[cache_slot];
    if (!entry->valid || entry->pair_idx != pair_idx) {
        entry->valid = true;
        entry->pair_idx = pair_idx;
        pop_or_silence(entry->data);
        pop_or_silence(entry->data + AAC_FIXED_FRAME_BYTES);
    }

    memcpy(out_sector, entry->data + (phase * DISK_SECTOR_BYTES), DISK_SECTOR_BYTES);
}
