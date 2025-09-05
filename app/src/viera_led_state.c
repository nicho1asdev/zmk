#include <zephyr/sys/atomic.h>
#include "viera_led_state.h"

static atomic_t g_user_brt = ATOMIC_INIT(80); // 0..100
uint8_t viera_user_brightness_get(void) { return (uint8_t)atomic_get(&g_user_brt); }
void viera_user_brightness_set(uint8_t v) { if (v > 100) v = 100; atomic_set(&g_user_brt, v); }

void viera_on_brightness_changed(uint8_t level) {
    if (level > 100) level = 100;
    viera_user_brightness_set(level);
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
    zmk_rgb_underglow_set_brt(level);
    zmk_rgb_underglow_resume();
#endif
}