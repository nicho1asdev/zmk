// SPDX-License-Identifier: MIT
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(viera_led_bridge, CONFIG_LOG_DEFAULT_LEVEL);

/* ---- Master user brightness state (0..100) ---- */
static atomic_t g_user_brt = ATOMIC_INIT(80);
uint8_t viera_user_brightness_get(void) { return (uint8_t)atomic_get(&g_user_brt); }
void    viera_user_brightness_set(uint8_t v) { if (v > 100) v = 100; atomic_set(&g_user_brt, v); }

/* Include underglow API only when the feature is enabled */
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif

void viera_on_brightness_changed(uint8_t level) {
    if (level > 100) level = 100;
    viera_user_brightness_set(level);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    zmk_rgb_underglow_set_brt(level);
    zmk_rgb_underglow_on();
#else
    ARG_UNUSED(level);
    LOG_DBG("Underglow API not present; using master brightness only");
#endif
}