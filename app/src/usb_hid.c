/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/usb.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

extern void viera_on_backlight_power_changed(uint8_t on);
extern void viera_on_brightness_changed(uint8_t level);
extern void viera_on_effect_changed(uint8_t effect);
extern void viera_on_color_changed(uint8_t r, uint8_t g, uint8_t b);
extern void viera_on_speed_changed(uint8_t speed);
extern int viera_get_power(uint8_t *out);
extern int viera_get_brightness(uint8_t *out);
extern int viera_get_effect(uint8_t *out);
extern int viera_get_color(uint8_t out[3]);
extern int viera_get_speed(uint8_t *out);

#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
#include <zmk/pointing/resolution_multipliers.h>
#endif // IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

#include <zmk/event_manager.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *hid_dev;

static K_SEM_DEFINE(hid_sem, 1, 1);

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_sem); }

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK 0x00ff

#define HID_REPORT_TYPE_INPUT 0x100
#define HID_REPORT_TYPE_OUTPUT 0x200
#define HID_REPORT_TYPE_FEATURE 0x300

struct viera_feature_report_1 {
    uint8_t report_id;
    uint8_t value;
} __packed;

struct viera_feature_report_3 {
    uint8_t report_id;
    uint8_t value[3];
} __packed;

static void hsb_to_rgb(uint8_t h, uint8_t s, uint8_t b, uint8_t *r, uint8_t *g, uint8_t *bl) {
    int hue = (h * 359) / 255;
    int sat = s > 100 ? 100 : s;
    int bri = b > 100 ? 100 : b;
    int v = (bri * 255) / 100;

    if (sat == 0) {
        *r = (uint8_t)v;
        *g = (uint8_t)v;
        *bl = (uint8_t)v;
        return;
    }

    int region = hue / 60;
    int remainder = (hue - (region * 60)) * 255 / 60;
    int p = (v * (255 - sat * 255 / 100)) / 255;
    int q = (v * (255 - (sat * remainder) / 100)) / 255;
    int t = (v * (255 - (sat * (255 - remainder)) / 100)) / 255;

    switch (region) {
    case 0:
        *r = (uint8_t)v;
        *g = (uint8_t)t;
        *bl = (uint8_t)p;
        break;
    case 1:
        *r = (uint8_t)q;
        *g = (uint8_t)v;
        *bl = (uint8_t)p;
        break;
    case 2:
        *r = (uint8_t)p;
        *g = (uint8_t)v;
        *bl = (uint8_t)t;
        break;
    case 3:
        *r = (uint8_t)p;
        *g = (uint8_t)q;
        *bl = (uint8_t)v;
        break;
    case 4:
        *r = (uint8_t)t;
        *g = (uint8_t)p;
        *bl = (uint8_t)v;
        break;
    default:
        *r = (uint8_t)v;
        *g = (uint8_t)p;
        *bl = (uint8_t)q;
        break;
    }
}

static void rgb_to_hsb(uint8_t r, uint8_t g, uint8_t b, uint8_t *h, uint8_t *s, uint8_t *br) {
    int max_c = MAX(r, MAX(g, b));
    int min_c = MIN(r, MIN(g, b));
    int delta = max_c - min_c;
    int hue = 0;

    *br = (uint8_t)((max_c * 100) / 255);
    if (max_c == 0 || delta == 0) {
        *s = 0;
        *h = 0;
        return;
    }

    *s = (uint8_t)((delta * 100) / max_c);

    if (max_c == r) {
        hue = ((g - b) * 60) / delta;
        if (hue < 0) {
            hue += 360;
        }
    } else if (max_c == g) {
        hue = 120 + ((b - r) * 60) / delta;
    } else {
        hue = 240 + ((r - g) * 60) / delta;
    }

    *h = (uint8_t)((hue * 255) / 359);
}

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static uint8_t hid_protocol = HID_PROTOCOL_REPORT;

static void set_proto_cb(const struct device *dev, uint8_t protocol) { hid_protocol = protocol; }

void zmk_usb_hid_set_protocol(uint8_t protocol) { hid_protocol = protocol; }
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

static uint8_t *get_keyboard_report(size_t *len) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol != HID_PROTOCOL_REPORT) {
        zmk_hid_boot_report_t *boot_report = zmk_hid_get_boot_report();
        *len = sizeof(*boot_report);
        return (uint8_t *)boot_report;
    }
