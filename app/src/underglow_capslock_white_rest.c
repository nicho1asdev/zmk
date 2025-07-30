#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk/underglow.h>

static int custom_effect_update(struct led_rgb *pixels, size_t count) {
    for (int i = 0; i < count; i++) {
        pixels[i].r = 255;
        pixels[i].g = 255;
        pixels[i].b = 255;
    }

    if (zmk_hid_caps_word_active() || zmk_hid_get_keyboard_leds() & HID_LED_CAPS_LOCK) {
        pixels[5].r = 0;
        pixels[5].g = 255;
        pixels[5].b = 0;
    }

    return 0;
}

ZMK_RGB_UNDERGLOW_EFFECT(custom_capslock_white, custom_effect_update);
