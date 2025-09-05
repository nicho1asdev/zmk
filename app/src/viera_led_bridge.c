#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(viera_led_bridge, CONFIG_LOG_DEFAULT_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif

void viera_on_brightness_changed(uint8_t level) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    if (level > 100) level = 100;
    zmk_rgb_underglow_set_brt(level);
    zmk_rgb_underglow_resume();
    LOG_INF("Underglow brightness -> %u", level);
#else
    ARG_UNUSED(level);
    LOG_WRN("Underglow not enabled; ignoring brightness");
#endif
}