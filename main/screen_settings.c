// Settings screen: WiFi connection, Tibber status (LED + text), and the button
// that opens WiFi setup.
//
// The widgets come from the SquareLine Vision export (main/GUI/); this file is
// hand-written so a re-export does not overwrite it. The Back button's screen
// change is a SquareLine event. The LED (lv_led) is created here, inside
// Status_Led_Container, because SquareLine Vision has no LED widget.

#include "screen_settings.h"
#include "tibber.h"
#include "wifi_manager.h"
#include "GUI.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define INFO_POLL_MS 1000
#define LED_SIZE     18   // inside the 28x28 container: dark ring around it

// Same colors as the price trend chevrons.
#define LED_GREEN  0x00cc66
#define LED_ORANGE 0xff9900
#define LED_RED    0xff3333

static lv_obj_t *s_led;

static void set_label(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void show_wifi_info(void)
{
    wifi_info_t info;
    wifi_manager_get_info(&info);

    char text[96];
    switch (wifi_manager_get_state()) {
    case WIFI_STATE_CONNECTED:
        snprintf(text, sizeof(text), "WiFi: %s\nIP: %s", info.sta_ssid, info.sta_ip);
        break;
    case WIFI_STATE_CONNECTING:
        snprintf(text, sizeof(text), "WiFi: %s\nVerbinden...", info.sta_ssid);
        break;
    default:
        snprintf(text, sizeof(text), "WiFi: niet ingesteld");
        break;
    }
    set_label(GUI_Label__Settings__WiFi_Info, text);
}

// Green = prices OK, orange = token missing/wrong, red = no data from Tibber.
// While fetching the LED keeps its previous color (off before the first result).
static void show_tibber_info(void)
{
    tibber_status_t st;
    tibber_get_status(&st);

    const char *status;
    uint32_t color = 0;
    switch (st.state) {
    case TIBBER_STATE_OK:
        status = "Verbonden";
        color = LED_GREEN;
        break;
    case TIBBER_STATE_NO_TOKEN:
        status = "Geen token";
        color = LED_ORANGE;
        break;
    case TIBBER_STATE_TOKEN_INVALID:
        status = "Token ongeldig";
        color = LED_ORANGE;
        break;
    case TIBBER_STATE_WAIT_WIFI:
        status = "Wacht op WiFi";
        color = LED_RED;
        break;
    case TIBBER_STATE_NO_CONNECTION:
        status = "Geen verbinding";
        color = LED_RED;
        break;
    case TIBBER_STATE_ERROR:
        status = "Fout van Tibber";
        color = LED_RED;
        break;
    default:
        status = "Ophalen...";
        break;
    }

    if (color != 0) {
        lv_led_set_color(s_led, lv_color_hex(color));
        lv_led_on(s_led);
    }

    // Prices keep working after a failed fetch; show how old they are.
    char text[64];
    if (st.last_update != 0) {
        struct tm t;
        localtime_r(&st.last_update, &t);
        snprintf(text, sizeof(text), "%s\nPrijzen van %02d:%02d", status, t.tm_hour, t.tm_min);
    } else {
        snprintf(text, sizeof(text), "%s", status);
    }
    set_label(GUI_Label__Settings__Tibber_Info, text);
}

static void show_info(void)
{
    show_wifi_info();
    show_tibber_info();
}

static void poll_cb(lv_timer_t *timer)
{
    if (lv_screen_active() == GUI_Screen__Settings) {
        show_info();
    }
}

static void screen_loading(lv_event_t *e)
{
    show_info();  // no stale text during the slide-in
}

static void reset_clicked(lv_event_t *e)
{
    wifi_manager_start_setup();  // screen_setup then shows the WiFi_Setup screen
}

void screen_settings_start(void)
{
    s_led = lv_led_create(GUI_Container__Settings__Status_Led_Container);
    lv_obj_set_size(s_led, LED_SIZE, LED_SIZE);
    lv_obj_center(s_led);
    lv_led_set_color(s_led, lv_color_hex(0x808080));  // dim gray until the first result
    lv_led_off(s_led);

    lv_obj_add_event_cb(GUI_Screen__Settings, screen_loading, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(GUI_Button__Settings__WiFi_Reset_Button, reset_clicked, LV_EVENT_CLICKED, NULL);
    lv_timer_create(poll_cb, INFO_POLL_MS, NULL);
}
