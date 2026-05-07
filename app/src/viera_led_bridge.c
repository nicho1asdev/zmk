// SPDX-License-Identifier: MIT
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <stdlib.h>
#include <viera_effects.h>

LOG_MODULE_REGISTER(viera_led_bridge, CONFIG_LOG_DEFAULT_LEVEL);

#define VIERA_FADE_TOTAL_MS 250
#define VIERA_FADE_STEP_MS 16

#define ZMK_BRT_MAX 100

static atomic_t g_target_brt = ATOMIC_INIT(10);
static atomic_t g_current_brt = ATOMIC_INIT(10);

/* Last-written RGB cache so GATT reads return exactly what the host wrote.
 * Stored as 0xRRGGBB; sentinel -1 means uninitialized (lazy-seed from HSB).
 */
static atomic_t g_color_cache = ATOMIC_INIT(-1);

static void viera_brightness_fade_work(struct k_work *work);
static struct k_work_delayable g_fade_work;

static inline uint8_t clamp_0_100(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 100) {
        return 100;
    }
    return (uint8_t)v;
}

uint8_t viera_user_brightness_get(void) {
    return (uint8_t)atomic_get(&g_current_brt);
}

static void viera_user_brightness_set_target(uint8_t v) {
    v = clamp_0_100(v);
    atomic_set(&g_target_brt, v);
    (void)k_work_reschedule(&g_fade_work, K_NO_WAIT);
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif

static void viera_brightness_fade_work(struct k_work *work) {
    ARG_UNUSED(work);

    int curr = atomic_get(&g_current_brt);
    int target = atomic_get(&g_target_brt);

    if (curr == target) {
        return;
    }

    int diff = target - curr;
    int steps = MAX(1, VIERA_FADE_TOTAL_MS / VIERA_FADE_STEP_MS);
    int step_mag = MAX(1, abs(diff) / steps);
    int next = curr + (diff > 0 ? step_mag : -step_mag);

    if ((diff > 0 && next > target) || (diff < 0 && next < target)) {
        next = target;
    }

    atomic_set(&g_current_brt, clamp_0_100(next));

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    struct zmk_led_hsb c = zmk_rgb_underglow_get_hsb();
    int b = (atomic_get(&g_current_brt) * ZMK_BRT_MAX) / 100;
    c.b = (uint8_t)MIN(b, ZMK_BRT_MAX);
    (void)zmk_rgb_underglow_set_hsb(c);
    zmk_rgb_underglow_request_refresh();
#endif

    if (next != target) {
        (void)k_work_reschedule(&g_fade_work, K_MSEC(VIERA_FADE_STEP_MS));
    }
}

void viera_on_backlight_power_changed(uint8_t on) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    if (on) {
        (void)zmk_rgb_underglow_on();
        struct zmk_led_hsb c = zmk_rgb_underglow_get_hsb();
        int b = (atomic_get(&g_current_brt) * ZMK_BRT_MAX) / 100;
        c.b = (uint8_t)MIN(b, ZMK_BRT_MAX);
        (void)zmk_rgb_underglow_set_hsb(c);
        zmk_rgb_underglow_request_refresh();
    } else {
        (void)zmk_rgb_underglow_off();
    }
#else
    ARG_UNUSED(on);
#endif
}

void viera_on_brightness_changed(uint8_t level) {
    viera_user_brightness_set_target(level);
}

void viera_on_effect_changed(uint8_t effect) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    if (effect >= VIERA_EFF_NUMBER) {
        return;
    }
    (void)zmk_rgb_underglow_on();
    (void)zmk_rgb_underglow_select_effect((int)effect);
    zmk_rgb_underglow_request_refresh();
#endif
}

static void rgb_to_hsb(uint8_t r, uint8_t g, uint8_t b, uint16_t *h, uint8_t *s, uint8_t *br) {
    int max_c = MAX(r, MAX(g, b));
    int min_c = MIN(r, MIN(g, b));
    int delta = max_c - min_c;

    *br = (uint8_t)((max_c * 100) / 255);

    if (max_c == 0 || delta == 0) {
        *s = 0;
        *h = 0;
        return;
    }

    *s = (uint8_t)((delta * 100) / max_c);

    int hue;
    if (max_c == r) {
        hue = ((g - b) * 60) / delta;
        if (hue < 0) hue += 360;
    } else if (max_c == g) {
        hue = 120 + ((b - r) * 60) / delta;
    } else {
        hue = 240 + ((r - g) * 60) / delta;
    }
    *h = (uint16_t)hue;
}

