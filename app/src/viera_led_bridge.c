// SPDX-License-Identifier: MIT
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(viera_led_bridge, CONFIG_LOG_DEFAULT_LEVEL);

/* ---- Master user brightness state with fade (0..100) ---- */
#define VIERA_FADE_TOTAL_MS 250
#define VIERA_FADE_STEP_MS   16

static atomic_t g_target_brt  = ATOMIC_INIT(10); /* Requested brightness */
static atomic_t g_current_brt = ATOMIC_INIT(10); /* Animated brightness  */

static void viera_brightness_fade_work(struct k_work *work);
static struct k_work_delayable g_fade_work;

static inline uint8_t clamp_0_100(int v) {
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

uint8_t viera_user_brightness_get(void) {
    /* Return the animated (current) brightness so renderers can show the fade. */
    return (uint8_t)atomic_get(&g_current_brt);
}

static void viera_user_brightness_set_target(uint8_t v) {
    v = clamp_0_100(v);
    atomic_set(&g_target_brt, v);
    /* Kick the fade worker immediately */
    (void)k_work_reschedule(&g_fade_work, K_NO_WAIT);
}

/* Include underglow API only when the feature is enabled */
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif


/* Fade worker: step current brightness toward target and refresh LEDs */
static void viera_brightness_fade_work(struct k_work *work) {
    ARG_UNUSED(work);

    int curr   = atomic_get(&g_current_brt);
    int target = atomic_get(&g_target_brt);

    if (curr == target) {
        return; /* Nothing to do */
    }

    int diff = target - curr;
    /* Compute step so we roughly complete in VIERA_FADE_TOTAL_MS */
    int steps     = MAX(1, VIERA_FADE_TOTAL_MS / VIERA_FADE_STEP_MS);
    int step_mag  = MAX(1, ABS(diff) / steps);
    int next      = curr + (diff > 0 ? step_mag : -step_mag);

    /* Clamp and avoid overshoot */
    if ((diff > 0 && next > target) || (diff < 0 && next < target)) {
        next = target;
    }

    atomic_set(&g_current_brt, clamp_0_100(next));

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    zmk_rgb_underglow_request_refresh();
#endif

    if (next != target) {
        (void)k_work_reschedule(&g_fade_work, K_MSEC(VIERA_FADE_STEP_MS));
    }
}

void viera_on_brightness_changed(uint8_t level) {
    viera_user_brightness_set_target(level);
}

static int viera_led_bridge_init(void) {
    k_work_init_delayable(&g_fade_work, viera_brightness_fade_work);
    return 0;
}
SYS_INIT(viera_led_bridge_init, APPLICATION, 50);