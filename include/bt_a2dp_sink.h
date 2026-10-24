#ifndef PICO_BT_USB_M4A_BT_A2DP_SINK_H
#define PICO_BT_USB_M4A_BT_A2DP_SINK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize CYW43439 Bluetooth Classic stack, SDP records, A2DP Sink
 * (44.1 kHz Stereo AAC-LC), AVRCP, auto-reconnect timer, and status LED.
 *
 * Returns 0 on success.
 */
int bt_a2dp_sink_init(void);

/**
 * Returns true if a remote Bluetooth A2DP source is currently connected.
 */
bool bt_a2dp_is_connected(void);

/**
 * Returns true if A2DP audio stream is actively playing.
 */
bool bt_a2dp_is_streaming(void);

#ifdef __cplusplus
}
#endif

#endif /* PICO_BT_USB_M4A_BT_A2DP_SINK_H */
