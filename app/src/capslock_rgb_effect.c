/*
 * Switch RGB underglow based on Caps Lock state (modern ZMK API).
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/sys/util.h>

#ifndef ZMK_LED_NUMLOCK_BIT
#define ZMK_LED_NUMLOCK_BIT  BIT(0)
#endif
#ifndef ZMK_LED_CAPSLOCK_BIT
#define ZMK_LED_CAPSLOCK_BIT BIT(1)
#endif
#ifndef ZMK_LED_SCROLLLOCK_BIT
#define ZMK_LED_SCROLLLOCK_BIT BIT(2)
#endif

static bool s_backlight_on = true;

static void apply_caps_state(bool caps_on) {
    if (!zmk_rgb_underglow_is_on()) {
        return;
    }

    if (!s_backlight_on) {
        zmk_rgb_underglow_set_brightness(0);
        return;
    }

    if (caps_on) {
        zmk_rgb_underglow_set_color(255, 255, 255);
        zmk_rgb_underglow_set_brightness(100);
    } else {
        zmk_rgb_underglow_set_brightness(0);
    }
}

static void apply_from_flags(uint8_t flags) {
    const bool caps_on = (flags & ZMK_LED_CAPSLOCK_BIT);
    apply_caps_state(caps_on);
}

static int capslock_rgb_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev =
        as_zmk_hid_indicators_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    apply_caps_state(ev->indicators.caps_lock);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(capslock_rgb, capslock_rgb_listener);
ZMK_SUBSCRIPTION(capslock_rgb, zmk_hid_indicators_changed);

void caps_rgb_set_backlight(bool on) {
    s_backlight_on = on;
}

bool caps_rgb_get_backlight(void) {
    return s_backlight_on;
}

void caps_rgb_apply_from_flags(uint8_t flags) {
    apply_from_flags(flags);
}

static int capslock_rgb_init(void) {
    return 0;
}
SYS_INIT(capslock_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);