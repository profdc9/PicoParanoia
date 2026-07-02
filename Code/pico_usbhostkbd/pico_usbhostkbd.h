// pico_usbhostkbd — standalone RP2040 USB-host boot-keyboard driver.
//
// TinyUSB host mode + the HID class driver, restricted to a single boot-
// protocol USB keyboard. Decoupled from the application, matching pico_ntsc
// and pico_ps2kbd (PORTING.md §0.B): it knows only the USB HID wire protocol
// and hands back plain decoded bytes, nothing app-specific. Same two-function
// shape as pico_ps2kbd (init/getkey) so consoleio can drain both sources the
// same way.
//
// RP2040 has one native USB controller: host and device mode are mutually
// exclusive, so this cannot be compiled in alongside USB-CDC stdio
// (PICOPARANOIA_ENABLE_USB_STDIO) -- see PORTING.md §1.7. This library owns
// core1 entirely: tuh_init()/tuh_task() run in a dedicated core1 loop
// (PORTING.md §1.5's "core1 is free" -- this is its documented tenant), so
// pico_usbhostkbd_init() must be the only thing launching core1.
#ifndef PICO_USBHOSTKBD_H
#define PICO_USBHOSTKBD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pico_usbhostkbd_init(void);    // start the TinyUSB host stack on core1
int  pico_usbhostkbd_getkey(void);  // next decoded byte, or -1 if the FIFO is empty

// Keystroke-timing entropy (defense in depth) -- same treatment as
// pico_ps2kbd's accumulator (see pico_ps2kbd.h for the full rationale): raw,
// unwhitened, mixed once per received HID report, not to be trusted alone.
uint32_t pico_usbhostkbd_entropy_sample(void);
uint32_t pico_usbhostkbd_entropy_count(void);

// Bring-up diagnostics: counters at each stage of the connection, so a stall
// can be localized instead of guessed at. All monotonically increasing;
// read from core0, written from core1 -- single-word reads/writes are
// naturally coherent on RP2040 (no per-core cache), no locking needed for
// a "did this number change" check.
uint32_t pico_usbhostkbd_diag_core1_alive(void);  // tuh_task() loop iterations (>0 means core1 is running)
uint32_t pico_usbhostkbd_diag_dev_mounts(void);   // tuh_mount_cb count: ANY USB device enumerated (pre-HID)
uint32_t pico_usbhostkbd_diag_hid_mounts(void);   // tuh_hid_mount_cb count: a HID interface enumerated
uint32_t pico_usbhostkbd_diag_reports(void);      // tuh_hid_report_received_cb count: any HID report received

#ifdef __cplusplus
}
#endif

#endif // PICO_USBHOSTKBD_H
