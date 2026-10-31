// Drives the Home screen clock labels from the system time.
//
// Uses the standard POSIX clock (time/localtime_r + TZ), so adding SNTP later
// only means starting SNTP: it sets the system time and this code follows.
// Until then the clock starts at 00:00 on every boot.
//
// The labels come from the SquareLine Vision export (main/GUI/); this file is
// hand-written so a re-export does not overwrite it.

#include "home_clock.h"
#include "GUI.h"

#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

// Europe/Amsterdam: CET (UTC+1), CEST (UTC+2) from last Sunday of March 02:00
// until last Sunday of October 03:00.
#define CLOCK_TZ         "CET-1CEST,M3.5.0,M10.5.0/3"
#define CLOCK_TICK_MS    100   // short, so the colon blink follows the real second
#define CLOCK_SYNCED_YEAR 2024 // system time before this year = never set

static int s_last_min = -1;
static bool s_colon_on = true;

static void clock_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm now;
    localtime_r(&tv.tv_sec, &now);

    if (now.tm_min != s_last_min) {
        s_last_min = now.tm_min;
        lv_label_set_text_fmt(GUI_Label__Home__Hours, "%02d", now.tm_hour);
        lv_label_set_text_fmt(GUI_Label__Home__Minutes, "%02d", now.tm_min);
    }

    // Blink via text opacity, not LV_OBJ_FLAG_HIDDEN: hiding removes the colon
    // from the flex row, so hours and minutes would jump every second.
    bool colon_on = tv.tv_usec < 500000;
    if (colon_on != s_colon_on) {
        s_colon_on = colon_on;
        lv_obj_set_style_text_opa(GUI_Label__Home__Divider,
                                  colon_on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void home_clock_start(void)
{
    setenv("TZ", CLOCK_TZ, 1);
    tzset();

    // No time source yet (no NTP): start at 2026-01-01 00:00 local time.
    time_t t = time(NULL);
    struct tm now;
    localtime_r(&t, &now);
    if (now.tm_year + 1900 < CLOCK_SYNCED_YEAR) {
        struct tm boot = {0};
        boot.tm_year = 2026 - 1900;
        boot.tm_mon = 0;
        boot.tm_mday = 1;
        boot.tm_isdst = -1;
        struct timeval tv = { .tv_sec = mktime(&boot), .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }

    clock_tick_cb(NULL);  // replace the designer placeholder "23:59" right away
    lv_timer_create(clock_tick_cb, CLOCK_TICK_MS, NULL);
}
