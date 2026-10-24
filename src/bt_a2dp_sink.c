#include "bt_a2dp_sink.h"
#include "config.h"
#include "aac_latm.h"
#include "stream_ringbuf.h"

#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <string.h>

static uint8_t g_sdp_a2dp_sink_service_buffer[150];
static uint8_t g_sdp_avrcp_target_service_buffer[150];
static uint8_t g_sdp_avrcp_controller_service_buffer[150];
static uint8_t g_device_id_sdp_service_buffer[100];

static btstack_packet_callback_registration_t g_hci_event_callback_registration;
static btstack_timer_source_t g_status_timer;

static uint16_t g_a2dp_cid = 0;
static uint8_t  g_local_seid = 0;
static bool     g_connected = false;
static bool     g_streaming = false;
static bool     g_led_state = false;
static uint32_t g_reconnect_ticks = 0;
static bd_addr_t g_last_bonded_addr;
static bool      g_has_last_bonded = false;

/*
 * Bluetooth A2DP v1.3 Section 4.5.2: MPEG-2,4 AAC Codec Specific Information Elements (6 bytes)
 * - Octet 0: MPEG-2 AAC LC (0x80) | MPEG-4 AAC LC (0x40)
 * - Octet 1: 44.1 kHz only (bit 0 = 0x01)
 * - Octet 2: 48..96 kHz disabled (0x00) | Channels 2 Stereo only (bit 2 = 0x04)
 * - Octet 3: CBR/VBR (0x80) | Bitrate[22:16]
 * - Octet 4: Bitrate[15:8]
 * - Octet 5: Bitrate[7:0]
 */
static uint8_t g_media_aac_codec_capabilities[6] = {
    0xC0U,
    0x01U, /* 44100 Hz */
    0x04U, /* Stereo (2 channels) */
    (uint8_t)(0x80U | ((AAC_MAX_A2DP_BITRATE >> 16) & 0x7FU)),
    (uint8_t)((AAC_MAX_A2DP_BITRATE >> 8) & 0xFFU),
    (uint8_t)(AAC_MAX_A2DP_BITRATE & 0xFFU)
};

static uint8_t g_media_aac_codec_configuration[6];

bool bt_a2dp_is_connected(void) {
    return g_connected;
}

bool bt_a2dp_is_streaming(void) {
    return g_streaming;
}

static void find_last_bonded_device(void) {
    bd_addr_t addr;
    link_key_t link_key;
    link_key_type_t type;
    btstack_link_key_iterator_t it;

    if (!hci_link_key_iterator_init(&it)) {
        return;
    }
    if (hci_link_key_iterator_get_next(&it, addr, link_key, &type)) {
        memcpy(g_last_bonded_addr, addr, sizeof(bd_addr_t));
        g_has_last_bonded = true;
    }
    hci_link_key_iterator_done(&it);
}

static void status_timer_handler(btstack_timer_source_t *ts) {
    if (g_streaming) {
        /* Solid ON when actively streaming audio */
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else if (g_connected) {
        /* Slow pulse when connected & paused */
        g_led_state = !g_led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, g_led_state ? 1 : 0);
    } else {
        /* Fast blink when discoverable / waiting for phone */
        g_led_state = !g_led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, g_led_state ? 1 : 0);

        g_reconnect_ticks++;
        if ((g_reconnect_ticks * 500U) >= BT_RECONNECT_INTERVAL_MS) {
            g_reconnect_ticks = 0;
            if (!g_has_last_bonded) {
                find_last_bonded_device();
            }
            if (g_has_last_bonded) {
                (void)a2dp_sink_establish_stream(g_last_bonded_addr, &g_a2dp_cid);
            }
        }
    }

    btstack_run_loop_set_timer(ts, 500);
    btstack_run_loop_add_timer(ts);
}

static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size) {
    (void)seid;
    uint8_t fixed_frame[AAC_FIXED_FRAME_BYTES];

    size_t raw_len = aac_latm_extract_rtp_to_fixed_frame(packet, size, fixed_frame);
    if (raw_len > 0) {
        (void)stream_ringbuf_push_frame(fixed_frame);
    }
}

