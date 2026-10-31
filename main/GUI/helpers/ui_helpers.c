
#include "ui_helpers.h"

void _ui_bar_set_property(lv_obj_t *target, int id, int val) {
    if (id == _UI_BAR_PROPERTY_VALUE_WITH_ANIM)
        lv_bar_set_value(target, val, LV_ANIM_ON);
    if (id == _UI_BAR_PROPERTY_VALUE)
        lv_bar_set_value(target, val, LV_ANIM_OFF);
}

void _ui_basic_set_property(lv_obj_t *target, int id, int val) {
    if (id == _UI_BASIC_PROPERTY_POSITION_X)
        lv_obj_set_x(target, val);
    if (id == _UI_BASIC_PROPERTY_POSITION_Y)
        lv_obj_set_y(target, val);
    if (id == _UI_BASIC_PROPERTY_WIDTH)
        lv_obj_set_width(target, val);
    if (id == _UI_BASIC_PROPERTY_HEIGHT)
        lv_obj_set_height(target, val);
}

void _ui_dropdown_set_property(lv_obj_t *target, int id, int val) {
    if (id == _UI_DROPDOWN_PROPERTY_SELECTED)
        lv_dropdown_set_selected(target, val);
}

void _ui_image_set_property(lv_obj_t *target, int id, uint8_t *val) {
    if (id == _UI_IMAGE_PROPERTY_IMAGE)
        lv_image_set_src(target, val);
}

void _ui_label_set_property(lv_obj_t *target, int id, const char *val) {
    if (id == _UI_LABEL_PROPERTY_TEXT)
        lv_label_set_text(target, val);
}

void _ui_roller_set_property(lv_obj_t *target, int id, int val) {
    if (id == _UI_ROLLER_PROPERTY_SELECTED_WITH_ANIM)
        lv_roller_set_selected(target, val, LV_ANIM_ON);
    if (id == _UI_ROLLER_PROPERTY_SELECTED)
        lv_roller_set_selected(target, val, LV_ANIM_OFF);
}

void _ui_slider_set_property(lv_obj_t *target, int id, int val) {
    if (id == _UI_SLIDER_PROPERTY_VALUE_WITH_ANIM)
        lv_slider_set_value(target, val, LV_ANIM_ON);
    if (id == _UI_SLIDER_PROPERTY_VALUE)
        lv_slider_set_value(target, val, LV_ANIM_OFF);
}

void _ui_screen_change(lv_obj_t **target, lv_screen_load_anim_t fademode, int spd, int delay,
                       void (*target_init)(void)) {
    if (*target == NULL)
        target_init();

    lv_obj_t *act = lv_screen_active();
    bool auto_del = act != NULL && lv_obj_has_flag(act, LV_OBJ_FLAG_USER_1);
    lv_screen_load_anim(*target, fademode, spd, delay, auto_del);
}

void _ui_screen_delete(lv_obj_t **target) {
    if (*target != NULL) {
        lv_obj_delete(*target);
        *target = NULL;
    }
}

void _ui_arc_increment(lv_obj_t *target, int val) {
    int old = lv_arc_get_value(target);
    lv_arc_set_value(target, old + val);
    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
}

void _ui_bar_increment(lv_obj_t *target, int val, int anm) {
    int old = lv_bar_get_value(target);
    lv_bar_set_value(target, old + val, anm);
}

void _ui_slider_increment(lv_obj_t *target, int val, int anm) {
    int old = lv_slider_get_value(target);
    lv_slider_set_value(target, old + val, anm);
    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
}

void _ui_keyboard_set_target(lv_obj_t *keyboard, lv_obj_t *textarea) {
    lv_keyboard_set_textarea(keyboard, textarea);
}

