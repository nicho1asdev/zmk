#include "viera_led_state.h"
void viera_on_brightness_changed(uint8_t level) {
    if (level > 100) level = 100;
    viera_user_brightness_set(level);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    zmk_rgb_underglow_set_brt(level);
    zmk_rgb_underglow_resume();
#endif
}
// SPDX-License-Identifier: MIT
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(viera_led_bridge, CONFIG_LOG_DEFAULT_LEVEL);

#include "viera_led_state.h"

/* Detect if ZMK underglow public API header is available at build time */
#if __has_include(<zmk/rgb_underglow.h>)
#include <zmk/rgb_underglow.h>
#define VIERA_HAS_UNDERGLOW 1
#else
#define VIERA_HAS_UNDERGLOW 0
#endif

void viera_on_brightness_changed(uint8_t level) {
    if (level > 100) level = 100;
    viera_user_brightness_set(level);

#if VIERA_HAS_UNDERGLOW
    /* If underglow API is present in this ZMK build, update it as well */
    zmk_rgb_underglow_set_brt(level);
    zmk_rgb_underglow_resume();
#else
    /* No underglow API available; effects should read viera_user_brightness_get() */
    ARG_UNUSED(level);
    LOG_DBG("Underglow API not present; using master brightness only");
#endif
}