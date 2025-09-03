// SPDX-License-Identifier: MIT
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h> // common in behaviors
#include <zmk/caps_rgb_ctrl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int bl_tog_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    bool on = caps_rgb_get_backlight();
    caps_rgb_set_backlight(!on);
    return 0;
}

static int bl_tog_keymap_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    return 0;
}

static const struct behavior_driver_api bl_tog_driver_api = {
    .binding_pressed = bl_tog_keymap_binding_pressed,
    .binding_released = bl_tog_keymap_binding_released,
};

static int bl_tog_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

#define INST(n)                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, bl_tog_init, NULL, NULL, NULL, APPLICATION,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &bl_tog_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INST)