void _ui_flag_modify(lv_obj_t *target, int32_t flag, int value) {
    if (value == _UI_MODIFY_FLAG_TOGGLE) {
        if (lv_obj_has_flag(target, flag))
            lv_obj_remove_flag(target, flag);
        else
            lv_obj_add_flag(target, flag);
    } else if (value == _UI_MODIFY_FLAG_ADD)
        lv_obj_add_flag(target, flag);
    else
        lv_obj_remove_flag(target, flag);
}

void _ui_state_modify(lv_obj_t *target, int32_t state, int value) {
    if (value == _UI_MODIFY_STATE_TOGGLE) {
        if (lv_obj_has_state(target, state))
            lv_obj_remove_state(target, state);
        else
            lv_obj_add_state(target, state);
    } else if (value == _UI_MODIFY_STATE_ADD)
        lv_obj_add_state(target, state);
    else
        lv_obj_remove_state(target, state);
}

#if !(LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5))
static void _ui_state_trickle_event_cb(lv_event_t *e) {
    lv_obj_t *parent = lv_event_get_current_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_state_t st;
    bool add;
    switch (code) {
    case LV_EVENT_PRESSED:
        st = LV_STATE_PRESSED;
        add = true;
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        st = LV_STATE_PRESSED;
        add = false;
        break;
    case LV_EVENT_FOCUSED:
        st = LV_STATE_FOCUSED;
        add = true;
        break;
    case LV_EVENT_DEFOCUSED:
        st = LV_STATE_FOCUSED;
        add = false;
        break;
    default:
        return;
    }
    uint32_t cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *ch = lv_obj_get_child(parent, i);
        if (add)
            _ui_state_trickle_add(ch, st);
        else
            _ui_state_trickle_remove(ch, st);
    }
}
#endif

void _ui_clear_transition(lv_obj_t *target) {
    static lv_style_t _ui_trans_reset;
    static bool _ui_trans_reset_inited = false;
    if (!_ui_trans_reset_inited) {
        lv_style_init(&_ui_trans_reset);
        _ui_trans_reset_inited = true;
    }
    lv_obj_add_style(target, &_ui_trans_reset, LV_PART_MAIN);
}

void _ui_state_trickle_enable(lv_obj_t *target) {
    if (lv_obj_has_flag(target, _UI_STATE_TRICKLE_FLAG))
        return;
    lv_obj_add_flag(target, _UI_STATE_TRICKLE_FLAG);
#if !(LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5))
    static const lv_event_code_t evs[] = {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST, LV_EVENT_FOCUSED,
                                          LV_EVENT_DEFOCUSED};
    for (uint32_t i = 0; i < sizeof(evs) / sizeof(evs[0]); i++) {
        lv_obj_add_event_cb(target, _ui_state_trickle_event_cb, evs[i], NULL);
    }
#endif
}

void _ui_state_trickle_add(lv_obj_t *target, lv_state_t state) {
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5)
    lv_obj_add_state(target, state);
#else
    lv_state_t old = lv_obj_get_state(target);
    if ((old | state) == old)
        return;
    lv_obj_add_state(target, state);
    if (!lv_obj_has_flag(target, _UI_STATE_TRICKLE_FLAG))
        return;
    uint32_t cnt = lv_obj_get_child_count(target);
    for (uint32_t i = 0; i < cnt; i++) {
        _ui_state_trickle_add(lv_obj_get_child(target, i), state);
    }
#endif
}

void _ui_state_trickle_remove(lv_obj_t *target, lv_state_t state) {
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5)
    lv_obj_remove_state(target, state);
#else
    lv_state_t old = lv_obj_get_state(target);
    if ((old & ~state) == old)
        return;
    lv_obj_remove_state(target, state);
    if (!lv_obj_has_flag(target, _UI_STATE_TRICKLE_FLAG))
        return;
    uint32_t cnt = lv_obj_get_child_count(target);
    for (uint32_t i = 0; i < cnt; i++) {
        _ui_state_trickle_remove(lv_obj_get_child(target, i), state);
    }
#endif
}