static inline int pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((int)r << 16) | ((int)g << 8) | (int)b;
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
static void hsb_to_rgb(uint16_t h, uint8_t s, uint8_t br,
                       uint8_t *r, uint8_t *g, uint8_t *b) {
    /* h: 0..359, s: 0..100, br: 0..100 */
    if (h >= 360) h %= 360;
    if (s > 100) s = 100;
    if (br > 100) br = 100;

    int v = (br * 255) / 100;
    if (s == 0) {
        *r = *g = *b = (uint8_t)v;
        return;
    }
    int region = h / 60;
    int remainder = (h - region * 60) * 255 / 60;
    int p = (v * (255 - s * 255 / 100)) / 255;
    int q = (v * (255 - (s * remainder) / 100)) / 255;
    int t = (v * (255 - (s * (255 - remainder)) / 100)) / 255;

    int rr = 0, gg = 0, bb = 0;
    switch (region) {
    case 0: rr = v; gg = t; bb = p; break;
    case 1: rr = q; gg = v; bb = p; break;
    case 2: rr = p; gg = v; bb = t; break;
    case 3: rr = p; gg = q; bb = v; break;
    case 4: rr = t; gg = p; bb = v; break;
    default: rr = v; gg = p; bb = q; break;
    }
    *r = (uint8_t)CLAMP(rr, 0, 255);
    *g = (uint8_t)CLAMP(gg, 0, 255);
    *b = (uint8_t)CLAMP(bb, 0, 255);
}
#endif

void viera_on_color_changed(uint8_t r, uint8_t g, uint8_t b) {
    atomic_set(&g_color_cache, pack_rgb(r, g, b));
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    uint16_t h;
    uint8_t s, br;
    rgb_to_hsb(r, g, b, &h, &s, &br);

    struct zmk_led_hsb c = zmk_rgb_underglow_get_hsb();
    c.h = h;
    c.s = s;
    (void)zmk_rgb_underglow_set_hsb(c);
    zmk_rgb_underglow_request_refresh();
    LOG_DBG("Color RGB(%u,%u,%u) -> HSB(%u,%u,%u)", r, g, b, h, s, br);
#endif
}

void viera_on_speed_changed(uint8_t speed) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    (void)zmk_rgb_underglow_set_speed(speed);
    zmk_rgb_underglow_request_refresh();
    LOG_DBG("Speed -> %u", speed);
#endif
}

/* -------- Read-side getters consumed by viera_gatt.c -------- */

int viera_get_power(uint8_t *out) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    *out = zmk_rgb_underglow_is_on() ? 1 : 0;
    return 0;
#else
    ARG_UNUSED(out);
    return -ENOTSUP;
#endif
}

int viera_get_brightness(uint8_t *out) {
    /* Return the last user-requested level (target), so a write immediately
     * round-trips through a read even mid-fade. */
    int v = atomic_get(&g_target_brt);
    *out = clamp_0_100(v);
    return 0;
}

int viera_get_effect(uint8_t *out) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    *out = zmk_rgb_underglow_get_effect();
    return 0;
#else
    ARG_UNUSED(out);
    return -ENOTSUP;
#endif
}

int viera_get_color(uint8_t out[3]) {
    int v = atomic_get(&g_color_cache);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    if (v < 0) {
        /* Lazy seed: derive RGB from the persisted HSB so the first read
         * after boot reflects real state. We synthesize at full brightness
         * because the host's color slider is independent of the brightness
         * slider; the user_brightness path already handles dimming. */
        struct zmk_led_hsb c = zmk_rgb_underglow_get_hsb();
        uint8_t r, g, b;
        hsb_to_rgb(c.h, c.s, 100, &r, &g, &b);
        v = pack_rgb(r, g, b);
        atomic_set(&g_color_cache, v);
    }
#else
    if (v < 0) {
        v = pack_rgb(255, 255, 255);
    }
#endif
    out[0] = (uint8_t)((v >> 16) & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)(v & 0xFF);
    return 0;
}

int viera_get_speed(uint8_t *out) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    uint8_t s = zmk_rgb_underglow_get_speed();
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    *out = s;
    return 0;
#else
    ARG_UNUSED(out);
    return -ENOTSUP;
#endif
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
static void viera_startup_effect_work(struct k_work *work) {
    ARG_UNUSED(work);
    struct zmk_led_hsb c = zmk_rgb_underglow_get_hsb();
    int b = (atomic_get(&g_current_brt) * ZMK_BRT_MAX) / 100;
    c.b = (uint8_t)MIN(b, ZMK_BRT_MAX);
    (void)zmk_rgb_underglow_set_hsb(c);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_VIERA_MIRROR_FILL)
    (void)zmk_rgb_underglow_select_effect(VIERA_EFF_MIRROR_FILL);
#else
    (void)zmk_rgb_underglow_select_effect(VIERA_EFF_SWIRL);
#endif
    zmk_rgb_underglow_request_refresh();
}
static K_WORK_DELAYABLE_DEFINE(g_startup_work, viera_startup_effect_work);
#endif

static int viera_led_bridge_init(void) {
    k_work_init_delayable(&g_fade_work, viera_brightness_fade_work);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    k_work_schedule(&g_startup_work, K_MSEC(50));
#endif
    return 0;
}
SYS_INIT(viera_led_bridge_init, APPLICATION, 50);
