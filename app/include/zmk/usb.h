/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/keys.h>
#include <zmk/hid.h>

enum zmk_usb_conn_state {
    ZMK_USB_CONN_NONE,
    ZMK_USB_CONN_POWERED,
    ZMK_USB_CONN_HID,
};

enum usb_dc_status_code zmk_usb_get_status(void);
enum zmk_usb_conn_state zmk_usb_get_conn_state(void);

static inline bool zmk_usb_is_powered(void) {
    return zmk_usb_get_conn_state() != ZMK_USB_CONN_NONE;
}

/**
 * USB HID "active" / data path ready (used for output routing and exclusive-transport policy).
 *
 * True when the controller has seen USB_DC_CONFIGURED while in the HID stack's
 * connected state: ZMK_USB_CONN_HID (Zephyr USB_DC_CONFIGURED, SUSPEND, RESUME,
 * CLEAR_HALT, SOF). Implies the HID interface is configured, not merely VBUS or
 * charge-only (ZMK_USB_CONN_POWERED without configuration is not HID-ready).
 *
 * After reset/disconnect, configuration must be seen again before HID is ready.
 */
bool zmk_usb_is_hid_ready(void);
