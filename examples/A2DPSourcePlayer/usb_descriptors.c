#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"
// Pico SDK CDC VID/PID plus the SDK-standard reset interface (picotool).
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0x2e8a,
    .idProduct = 0x000a, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
static const uint8_t config[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 9, 0, 250),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    9, TUSB_DESC_INTERFACE, 2, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
    RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, 5,
};
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return config; }
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t result[48];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    pico_get_unique_board_id_string(serial, sizeof(serial));
    const char *strings[] = {"", "Raspberry Pi", "PicoW A2DP Source Player", serial, "Player trace", "Reset"};
    unsigned n = 0;
    if (index == 0) { result[1] = 0x0409; n = 1; }
    else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
        while (n < 47 && strings[index][n]) { result[n + 1] = strings[index][n]; ++n; }
    }
    result[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    return result;
}
