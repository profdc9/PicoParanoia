// TinyUSB config for pico_usbhostkbd — host mode, HID class only.
//
// Deliberately minimal (PORTING.md §0.A: keep the TCB small): a single boot-
// protocol USB keyboard on the root port, no hub, no mass storage, no CDC,
// no vendor class. Everything below that isn't set here already defaults to
// off in TinyUSB's own tusb_option.h -- listed explicitly anyway so the scope
// is visible here rather than requiring a reader to trace defaults.
//
// CFG_TUSB_MCU / CFG_TUSB_OS / CFG_TUSB_DEBUG come from the Pico SDK's
// tinyusb_common_base target (linked transitively via tinyusb_host), not
// from this file.
#ifndef PICO_USBHOSTKBD_TUSB_CONFIG_H
#define PICO_USBHOSTKBD_TUSB_CONFIG_H

#define CFG_TUH_ENABLED             1

// RP2040 has one native USB controller (root hub port 0), used here in host
// role. No pio-usb / max3421 second controller.
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST)

#define CFG_TUH_DEVICE_MAX          1     // exactly one keyboard, no hub
#define CFG_TUH_HUB                 0
#define CFG_TUH_CDC                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// A composite keyboard can expose more than one HID interface (e.g. boot
// keyboard + a media-key/consumer-control interface); allow a few.
#define CFG_TUH_HID                 4
#define CFG_TUH_HID_EP_BUFSIZE      64

#endif
