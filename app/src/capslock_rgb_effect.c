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

#ifndef ZMK_LED_NUMLOCK_BIT
#define ZMK_LED_NUMLOCK_BIT  BIT(0)
#endif
#ifndef ZMK_LED_CAPSLOCK_BIT
#define ZMK_LED_CAPSLOCK_BIT BIT(1)
#endif
#ifndef ZMK_LED_SCROLLLOCK_BIT
#define ZMK_LED_SCROLLLOCK_BIT BIT(2)
#endif

#include <zmk/caps_rgb_ctrl.h>

static bool s_backlight_on = true;

static uint8_t select_effect(bool caps_on) {
    if (!s_backlight_on) {
        return caps_on ? CONFIG_ZMK_EFF_IDX_CAPS_ONLY : CONFIG_ZMK_EFF_IDX_ALL_OFF;
    } else {
        return caps_on ? CONFIG_ZMK_EFF_IDX_WHITE_EXCEPT_CAPS : CONFIG_ZMK_EFF_IDX_ALL_WHITE;
    }
}

static void apply_from_flags(zmk_hid_indicators_t flags) {
    const bool caps_on = (flags & ZMK_LED_CAPSLOCK_BIT);
    (void)zmk_rgb_underglow_select_effect(select_effect(caps_on));
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

void caps_rgb_set_backlight(bool on) {
    s_backlight_on = on;
    apply_from_flags(zmk_hid_indicators_get_current_profile());
}

bool caps_rgb_get_backlight(void) {
    return s_backlight_on;
}

void caps_rgb_apply_from_flags(zmk_hid_indicators_t flags) {
    apply_from_flags(flags);
}

/* Seed the correct effect on boot. */
static int capslock_rgb_init(void) {
    apply_from_flags(zmk_hid_indicators_get_current_profile());
    return 0;
}
SYS_INIT(capslock_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);