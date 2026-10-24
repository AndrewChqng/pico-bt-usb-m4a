#include "config.h"
#include "bt_a2dp_sink.h"
#include "stream_ringbuf.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/cyw43_arch.h"
#include "btstack_run_loop.h"
#include "tusb.h"

static void core1_usb_entry(void) {
    /* Allow Core 0 (BTstack TLV bonding storage) to safely write flash when pairing */
    flash_safe_execute_core_init();

    tusb_init();

    while (true) {
        tud_task();
    }
}

int main(void) {
    stdio_init_all();
    stream_ringbuf_init();

    /* Launch USB Mass Storage device stack on Core 1 */
    multicore_launch_core1(core1_usb_entry);

    /* Initialize Bluetooth Classic A2DP Sink (44.1 kHz Stereo AAC-LC) on Core 0 */
    if (bt_a2dp_sink_init() != 0) {
        while (true) {
            tight_loop_contents();
        }
    }

    btstack_run_loop_execute();
    return 0;
}
