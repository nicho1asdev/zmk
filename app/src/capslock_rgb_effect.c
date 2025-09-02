/*
 * Switch RGB underglow effect depending on Caps Lock LED state.
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/hid_indicators_types.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/sys/util.h> /* BIT() */

#include <stdint.h>
int  zmk_rgb_underglow_set_effect(uint8_t idx);
bool zmk_rgb_underglow_is_on(void);


#ifndef ZMK_LED_NUMLOCK_BIT
#define ZMK_LED_NUMLOCK_BIT  BIT(0)
#endif
#ifndef ZMK_LED_CAPSLOCK_BIT
#define ZMK_LED_CAPSLOCK_BIT BIT(1)
#endif
#ifndef ZMK_LED_SCROLLLOCK_BIT
#define ZMK_LED_SCROLLLOCK_BIT BIT(2)
#endif

/* Configure which effects to use (indices match ZMK's built-in list). */
#ifndef CONFIG_ZMK_CAPSLOCK_RGB_EFF_ON
#define CONFIG_ZMK_CAPSLOCK_RGB_EFF_ON  0 /* Solid */
#endif
#ifndef CONFIG_ZMK_CAPSLOCK_RGB_EFF_OFF
#define CONFIG_ZMK_CAPSLOCK_RGB_EFF_OFF 2 /* Spectrum */
#endif

static void apply_from_flags(zmk_hid_indicators_t flags) {
    const bool caps_on = (flags & ZMK_LED_CAPSLOCK_BIT);
    const uint8_t eff = caps_on ? 1
                                : 2;

    /* Set the effect regardless of power; do not change on/off state. */
    (void)zmk_rgb_underglow_set_effect(eff);
}

/* Listener callback: we ignore payload details and read the current profile. */
static int capslock_rgb_listener(const zmk_event_t *eh) {
    if (!as_zmk_hid_indicators_changed(eh)) {
        return 0; /* Not our event */
    }
    apply_from_flags(zmk_hid_indicators_get_current_profile());
    return 0;
}

ZMK_LISTENER(capslock_rgb, capslock_rgb_listener);
ZMK_SUBSCRIPTION(capslock_rgb, zmk_hid_indicators_changed);

/* Seed the correct effect on boot. */
static int capslock_rgb_init(const struct device *dev) {
    ARG_UNUSED(dev);
    apply_from_flags(zmk_hid_indicators_get_current_profile());
    return 0;
}
SYS_INIT(capslock_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);