static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel,
                                     uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) {
        return;
    }

    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED: {
            uint8_t status = a2dp_subevent_signaling_connection_established_get_status(packet);
            if (status == ERROR_CODE_SUCCESS) {
                g_a2dp_cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
                a2dp_subevent_signaling_connection_established_get_bd_addr(packet, g_last_bonded_addr);
                g_has_last_bonded = true;
                g_connected = true;
                gap_discoverable_control(0);
            }
            break;
        }
        case A2DP_SUBEVENT_STREAM_ESTABLISHED: {
            uint8_t status = a2dp_subevent_stream_established_get_status(packet);
            if (status == ERROR_CODE_SUCCESS) {
                g_connected = true;
            }
            break;
        }
        case A2DP_SUBEVENT_STREAM_STARTED:
            g_streaming = true;
            break;
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            g_streaming = false;
            break;
        case A2DP_SUBEVENT_STREAM_RELEASED:
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            g_streaming = false;
            g_connected = false;
            g_a2dp_cid = 0;
            gap_discoverable_control(1);
            gap_connectable_control(1);
            break;
        default:
            break;
    }
}

static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel,
                                 uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) {
        return;
    }
    if (packet[2] == AVRCP_SUBEVENT_CONNECTION_ESTABLISHED) {
        uint16_t avrcp_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
        avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
    }
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    if (hci_event_packet_get_type(packet) == BTSTACK_EVENT_STATE) {
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            find_last_bonded_device();
            gap_discoverable_control(1);
            gap_connectable_control(1);
            if (g_has_last_bonded) {
                (void)a2dp_sink_establish_stream(g_last_bonded_addr, &g_a2dp_cid);
            }
        }
    }
}

int bt_a2dp_sink_init(void) {
    if (cyw43_arch_init()) {
        return -1;
    }

    l2cap_init();
    sdp_init();

    /* Initialize AVDTP + A2DP Sink */
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&a2dp_sink_packet_handler);
    a2dp_sink_register_media_handler(&a2dp_sink_media_packet_handler);

    /* Create 44.1 kHz Stereo AAC-LC Stream Endpoint */
    avdtp_stream_endpoint_t *local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_MPEG_2_4_AAC,
        g_media_aac_codec_capabilities,
        sizeof(g_media_aac_codec_capabilities),
        g_media_aac_codec_configuration,
        sizeof(g_media_aac_codec_configuration)
    );
    if (!local_stream_endpoint) {
        return -2;
    }
    g_local_seid = avdtp_local_seid(local_stream_endpoint);

    /* Initialize AVRCP Controller & Target */
    avrcp_init();
    avrcp_register_packet_handler(&avrcp_packet_handler);
    avrcp_controller_init();
    avrcp_target_init();

    /* Register SDP records */
    memset(g_sdp_a2dp_sink_service_buffer, 0, sizeof(g_sdp_a2dp_sink_service_buffer));
    a2dp_sink_create_sdp_record(g_sdp_a2dp_sink_service_buffer,
                                sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE,
                                NULL, NULL);
    sdp_register_service(g_sdp_a2dp_sink_service_buffer);

    memset(g_sdp_avrcp_target_service_buffer, 0, sizeof(g_sdp_avrcp_target_service_buffer));
    avrcp_target_create_sdp_record(g_sdp_avrcp_target_service_buffer,
                                   sdp_create_service_record_handle(),
                                   AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER,
                                   NULL, NULL);
    sdp_register_service(g_sdp_avrcp_target_service_buffer);

    memset(g_sdp_avrcp_controller_service_buffer, 0, sizeof(g_sdp_avrcp_controller_service_buffer));
    avrcp_controller_create_sdp_record(g_sdp_avrcp_controller_service_buffer,
                                       sdp_create_service_record_handle(),
                                       AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER,
                                       NULL, NULL);
    sdp_register_service(g_sdp_avrcp_controller_service_buffer);

    memset(g_device_id_sdp_service_buffer, 0, sizeof(g_device_id_sdp_service_buffer));
    device_id_create_sdp_record(g_device_id_sdp_service_buffer,
                                sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                BLUETOOTH_COMPANY_ID_RASPBERRY_PI_FOUNDATION,
                                1, 1);
    sdp_register_service(g_device_id_sdp_service_buffer);

    /* Configure GAP */
    gap_set_local_name(BT_DEVICE_NAME);
    gap_discoverable_control(1);
    gap_set_class_of_device(0x240414U); /* Audio/Video: Loudspeaker / Car Audio */
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    g_hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&g_hci_event_callback_registration);

    /* Start status LED + auto-reconnect timer */
    g_status_timer.process = &status_timer_handler;
    btstack_run_loop_set_timer(&g_status_timer, 500);
    btstack_run_loop_add_timer(&g_status_timer);

    hci_power_control(HCI_POWER_ON);
    return 0;
}
