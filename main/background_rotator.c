// Rotates the Home screen background images with a fade: automatically every
// BG_SWITCH_PERIOD_MS, and by hand with the rotary knob (see knob.c).
//
// The image widgets come from the SquareLine Vision export (main/GUI/); this file
// is hand-written so a re-export does not overwrite it. When a background is
// added or renamed in SquareLine, update the list in background_rotator_start().

#include "background_rotator.h"
#include "knob.h"
#include "GUI.h"

#define BG_SWITCH_PERIOD_MS 60000
#define BG_FADE_MS          1000
#define BG_KNOB_FADE_MS     300
#define BG_KNOB_POLL_MS     30

enum { BG_COUNT = 4 };

static lv_obj_t *s_bgs[BG_COUNT];
static uint32_t s_cur = 0;
static uint32_t s_next = 0;   // == s_cur when no fade is running
static lv_timer_t *s_auto_timer = NULL;

// Fade via image_opa, not opa: opa < 255 makes LVGL render the full-screen
// image into an extra layer every frame.
static void fade_exec_cb(void *var, int32_t v)
{
    lv_obj_set_style_image_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

// Jump to the end of a running fade: new image fully opaque, old one hidden so
// it is no longer drawn underneath.
static void finish_fade(void)
{
    if (s_next == s_cur) {
        return;
    }
    lv_anim_delete(s_bgs[s_next], fade_exec_cb);
    lv_obj_set_style_image_opa(s_bgs[s_next], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_bgs[s_cur], LV_OBJ_FLAG_HIDDEN);
    s_cur = s_next;
}

static void fade_completed_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    finish_fade();
}

static void show(uint32_t idx, uint32_t fade_ms)
{
    // Turning the knob fast must never leave two half-faded images.
    finish_fade();
    if (idx == s_cur) {
        return;
    }

    lv_obj_t *cur = s_bgs[s_cur];
    lv_obj_t *next = s_bgs[idx];
    s_next = idx;

    // Directly above the current background, still below the price box, clock
    // and settings button (they come later in the screen's child list).
    lv_obj_move_to_index(next, lv_obj_get_index(cur));

    lv_obj_set_style_image_opa(next, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(next, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, next);
    lv_anim_set_exec_cb(&a, fade_exec_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, fade_ms);
    lv_anim_set_completed_cb(&a, fade_completed_cb);
    lv_anim_start(&a);
}

static void switch_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    show((s_cur + 1) % BG_COUNT, BG_FADE_MS);
}

static void knob_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    int32_t steps = knob_take_steps();
    if (steps == 0 || lv_screen_active() != GUI_Screen__Home) {
        return;
    }

    // Count from the image being faded in, so quick turns add up.
    int32_t from = (int32_t)s_next;
    int32_t idx = (from + steps % BG_COUNT + BG_COUNT) % BG_COUNT;
    show((uint32_t)idx, BG_KNOB_FADE_MS);

    // Don't auto-switch right after the user picked an image.
    lv_timer_reset(s_auto_timer);
}

void background_rotator_start(void)
{
    // Rotation order.
    s_bgs[0] = GUI_Image__Home__Eclipse;
    s_bgs[1] = GUI_Image__Home__Aurora;
    s_bgs[2] = GUI_Image__Home__Bol;
    s_bgs[3] = GUI_Image__Home__Corsica;

    s_cur = 0;
    s_next = 0;
    lv_obj_remove_flag(s_bgs[0], LV_OBJ_FLAG_HIDDEN);
    for (uint32_t i = 1; i < BG_COUNT; i++) {
        lv_obj_add_flag(s_bgs[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_auto_timer = lv_timer_create(switch_cb, BG_SWITCH_PERIOD_MS, NULL);
    lv_timer_create(knob_poll_cb, BG_KNOB_POLL_MS, NULL);
}
