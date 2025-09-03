

#pragma once

#include <stdbool.h>
#include <zephyr/types.h>
#include <zmk/hid_indicators_types.h>

/**
 * Control API for Caps/Backlight integrated RGB logic.
 */

/* Set the backlight state (true = on, false = off) */
void caps_rgb_set_backlight(bool on);

/* Query the current backlight state */
bool caps_rgb_get_backlight(void);

/* Apply the correct effect based on provided HID flags */
void caps_rgb_apply_from_flags(zmk_hid_indicators_t flags);