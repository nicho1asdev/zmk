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
