#ifndef PICO_BT_USB_M4A_BTSTACK_CONFIG_H
#define PICO_BT_USB_M4A_BTSTACK_CONFIG_H

/* Port related features */
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define ENABLE_PRINTF_HEXDUMP

/* Bluetooth Classic features */
#define ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_GOEP_L2CAP

/* BTstack buffer & connection configuration */
#define HCI_OUTGOING_PRE_BUFFER_SIZE                4
#define HCI_ACL_PAYLOAD_SIZE                        (1021 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT                4

#define MAX_NR_AVDTP_CONNECTIONS                    2
#define MAX_NR_AVDTP_STREAM_ENDPOINTS               2
#define MAX_NR_AVRCP_CONNECTIONS                    2
#define MAX_NR_BNEP_CHANNELS                        0
#define MAX_NR_BNEP_SERVICES                        0
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES   4
#define MAX_NR_GATT_CLIENTS                         0
#define MAX_NR_HCI_CONNECTIONS                      2
#define MAX_NR_HFP_CONNECTIONS                      0
#define MAX_NR_L2CAP_CHANNELS                       8
#define MAX_NR_L2CAP_SERVICES                       5
#define MAX_NR_RFCOMM_CHANNELS                      0
#define MAX_NR_RFCOMM_MULTIPLEXERS                  0
#define MAX_NR_RFCOMM_SERVICES                      0
#define MAX_NR_SERVICE_RECORD_ITEMS                 6
#define MAX_NR_SM_LOOKUP_ENTRIES                    3
#define MAX_NR_WHITELIST_ENTRIES                    1

#define MAX_NUM_LINK_KEYS                           8

/* Persistent TLV storage in flash for Bluetooth bonding keys */
#define NVM_NUM_DEVICE_DB_ENTRIES                   8
#define NVM_NUM_LINK_KEYS                           8

#endif /* PICO_BT_USB_M4A_BTSTACK_CONFIG_H */