void _ui_textarea_move_cursor(lv_obj_t *target, int val) {
    if (val == UI_MOVE_CURSOR_UP)
        lv_textarea_cursor_up(target);
    if (val == UI_MOVE_CURSOR_RIGHT)
        lv_textarea_cursor_right(target);
    if (val == UI_MOVE_CURSOR_DOWN)
        lv_textarea_cursor_down(target);
    if (val == UI_MOVE_CURSOR_LEFT)
        lv_textarea_cursor_left(target);
    lv_obj_add_state(target, LV_STATE_FOCUSED);
}

static void scr_unloaded_delete_async_cb(void *p) {
    if (p != NULL)
        lv_obj_delete((lv_obj_t *)p);
}

void scr_unloaded_delete_cb(lv_event_t *e) {
    lv_obj_t **var = lv_event_get_user_data(e);
    lv_obj_t *scr = *var;
    *var = NULL;
    lv_async_call(scr_unloaded_delete_async_cb, scr);
}

void _ui_opacity_set(lv_obj_t *target, int val) {
    lv_obj_set_style_opa(target, val, 0);
}

void _ui_anim_callback_free_user_data(lv_anim_t *a) {
    lv_free(a->user_data);
    a->user_data = NULL;
}

void _ui_anim_callback_set_x(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_x(usr->target, v);
}

void _ui_anim_callback_set_y(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_y(usr->target, v);
}

void _ui_anim_callback_set_width(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_width(usr->target, v);
}

void _ui_anim_callback_set_height(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_height(usr->target, v);
}

void _ui_anim_callback_set_opacity(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_style_opa(usr->target, v, 0);
}

void _ui_anim_callback_set_image_zoom(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_image_set_scale(usr->target, v);
}

void _ui_anim_callback_set_image_angle(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    lv_image_set_rotation(usr->target, v);
}

void _ui_anim_callback_set_image_frame(lv_anim_t *a, int32_t v) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    usr->val = v;

    if (v < 0)
        v = 0;
    if (v >= usr->imgset_size)
        v = usr->imgset_size - 1;
    lv_image_set_src(usr->target, usr->imgset[v]);
}

int32_t _ui_anim_callback_get_x(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_x_aligned(usr->target);
}

int32_t _ui_anim_callback_get_y(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_y_aligned(usr->target);
}

int32_t _ui_anim_callback_get_width(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_width(usr->target);
}

int32_t _ui_anim_callback_get_height(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_height(usr->target);
}

int32_t _ui_anim_callback_get_opacity(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_style_opa(usr->target, 0);
}

int32_t _ui_anim_callback_get_image_zoom(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_image_get_scale(usr->target);
}

int32_t _ui_anim_callback_get_image_angle(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return lv_image_get_rotation(usr->target);
}

int32_t _ui_anim_callback_get_image_frame(lv_anim_t *a) {
    ui_anim_user_data_t *usr = (ui_anim_user_data_t *)a->user_data;
    return usr->val;
}

void _ui_arc_set_text_value(lv_obj_t *trg, lv_obj_t *src, const char *prefix, const char *postfix) {
    char buf[_UI_TEMPORARY_STRING_BUFFER_SIZE];

    lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)lv_arc_get_value(src), postfix);

    lv_label_set_text(trg, buf);
}

void _ui_slider_set_text_value(lv_obj_t *trg, lv_obj_t *src, const char *prefix, const char *postfix) {
    char buf[_UI_TEMPORARY_STRING_BUFFER_SIZE];

    lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)lv_slider_get_value(src), postfix);

    lv_label_set_text(trg, buf);
}

void _ui_checked_set_text_value(lv_obj_t *trg, lv_obj_t *src, const char *txt_on, const char *txt_off) {
    if (lv_obj_has_state(src, LV_STATE_CHECKED))
        lv_label_set_text(trg, txt_on);
    else
        lv_label_set_text(trg, txt_off);
}