#endif
    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    *len = sizeof(*report);
    return (uint8_t *)report;
}

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    switch (setup->wValue & HID_GET_REPORT_TYPE_MASK) {
    case HID_REPORT_TYPE_FEATURE:
        switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
        case ZMK_HID_REPORT_ID_VIERA_POWER: {
            static struct viera_feature_report_1 report = {.report_id = ZMK_HID_REPORT_ID_VIERA_POWER};
            uint8_t value = 0;
            if (viera_get_power(&value) != 0) {
                value = 0;
            }
            report.value = value > 0 ? 1 : 0;
            *len = sizeof(report);
            *data = (uint8_t *)&report;
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_BRIGHTNESS: {
            static struct viera_feature_report_1 report = {
                .report_id = ZMK_HID_REPORT_ID_VIERA_BRIGHTNESS};
            uint8_t value = 0;
            if (viera_get_brightness(&value) != 0) {
                value = 0;
            }
            report.value = MIN(value, 100);
            *len = sizeof(report);
            *data = (uint8_t *)&report;
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_RGB: {
            static struct viera_feature_report_3 report = {.report_id = ZMK_HID_REPORT_ID_VIERA_RGB};
            uint8_t rgb[3] = {0, 0, 0};
            (void)viera_get_color(rgb);
            report.value[0] = rgb[0];
            report.value[1] = rgb[1];
            report.value[2] = rgb[2];
            *len = sizeof(report);
            *data = (uint8_t *)&report;
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_HSB: {
            static struct viera_feature_report_3 report = {.report_id = ZMK_HID_REPORT_ID_VIERA_HSB};
            uint8_t rgb[3] = {0, 0, 0};
            uint8_t brightness = 0;
            (void)viera_get_color(rgb);
            if (viera_get_brightness(&brightness) != 0) {
                brightness = 0;
            }
            rgb_to_hsb(rgb[0], rgb[1], rgb[2], &report.value[0], &report.value[1], &report.value[2]);
            report.value[2] = MIN(brightness, 100);
            *len = sizeof(report);
            *data = (uint8_t *)&report;
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_EFFECT: {
            static struct viera_feature_report_1 report = {.report_id = ZMK_HID_REPORT_ID_VIERA_EFFECT};
            uint8_t value = 0;
            if (viera_get_effect(&value) != 0) {
                value = 0;
            }
            report.value = value;
            *len = sizeof(report);
            *data = (uint8_t *)&report;
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_EFFECT_SPEED: {
            static struct viera_feature_report_1 report = {
                .report_id = ZMK_HID_REPORT_ID_VIERA_EFFECT_SPEED};
            uint8_t value = 1;
            if (viera_get_speed(&value) != 0) {
                value = 1;
            }
            report.value = CLAMP(value, 1, 5);
            *len = sizeof(report);
            *data = (uint8_t *)&report;
            break;
        }
#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
        case ZMK_HID_REPORT_ID_MOUSE:
            static struct zmk_hid_mouse_resolution_feature_report res_feature_report;

            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_USB,
            };

            *len = sizeof(struct zmk_hid_mouse_resolution_feature_report);
            struct zmk_pointing_resolution_multipliers mult =
                zmk_pointing_resolution_multipliers_get_profile(endpoint);

            res_feature_report.body.wheel_res = mult.wheel;
            res_feature_report.body.hwheel_res = mult.hor_wheel;
            *data = (uint8_t *)&res_feature_report;
            break;
#endif // IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
        default:
            return -ENOTSUP;
        }
        break;
    case HID_REPORT_TYPE_INPUT:
        switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
        case ZMK_HID_REPORT_ID_KEYBOARD: {
            size_t size;
            *data = get_keyboard_report(&size);
            *len = (int32_t)size;
            break;
        }
        case ZMK_HID_REPORT_ID_CONSUMER: {
            struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
            *data = (uint8_t *)report;
            *len = sizeof(*report);
            break;
        }
        default:
            LOG_ERR("Invalid report ID %d requested", setup->wValue & HID_GET_REPORT_ID_MASK);
            return -EINVAL;
        }
        break;
    default:
        /*
         * 7.2.1 of the HID v1.11 spec is unclear about handling requests for reports that do not
         * exist For requested reports that aren't input reports, return -ENOTSUP like the Zephyr
         * subsys does
         */
        LOG_ERR("Unsupported report type %d requested", (setup->wValue & HID_GET_REPORT_TYPE_MASK)
                                                            << 8);
        return -ENOTSUP;
    }

    return 0;
}

static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    switch (setup->wValue & HID_GET_REPORT_TYPE_MASK) {
    case HID_REPORT_TYPE_FEATURE:
        switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
        case ZMK_HID_REPORT_ID_VIERA_POWER: {
            if (*len != sizeof(struct viera_feature_report_1)) {
                return -EINVAL;
            }
            struct viera_feature_report_1 *report = (struct viera_feature_report_1 *)*data;
            viera_on_backlight_power_changed(report->value > 0 ? 1 : 0);
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_BRIGHTNESS: {
            if (*len != sizeof(struct viera_feature_report_1)) {
                return -EINVAL;
            }
            struct viera_feature_report_1 *report = (struct viera_feature_report_1 *)*data;
            viera_on_brightness_changed(MIN(report->value, 100));
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_RGB: {
            if (*len != sizeof(struct viera_feature_report_3)) {
                return -EINVAL;
            }
            struct viera_feature_report_3 *report = (struct viera_feature_report_3 *)*data;
            viera_on_color_changed(report->value[0], report->value[1], report->value[2]);
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_HSB: {
            if (*len != sizeof(struct viera_feature_report_3)) {
                return -EINVAL;
            }
            struct viera_feature_report_3 *report = (struct viera_feature_report_3 *)*data;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            hsb_to_rgb(report->value[0], report->value[1], report->value[2], &r, &g, &b);
            viera_on_color_changed(r, g, b);
            viera_on_brightness_changed(MIN(report->value[2], 100));
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_EFFECT: {
            if (*len != sizeof(struct viera_feature_report_1)) {
                return -EINVAL;
            }
            struct viera_feature_report_1 *report = (struct viera_feature_report_1 *)*data;
            viera_on_effect_changed(report->value);
            break;
        }
        case ZMK_HID_REPORT_ID_VIERA_EFFECT_SPEED: {
            if (*len != sizeof(struct viera_feature_report_1)) {
                return -EINVAL;
            }
            struct viera_feature_report_1 *report = (struct viera_feature_report_1 *)*data;
            viera_on_speed_changed(CLAMP(report->value, 1, 5));
            break;
        }
#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
        case ZMK_HID_REPORT_ID_MOUSE:
            if (*len != sizeof(struct zmk_hid_mouse_resolution_feature_report)) {
                return -EINVAL;
            }

            struct zmk_hid_mouse_resolution_feature_report *report =
                (struct zmk_hid_mouse_resolution_feature_report *)*data;
            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_USB,
            };

            zmk_pointing_resolution_multipliers_process_report(&report->body, endpoint);

            break;
#endif // IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
        default:
            return -ENOTSUP;
        }
        break;

    case HID_REPORT_TYPE_OUTPUT:
        switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
        case ZMK_HID_REPORT_ID_LEDS:
            if (*len != sizeof(struct zmk_hid_led_report)) {
                LOG_ERR("LED set report is malformed: length=%d", *len);
                return -EINVAL;
            } else {
                struct zmk_hid_led_report *report = (struct zmk_hid_led_report *)*data;
                struct zmk_endpoint_instance endpoint = {
                    .transport = ZMK_TRANSPORT_USB,
                };
                zmk_hid_indicators_process_report(&report->body, endpoint);
            }
            break;
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
        default:
            LOG_ERR("Invalid report ID %d requested", setup->wValue & HID_GET_REPORT_ID_MASK);
            return -EINVAL;
        }
        break;
    default:
        LOG_ERR("Unsupported report type %d requested",
                (setup->wValue & HID_GET_REPORT_TYPE_MASK) >> 8);
        return -ENOTSUP;
    }

    return 0;
}

static const struct hid_ops ops = {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    .protocol_change = set_proto_cb,
#endif
    .int_in_ready = in_ready_cb,
    .get_report = get_report_cb,
    .set_report = set_report_cb,
};

static int zmk_usb_hid_send_report(const uint8_t *report, size_t len) {
    switch (zmk_usb_get_status()) {
    case USB_DC_SUSPEND:
        return usb_wakeup_request();
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    default:
        k_sem_take(&hid_sem, K_MSEC(30));
        int err = hid_int_ep_write(hid_dev, report, len, NULL);

        if (err) {
            k_sem_give(&hid_sem);
        }

        return err;
    }
}

int zmk_usb_hid_send_keyboard_report(void) {
    size_t len;
    uint8_t *report = get_keyboard_report(&len);
    return zmk_usb_hid_send_report(report, len);
}

int zmk_usb_hid_send_consumer_report(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

    struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
    return zmk_usb_hid_send_report((uint8_t *)report, sizeof(*report));
}

#if IS_ENABLED(CONFIG_ZMK_POINTING)
int zmk_usb_hid_send_mouse_report() {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

    struct zmk_hid_mouse_report *report = zmk_hid_get_mouse_report();
    return zmk_usb_hid_send_report((uint8_t *)report, sizeof(*report));
}
#endif // IS_ENABLED(CONFIG_ZMK_POINTING)

static int zmk_usb_hid_init(void) {
    hid_dev = device_get_binding("HID_0");
    if (hid_dev == NULL) {
        LOG_ERR("Unable to locate HID device");
        return -EINVAL;
    }

    usb_hid_register_device(hid_dev, zmk_hid_report_desc, sizeof(zmk_hid_report_desc), &ops);

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    usb_hid_set_proto_code(hid_dev, HID_BOOT_IFACE_CODE_KEYBOARD);
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

    usb_hid_init(hid_dev);

    return 0;
}

SYS_INIT(zmk_usb_hid_init, APPLICATION, CONFIG_ZMK_USB_HID_INIT_PRIORITY);
