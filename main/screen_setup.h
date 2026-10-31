// WiFi_Setup screen: QR code + status while setup mode is open; knob long-press.

#ifndef SCREEN_SETUP_H
#define SCREEN_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

// Call once after GUI_init() and knob_init(). Switches to the WiFi_Setup screen
// by itself whenever wifi_manager enters setup mode.
void screen_setup_start(void);

#ifdef __cplusplus
}
#endif

#endif