void _ui_spinbox_step(lv_obj_t *target, int val) {
    if (val > 0)
        lv_spinbox_increment(target);

    else
        lv_spinbox_decrement(target);

    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
}

/* ===================== Chart ===================== */
#if LV_USE_CHART && LVGL_VERSION_MAJOR >= 9

lv_obj_t *_ui_chart_get_scale(lv_obj_t *chart, uint32_t idx) {
    if (chart == NULL || idx >= lv_obj_get_child_count(chart))
        return NULL;
    return lv_obj_get_child(chart, (int32_t)idx);
}

lv_chart_series_t *_ui_chart_get_series(lv_obj_t *chart, uint32_t index) {
    if (chart == NULL)
        return NULL;

    lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);
    for (uint32_t i = 0; i < index && ser != NULL; i++)
        ser = lv_chart_get_series_next(chart, ser);

    return ser;
}

static bool _ui_chart_is_sep(char c) {
    return c == '\n' || c == '\r' || c == ',' || c == ';' || c == '\t' || c == ' ';
}

static int32_t _ui_chart_next_token(const char **p, bool *got) {
    const char *s = *p;

    while (*s != '\0' && _ui_chart_is_sep(*s))
        s++;

    if (*s == '\0') {
        *p = s;
        *got = false;
        return 0;
    }

    const char *start = s;
    bool neg = false;
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }

    int32_t v = 0;
    bool any = false;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (int32_t)(*s - '0');
        s++;
        any = true;
    }

    if (!any) {
        s = start;
        while (*s != '\0' && !_ui_chart_is_sep(*s))
            s++;
        v = 0;
    }

    *p = s;
    *got = true;
    return neg ? -v : v;
}

void _ui_chart_series_set_from_string(lv_obj_t *chart, lv_chart_series_t *ser, const char *y_str, const char *x_str) {
    if (chart == NULL || ser == NULL)
        return;

    uint32_t cnt = lv_chart_get_point_count(chart);
    const char *yp = (y_str != NULL) ? y_str : "";
    const char *xp = (x_str != NULL) ? x_str : "";

    for (uint32_t i = 0; i < cnt; i++) {
        bool got_y = false;
        int32_t y = _ui_chart_next_token(&yp, &got_y);
        if (!got_y)
            y = LV_CHART_POINT_NONE;

        if (x_str != NULL) {
            bool got_x = false;
            int32_t x = _ui_chart_next_token(&xp, &got_x);
            if (!got_x)
                x = (int32_t)i;
            _UI_CHART_SET_VALUE_BY_ID2(chart, ser, i, x, y);
        } else {
            _UI_CHART_SET_VALUE_BY_ID(chart, ser, i, y);
        }
    }

    lv_chart_refresh(chart);
}

#if LV_USE_OBSERVER

static void _ui_chart_series_bind_del_cb(lv_event_t *e) {
    lv_free(lv_event_get_user_data(e));
}

_ui_chart_series_bind_t *_ui_chart_series_bind_create(lv_obj_t *chart, lv_chart_series_t *ser, uint8_t scatter) {
    _ui_chart_series_bind_t *b = lv_malloc(sizeof(_ui_chart_series_bind_t));
    if (b == NULL)
        return NULL;

    lv_memzero(b, sizeof(*b));
    b->ser = ser;
    b->scatter = scatter;

    lv_obj_add_event_cb(chart, _ui_chart_series_bind_del_cb, LV_EVENT_DELETE, b);
    return b;
}

void _ui_chart_series_bind_apply(lv_obj_t *chart, _ui_chart_series_bind_t *b) {
    if (b == NULL)
        return;

    const char *y = (b->y_src != NULL) ? lv_subject_get_string(b->y_src) : b->y_lit;
    const char *x = NULL;
    if (b->scatter)
        x = (b->x_src != NULL) ? lv_subject_get_string(b->x_src) : b->x_lit;

    _ui_chart_series_set_from_string(chart, b->ser, y, x);
}

#endif

#endif
