// SPDX-License-Identifier: MIT
// VIERA vendor BLE service for ZMK/Zephyr.

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <nrfx.h>

static bool viera_usb_vbus_present(void)
{
#if defined(NRF_POWER) && defined(POWER_USBREGSTATUS_VBUSDETECT_Msk)
    uint32_t s = NRF_POWER->USBREGSTATUS;
    return (s & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0u;
#else
    /* If USBREGSTATUS isn't available, assume present to avoid blocking updates on other SoCs. */
    return true;
#endif
}

LOG_MODULE_REGISTER(viera_gatt, CONFIG_VIERA_GATT_LOG_LEVEL);

/* -------- Configurable knobs (via prj.conf / Kconfig) -------- */

#ifndef CONFIG_VIERA_FW_VERSION
#define CONFIG_VIERA_FW_VERSION "0.1.0"
#endif

#ifndef CONFIG_VIERA_BOOT_GPREGRET_VALUE
/* Common value used by UF2 bootloaders to request bootloader on next reset. */
#define CONFIG_VIERA_BOOT_GPREGRET_VALUE 0x57
#endif

/* -------- UUIDs (must match the macOS app) --------
   Service:           8D81E3F5-3F2A-4C60-B3BE-6A9F9D9A8C77
   EnterBootloader:   3A9C9B0B-5A23-4F27-A5F8-2C34B8A8B0B1
   FirmwareVersion:   4B3A2F1A-77C9-4A4E-9D01-5E9C1F1B2C3D
   BacklightBrightness: 2E5E46B7-1B77-41E6-A3B3-DF2E7B7B5AA1
   BacklightEffect:   B9E1E31E-4C77-4B31-97F4-1B3D2B6C0AA2
--------------------------------------------------- */

static struct bt_uuid_128 VIERA_UUID_SVC =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8D81E3F5, 0x3F2A, 0x4C60, 0xB3BE, 0x6A9F9D9A8C77));

static struct bt_uuid_128 VIERA_UUID_ENTER =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x3A9C9B0B, 0x5A23, 0x4F27, 0xA5F8, 0x2C34B8A8B0B1));

static struct bt_uuid_128 VIERA_UUID_FWVER =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x4B3A2F1A, 0x77C9, 0x4A4E, 0x9D01, 0x5E9C1F1B2C3D));

static struct bt_uuid_128 VIERA_UUID_BRT =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x2E5E46B7, 0x1B77, 0x41E6, 0xA3B3, 0xDF2E7B7B5AA1));

static struct bt_uuid_128 VIERA_UUID_EFF =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xB9E1E31E, 0x4C77, 0x4B31, 0x97F4, 0x1B3D2B6C0AA2));

/* -------- Weak hooks you can override elsewhere -------- */
__weak void viera_on_brightness_changed(uint8_t level) { ARG_UNUSED(level); }
__weak void viera_on_effect_changed(uint8_t effect) { ARG_UNUSED(effect); }

/* Forward declaration for reboot function */
static void reboot_to_bootloader(void);

/* -------- Delayed work for reboot -------- */
static void bootloader_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    reboot_to_bootloader();
}

static K_WORK_DELAYABLE_DEFINE(boot_work, bootloader_work_handler);

/* -------- Characteristic handlers -------- */

static ssize_t fwver_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset)
{
    static const char fw[] = CONFIG_VIERA_FW_VERSION;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, fw, strlen(fw));
}

static void reboot_to_bootloader(void)
{
#if defined(NRF_POWER)
    /* Try both common UF2 magics on both GPREGRET registers */
    NRF_POWER->GPREGRET  = 0xB1; /* Adafruit DFU/UF2 */
#if defined(NRF_POWER_HAS_GPREGRET2)
    NRF_POWER->GPREGRET2 = 0x57; /* Alternate UF2 magic seen in the wild */
#endif
#endif

    /* Give the stack a moment to flush, then cold reboot */
    (void)bt_disable(NULL);
    k_msleep(50);
    sys_reboot(SYS_REBOOT_COLD);
}

/* In enter_write(), keep the VBUS check and bump the defer a bit */
if (p[0] == 0x01) {
    if (!viera_usb_vbus_present()) {
        LOG_WRN("Bootloader request ignored: USB not connected (no VBUS).");
        return len;
    }
    LOG_INF("Bootloader request received; USB present → rebooting to UF2.");
    k_work_schedule(&boot_work, K_MSEC(400)); /* was 300 ms */
}

static ssize_t enter_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    const uint8_t *p = buf;
    if (p[0] == 0x01) {
        if (!viera_usb_vbus_present()) {
            LOG_WRN("Bootloader request ignored: USB not connected (no VBUS).");
            return len;
        }
        LOG_INF("Bootloader request received; USB present → rebooting to UF2.");
        /* Defer reboot so the ATT write can complete cleanly. */
        k_work_schedule(&boot_work, K_MSEC(300));
    }
    return len;
}

static ssize_t brt_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(offset); ARG_UNUSED(flags);
    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t v = ((const uint8_t *)buf)[0];
    if (v > 100) v = 100;
    viera_on_brightness_changed(v);
    LOG_DBG("Brightness -> %u", v);
    return len;
}

static ssize_t eff_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(offset); ARG_UNUSED(flags);
    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t idx = ((const uint8_t *)buf)[0];
    viera_on_effect_changed(idx);
    LOG_DBG("Effect -> %u", idx);
    return len;
}

/* -------- GATT table -------- */

BT_GATT_SERVICE_DEFINE(viera_svc,
    BT_GATT_PRIMARY_SERVICE(&VIERA_UUID_SVC.uuid),

    /* Enter Bootloader: Write and Write Without Response, 1 byte */
    BT_GATT_CHARACTERISTIC(&VIERA_UUID_ENTER.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, enter_write, NULL),

    /* Firmware Version: Read, UTF-8 */
    BT_GATT_CHARACTERISTIC(&VIERA_UUID_FWVER.uuid,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, fwver_read, NULL, NULL),

    /* Backlight Brightness: Write, 0..100 */
    BT_GATT_CHARACTERISTIC(&VIERA_UUID_BRT.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, brt_write, NULL),

    /* Backlight Effect: Write, index */
    BT_GATT_CHARACTERISTIC(&VIERA_UUID_EFF.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, eff_write, NULL)
);