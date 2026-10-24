#ifndef PICO_BT_USB_M4A_TUSB_CONFIG_H
#define PICO_BT_USB_M4A_TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU                OPT_MCU_RP2040
#endif

#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

/* Device stack configuration */
#define CFG_TUD_ENDPOINT0_SIZE      64

/* Enabled USB device classes: Mass Storage Class (MSC) only */
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 1
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* MSC Endpoint buffer size (4096 bytes = 1 FAT32 cluster) */
#define CFG_TUD_MSC_EP_BUFSIZE      4096

#ifdef __cplusplus
}
#endif

#endif /* PICO_BT_USB_M4A_TUSB_CONFIG_H */
