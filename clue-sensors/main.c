/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       Sensor Deck: the CLUE sensors on three lvgl pages
 *
 * Climate (temperature, humidity, pressure and a temperature history),
 * light (ambient level, color and proximity) and a compass. All values come
 * from SAUL. Button A shows the previous page, button B the next one.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phydat.h"
#include "saul_reg.h"
#include "ztimer.h"

#include "lvgl/lvgl.h"
#include "lvgl_riot.h"

#include "demo_board.h"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

#define REFRESH_MS      (100U)
#define HISTORY_POINTS  (60U)
#define HISTORY_MS      (2000U)
#define CLIMATE_MS      (1000U)
#define LIGHT_MS        (200U)
#define NUM_PAGES       (3U)

#define COLOR_BG        (0x0b0e1e)
#define COLOR_PANEL     (0x161b33)
#define COLOR_TEXT      (0xe8ecff)
#define COLOR_DIM       (0x8890b8)
#define COLOR_WARM      (0xff8a3d)
#define COLOR_COOL      (0x3db8ff)

typedef struct {
    saul_reg_t *temp;
    saul_reg_t *hum;
    saul_reg_t *press;
    saul_reg_t *als;
    saul_reg_t *rgb;
    saul_reg_t *prox;
    saul_reg_t *mag;
} sensors_t;

static sensors_t _sensors;

static lv_obj_t *_pages[NUM_PAGES];
static lv_obj_t *_dots[NUM_PAGES];
static unsigned _page;
static buttons_t _buttons;

/* climate */
static lv_obj_t *_temp_arc;
static lv_obj_t *_temp_label;
static lv_obj_t *_hum_bar;
static lv_obj_t *_hum_label;
static lv_obj_t *_press_label;
static lv_obj_t *_chart;
static lv_chart_series_t *_temp_series;

/* light */
static lv_obj_t *_als_bar;
static lv_obj_t *_als_label;
static lv_obj_t *_swatch;
static lv_obj_t *_rgb_label;
static lv_obj_t *_prox_bar;
static lv_obj_t *_prox_label;

/* compass */
static lv_obj_t *_needle;
static lv_obj_t *_heading_label;
static lv_obj_t *_calib_label;
static lv_point_precise_t _needle_points[2];
static float _mag_min[2] = { 1e9f, 1e9f };
static float _mag_max[2] = { -1e9f, -1e9f };
static float _heading;

/* Read the first value of a sensor as a float, false on error */
static bool _read(saul_reg_t *dev, float *values, unsigned max, unsigned *dim, uint8_t *unit)
{
    phydat_t data;
    if (dev == NULL) {
        return false;
    }
    int res = saul_reg_read(dev, &data);
    if (res <= 0) {
        return false;
    }
    float factor = powf(10.0f, data.scale);
    for (unsigned i = 0; i < (unsigned)res && i < max; i++) {
        values[i] = data.val[i] * factor;
    }
    if (dim) {
        *dim = (unsigned)res;
    }
    if (unit) {
        *unit = data.unit;
    }
    return true;
}

static void _find_sensors(void)
{
    _sensors.temp = saul_reg_find_type(SAUL_SENSE_TEMP);
    _sensors.hum = saul_reg_find_type(SAUL_SENSE_HUM);
    _sensors.press = saul_reg_find_type(SAUL_SENSE_PRESS);
    _sensors.prox = saul_reg_find_type(SAUL_SENSE_PROXIMITY);
    _sensors.mag = saul_reg_find_type(SAUL_SENSE_MAG);

    /* the APDS9960 gives two light entries: the ambient level and the color */
    for (saul_reg_t *dev = saul_reg; dev; dev = dev->next) {
        if (dev->driver->type != SAUL_SENSE_LIGHT) {
            continue;
        }
        float v[3];
        unsigned dim = 0;
        if (!_read(dev, v, 3, &dim, NULL)) {
            continue;
        }
        if (dim >= 3 && !_sensors.rgb) {
            _sensors.rgb = dev;
        }
        else if (dim == 1 && !_sensors.als) {
            _sensors.als = dev;
        }
    }

    for (saul_reg_t *dev = saul_reg; dev; dev = dev->next) {
        printf("SAUL: %s (%s)\n", dev->name, saul_class_to_str(dev->driver->type));
    }
}

static lv_obj_t *_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (font) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *_page_create(lv_obj_t *scr, const char *title)
{
    lv_obj_t *page = lv_obj_create(scr);
    lv_obj_set_size(page, 240, 214);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_scrollable(page, false);

    lv_obj_t *label = _label(page, &lv_font_montserrat_14, COLOR_DIM);
    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);
    return page;
}

