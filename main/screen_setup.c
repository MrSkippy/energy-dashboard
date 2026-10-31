// WiFi_Setup screen: QR code for the setup hotspot and the connection status.
// Switches to this screen when setup mode opens and back to Home when it
// closes. Holding the knob button for 5 s opens setup mode.
//
// The widgets come from the SquareLine Vision export (main/GUI/); this file is
// hand-written so a re-export does not overwrite it. wifi_manager never calls
// LVGL, so its state is polled here.

#include "screen_setup.h"
#include "knob.h"
#include "settings_store.h"
#include "wifi_manager.h"
#include "GUI.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define TAG "screen_setup"

#define POLL_MS        200
#define KNOB_HOLD_MS   5000
#define SCREEN_FADE_MS 300

static lv_obj_t *s_qr;
static bool s_in_setup;
static bool s_has_wifi;  // a saved network to go back to: Cancel is useful
static wifi_state_t s_shown_state;
static wifi_fail_reason_t s_shown_reason;

static bool s_button_down;
static bool s_button_fired;
static uint32_t s_button_down_at;

static bool is_setup(wifi_state_t state)
{
    return state == WIFI_STATE_SETUP || state == WIFI_STATE_SETUP_TESTING ||
           state == WIFI_STATE_SETUP_FAILED || state == WIFI_STATE_SETUP_DONE;
}

static void load_screen(lv_obj_t *screen)
{
    if (lv_screen_active() != screen) {
        lv_screen_load_anim(screen, LV_SCREEN_LOAD_ANIM_FADE_IN, SCREEN_FADE_MS, 0, false);
    }
}

static void show_qr(const wifi_info_t *info)
{
    // Standard WiFi QR format: phone cameras offer to join the network.
    char data[96];
    snprintf(data, sizeof(data), "WIFI:S:%s;T:WPA;P:%s;;", info->ap_ssid, info->ap_pass);
    if (lv_qrcode_update(s_qr, data, strlen(data)) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "QR code does not fit in QR_Container");
    }
}

static void show_status(wifi_state_t state, wifi_fail_reason_t reason)
{
    const char *text;
    bool failed = false;
    switch (state) {
    case WIFI_STATE_SETUP_TESTING:
        text = "Verbinden...";
        break;
    case WIFI_STATE_SETUP_DONE:
        text = "Verbonden!";
        break;
    case WIFI_STATE_SETUP_FAILED:
        failed = true;
        text = reason == WIFI_FAIL_WRONG_PASSWORD ? "Wachtwoord onjuist"
             : reason == WIFI_FAIL_NOT_FOUND      ? "Netwerk niet gevonden"
                                                  : "Verbinden mislukt";
        break;
    default:
        text = "Wachten...";
        break;
    }
    lv_label_set_text_static(GUI_Label__WiFi_Setup__Status, text);

    // Failures in red; otherwise the color from SquareLine.
    if (failed) {
        lv_obj_set_style_text_color(GUI_Label__WiFi_Setup__Status, lv_color_hex(0xff3333), 0);
    } else {
        lv_obj_remove_local_style_prop(GUI_Label__WiFi_Setup__Status, LV_STYLE_TEXT_COLOR, 0);
    }

    // Cancel only works while waiting for the phone, and needs a saved network.
    if (s_has_wifi && (state == WIFI_STATE_SETUP || state == WIFI_STATE_SETUP_FAILED)) {
        lv_obj_remove_flag(GUI_Button__WiFi_Setup__Cancel_Button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(GUI_Button__WiFi_Setup__Cancel_Button, LV_OBJ_FLAG_HIDDEN);
    }
}

static void knob_hold_check(void)
{
    if (!knob_button_pressed()) {
        s_button_down = false;
        return;
    }
    if (!s_button_down) {
        s_button_down = true;
        s_button_fired = false;
        s_button_down_at = lv_tick_get();
    } else if (!s_button_fired && lv_tick_elaps(s_button_down_at) >= KNOB_HOLD_MS) {
        s_button_fired = true;  // once per press
        ESP_LOGI(TAG, "Knob held %d s, opening WiFi setup", KNOB_HOLD_MS / 1000);
        wifi_manager_start_setup();
    }
}

static void poll_cb(lv_timer_t *timer)
{
    knob_hold_check();

    wifi_state_t state = wifi_manager_get_state();
    if (!is_setup(state)) {
        if (s_in_setup) {
            s_in_setup = false;
            load_screen(GUI_Screen__Home);
        }
        return;
    }

    wifi_info_t info;
    wifi_manager_get_info(&info);
    if (!s_in_setup) {
        // New setup session: new hotspot password.
        s_in_setup = true;
        s_has_wifi = settings_has_wifi();
        s_shown_state = WIFI_STATE_IDLE;  // force a status update
        show_qr(&info);
        load_screen(GUI_Screen__WiFi_Setup);
    }
    if (state != s_shown_state || info.fail_reason != s_shown_reason) {
        s_shown_state = state;
        s_shown_reason = info.fail_reason;
        show_status(state, info.fail_reason);
    }
}

static void cancel_clicked(lv_event_t *e)
{
    wifi_manager_cancel_setup();  // poll_cb then returns to Home
}

void screen_setup_start(void)
{
    lv_obj_t *container = GUI_Container__WiFi_Setup__QR_Container;
    lv_obj_update_layout(container);

    s_qr = lv_qrcode_create(container);
    lv_qrcode_set_size(s_qr, LV_MIN(lv_obj_get_content_width(container), lv_obj_get_content_height(container)));
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_qrcode_set_quiet_zone(s_qr, true);  // white border: needed by phone cameras
    lv_obj_center(s_qr);

    lv_obj_add_flag(GUI_Button__WiFi_Setup__Cancel_Button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(GUI_Button__WiFi_Setup__Cancel_Button, cancel_clicked, LV_EVENT_CLICKED, NULL);

    lv_timer_create(poll_cb, POLL_MS, NULL);
}
