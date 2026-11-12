#pragma once
#include <cstdint>
#include <vector>
inline bool connected = true;
inline unsigned available = 4096;
inline unsigned short_write = 4096;
inline std::vector<uint8_t> wire;
inline bool tud_cdc_connected() { return connected; }
inline unsigned tud_cdc_write_available() { return available; }
inline unsigned tud_cdc_write(const void *p, unsigned n) {
    if (n > short_write) n = short_write;
    const auto *bytes = static_cast<const uint8_t *>(p);
    wire.insert(wire.end(), bytes, bytes + n);
    return n;
}
inline void tud_cdc_write_flush() {}
inline unsigned usb_inits = 0, usb_tasks = 0, rx_flushes = 0;
inline int pending_command = -1;
inline void tusb_init() { ++usb_inits; }
inline void tud_task() { ++usb_tasks; }
inline void tud_cdc_read_flush() { ++rx_flushes; pending_command = -1; }