static lv_obj_t *_bar(lv_obj_t *parent, int x, int y, int w, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, 10);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    return bar;
}

static void _create_climate(lv_obj_t *page)
{
    _temp_arc = lv_arc_create(page);
    lv_obj_set_size(_temp_arc, 120, 120);
    lv_obj_set_pos(_temp_arc, 10, 32);
    lv_arc_set_bg_angles(_temp_arc, 135, 45);
    lv_arc_set_range(_temp_arc, -100, 400);     /* tenths of degree */
    lv_obj_remove_style(_temp_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(_temp_arc, false);
    lv_obj_set_style_arc_width(_temp_arc, 10, 0);
    lv_obj_set_style_arc_width(_temp_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_temp_arc, lv_color_hex(COLOR_PANEL), 0);

    _temp_label = _label(_temp_arc, &lv_font_montserrat_28, COLOR_TEXT);
    lv_label_set_text(_temp_label, "--");
    lv_obj_center(_temp_label);
    lv_obj_t *unit = _label(_temp_arc, NULL, COLOR_DIM);
    lv_label_set_text(unit, "deg C");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 24);

    lv_obj_t *hum_title = _label(page, NULL, COLOR_DIM);
    lv_label_set_text(hum_title, "Humidity");
    lv_obj_set_pos(hum_title, 140, 40);
    _hum_label = _label(page, &lv_font_montserrat_14, COLOR_TEXT);
    lv_label_set_text(_hum_label, "-- %");
    lv_obj_set_pos(_hum_label, 140, 56);
    _hum_bar = _bar(page, 140, 76, 88, COLOR_COOL);

    lv_obj_t *press_title = _label(page, NULL, COLOR_DIM);
    lv_label_set_text(press_title, "Pressure");
    lv_obj_set_pos(press_title, 140, 100);
    _press_label = _label(page, &lv_font_montserrat_14, COLOR_TEXT);
    lv_label_set_text(_press_label, "-- hPa");
    lv_obj_set_pos(_press_label, 140, 116);

    _chart = lv_chart_create(page);
    lv_obj_set_size(_chart, 220, 54);
    lv_obj_set_pos(_chart, 10, 156);
    lv_chart_set_type(_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(_chart, HISTORY_POINTS);
    lv_chart_set_update_mode(_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(_chart, 0, 0);
    lv_obj_set_style_size(_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_chart, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_width(_chart, 0, 0);
    _temp_series = lv_chart_add_series(_chart, lv_color_hex(COLOR_WARM), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(_chart, _temp_series, LV_CHART_POINT_NONE);
}

static void _create_light(lv_obj_t *page)
{
    lv_obj_t *t = _label(page, NULL, COLOR_DIM);
    lv_label_set_text(t, "Ambient light");
    lv_obj_set_pos(t, 16, 40);
    _als_label = _label(page, &lv_font_montserrat_28, COLOR_TEXT);
    lv_label_set_text(_als_label, "--");
    lv_obj_set_pos(_als_label, 16, 56);
    _als_bar = _bar(page, 16, 92, 208, 0xffe066);

    t = _label(page, NULL, COLOR_DIM);
    lv_label_set_text(t, "Color");
    lv_obj_set_pos(t, 16, 112);
    _swatch = lv_obj_create(page);
    lv_obj_set_size(_swatch, 60, 40);
    lv_obj_set_pos(_swatch, 16, 128);
    lv_obj_set_style_radius(_swatch, 8, 0);
    lv_obj_set_style_border_color(_swatch, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_border_width(_swatch, 1, 0);
    _rgb_label = _label(page, NULL, COLOR_TEXT);
    lv_label_set_text(_rgb_label, "R -  G -  B -");
    lv_obj_set_pos(_rgb_label, 88, 140);

    t = _label(page, NULL, COLOR_DIM);
    lv_label_set_text(t, "Proximity");
    lv_obj_set_pos(t, 16, 178);
    _prox_label = _label(page, NULL, COLOR_TEXT);
    lv_label_set_text(_prox_label, "--");
    lv_obj_set_pos(_prox_label, 170, 178);
    _prox_bar = _bar(page, 16, 196, 208, 0xc77dff);
}

static void _create_compass(lv_obj_t *page)
{
    lv_obj_t *dial = lv_obj_create(page);
    lv_obj_set_size(dial, 150, 150);
    lv_obj_align(dial, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(dial, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_border_width(dial, 2, 0);
    lv_obj_set_style_pad_all(dial, 0, 0);
    lv_obj_set_scrollable(dial, false);

    static const char *const marks[] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *m = _label(dial, &lv_font_montserrat_14, i == 0 ? 0xff5050 : COLOR_DIM);
        lv_label_set_text(m, marks[i]);
        float a = i * (float)M_PI / 2;
        lv_obj_align(m, LV_ALIGN_CENTER, (int)(58 * sinf(a)), (int)(-58 * cosf(a)));
    }

    _needle = lv_line_create(dial);
    lv_obj_set_style_line_width(_needle, 4, 0);
    lv_obj_set_style_line_color(_needle, lv_color_hex(0xff5050), 0);
    lv_obj_set_style_line_rounded(_needle, true, 0);
    lv_obj_set_pos(_needle, 0, 0);

    _heading_label = _label(page, &lv_font_montserrat_14, COLOR_TEXT);
    lv_label_set_text(_heading_label, "---");
    lv_obj_align(_heading_label, LV_ALIGN_TOP_MID, 0, 186);

    _calib_label = _label(page, NULL, COLOR_DIM);
    lv_label_set_text(_calib_label, "turn the board around");
    lv_obj_align(_calib_label, LV_ALIGN_TOP_MID, 0, 204);
}

static void _show_page(unsigned page)
{
    for (unsigned i = 0; i < NUM_PAGES; i++) {
        lv_obj_set_hidden(_pages[i], i != page);
        lv_obj_set_style_bg_color(_dots[i], lv_color_hex(i == page ? COLOR_TEXT : COLOR_PANEL),
                                  0);
    }
    _page = page;
}

static void _update_climate(void)
{
    float v[1];
    uint8_t unit;

    if (_read(_sensors.temp, v, 1, NULL, NULL)) {
        lv_arc_set_value(_temp_arc, (int)(v[0] * 10));
        lv_obj_set_style_arc_color(_temp_arc, lv_color_hex(v[0] > 24 ? COLOR_WARM : COLOR_COOL),
                                   LV_PART_INDICATOR);
        lv_label_set_text_fmt(_temp_label, "%d.%d", (int)v[0], abs((int)(v[0] * 10)) % 10);
    }
    if (_read(_sensors.hum, v, 1, NULL, NULL)) {
        lv_bar_set_value(_hum_bar, (int)v[0], LV_ANIM_ON);
        lv_label_set_text_fmt(_hum_label, "%d %%", (int)lroundf(v[0]));
    }
    if (_read(_sensors.press, v, 1, NULL, &unit)) {
        float hpa = (unit == UNIT_PA) ? v[0] / 100.0f : v[0];
        lv_label_set_text_fmt(_press_label, "%d hPa", (int)lroundf(hpa));
    }
}

static void _sample_history(void)
{
    float v[1];
    if (_read(_sensors.temp, v, 1, NULL, NULL)) {
        int tenths = (int)(v[0] * 10);
        lv_chart_set_next_value(_chart, _temp_series, tenths);
        /* keep a range of at least 2 degrees around the values */
        int32_t lo = tenths - 10, hi = tenths + 10;
        for (unsigned i = 0; i < HISTORY_POINTS; i++) {
            int32_t p = lv_chart_get_series_y_array(_chart, _temp_series)[i];
            if (p == LV_CHART_POINT_NONE) {
                continue;
            }
            lo = (p - 5 < lo) ? p - 5 : lo;
            hi = (p + 5 > hi) ? p + 5 : hi;
        }
        lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
    }
}

static void _update_light(void)
{
    float v[3];
    unsigned dim;

    if (_read(_sensors.als, v, 1, NULL, NULL)) {
        /* logarithmic bar: 1 to 10000 counts */
        float pct = (v[0] > 1.0f) ? 25.0f * log10f(v[0]) : 0.0f;
        lv_bar_set_value(_als_bar, (int)fminf(pct, 100.0f), LV_ANIM_ON);
        lv_label_set_text_fmt(_als_label, "%d", (int)v[0]);
    }
    if (_read(_sensors.rgb, v, 3, &dim, NULL) && dim >= 3) {
        float max = fmaxf(v[0], fmaxf(v[1], v[2]));
        if (max > 0) {
            lv_obj_set_style_bg_color(_swatch, lv_color_make((uint8_t)(255 * v[0] / max),
                                                             (uint8_t)(255 * v[1] / max),
                                                             (uint8_t)(255 * v[2] / max)), 0);
        }
        lv_label_set_text_fmt(_rgb_label, "R %d  G %d  B %d", (int)v[0], (int)v[1], (int)v[2]);
    }
    if (_read(_sensors.prox, v, 1, NULL, NULL)) {
        /* the APDS9960 proximity is an 8-bit value */
        int pct = (int)(v[0] * 100 / 255);
        lv_bar_set_value(_prox_bar, pct > 100 ? 100 : pct, LV_ANIM_ON);
        lv_label_set_text(_prox_label, pct > 60 ? "near!" : (pct > 15 ? "close" : "far"));
    }
}

static void _update_compass(void)
{
    float v[3];
    unsigned dim;
    static const char *const names[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

    if (!_read(_sensors.mag, v, 3, &dim, NULL) || dim < 2) {
        return;
    }

    /* hard iron calibration from the extremes seen while turning */
    for (int i = 0; i < 2; i++) {
        _mag_min[i] = fminf(_mag_min[i], v[i]);
        _mag_max[i] = fmaxf(_mag_max[i], v[i]);
    }
    float range = fminf(_mag_max[0] - _mag_min[0], _mag_max[1] - _mag_min[1]);
    bool calibrated = range > 0.3f;
    float x = v[0] - (_mag_max[0] + _mag_min[0]) / 2;
    float y = v[1] - (_mag_max[1] + _mag_min[1]) / 2;

    /* smooth the heading, going the short way around */
    float target = atan2f(y, x);
    float diff = target - _heading;
    while (diff > (float)M_PI) {
        diff -= 2 * (float)M_PI;
    }
    while (diff < -(float)M_PI) {
        diff += 2 * (float)M_PI;
    }
    _heading += 0.3f * diff;

    /* the needle points north: opposite to the heading of the board */
    const float cx = 73, cy = 73, len = 50;
    _needle_points[0].x = cx;
    _needle_points[0].y = cy;
    _needle_points[1].x = cx + len * sinf(-_heading);
    _needle_points[1].y = cy - len * cosf(-_heading);
    lv_line_set_points(_needle, _needle_points, 2);

    int deg = (int)lroundf(_heading * 180.0f / (float)M_PI);
    deg = (deg % 360 + 360) % 360;
    lv_label_set_text_fmt(_heading_label, "%d deg %s", deg, names[((deg + 22) / 45) % 8]);
    lv_label_set_text(_calib_label, calibrated ? "" : "turn the board around");
}

static void _refresh(lv_timer_t *timer)
{
    (void)timer;
    static uint32_t last_climate, last_light, last_history;
    uint32_t now = ztimer_now(ZTIMER_MSEC);

    buttons_update(&_buttons);
    if (_buttons.pressed & BTN_A) {
        _show_page((_page + NUM_PAGES - 1) % NUM_PAGES);
        last_climate = last_light = 0;
    }
    if (_buttons.pressed & BTN_B) {
        _show_page((_page + 1) % NUM_PAGES);
        last_climate = last_light = 0;
    }

    if (now - last_history >= HISTORY_MS || last_history == 0) {
        last_history = now;
        _sample_history();
    }

    switch (_page) {
    case 0:
        if (now - last_climate >= CLIMATE_MS || last_climate == 0) {
            last_climate = now;
            _update_climate();
        }
        break;
    case 1:
        if (now - last_light >= LIGHT_MS || last_light == 0) {
            last_light = now;
            _update_light();
        }
        break;
    default:
        _update_compass();
        break;
    }

    lvgl_wakeup();
}

static void _create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_scrollable(scr, false);

    _pages[0] = _page_create(scr, "CLIMATE");
    _create_climate(_pages[0]);
    _pages[1] = _page_create(scr, "LIGHT");
    _create_light(_pages[1]);
    _pages[2] = _page_create(scr, "COMPASS");
    _create_compass(_pages[2]);

    for (unsigned i = 0; i < NUM_PAGES; i++) {
        _dots[i] = lv_obj_create(scr);
        lv_obj_set_size(_dots[i], 8, 8);
        lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(_dots[i], 0, 0);
        lv_obj_align(_dots[i], LV_ALIGN_BOTTOM_MID, (int)(i * 16) - 16, -8);
    }

    lv_obj_t *help_a = _label(scr, NULL, COLOR_DIM);
    lv_label_set_text(help_a, "< A");
    lv_obj_align(help_a, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_t *help_b = _label(scr, NULL, COLOR_DIM);
    lv_label_set_text(help_b, "B >");
    lv_obj_align(help_b, LV_ALIGN_BOTTOM_RIGHT, -8, -4);

    _show_page(0);
    lv_timer_create(_refresh, REFRESH_MS, NULL);
}

int main(void)
{
    buttons_init();
    _find_sensors();

    disp_dev_backlight_on();
    _create_ui();
    lvgl_run();

    return 0;
}
