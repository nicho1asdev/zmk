#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/hid_indicators_types.h>
#include <zephyr/sys/util.h>

static bool s_backlight_on = true;

static void apply_caps_state(bool caps_on) {
    if (!s_backlight_on) {
        zmk_rgb_underglow_off();
        return;
    }

    if (caps_on) {
        zmk_rgb_underglow_on();
        zmk_rgb_underglow_set_hsb(0, 0, 100); // white
    } else {
        zmk_rgb_underglow_off();
    }
}

static int capslock_rgb_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev =
        as_zmk_hid_indicators_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool caps_on = ev->indicators & ZMK_LED_CAPSLOCK_BIT;
    apply_caps_state(caps_on);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(capslock_rgb, capslock_rgb_listener);
ZMK_SUBSCRIPTION(capslock_rgb, zmk_hid_indicators_changed);