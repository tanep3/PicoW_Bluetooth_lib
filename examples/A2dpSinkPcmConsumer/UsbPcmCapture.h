#pragma once
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "tusb.h"
#include <cstring>
// Main-loop-owned TinyUSB (explicit tinyusb_device link disables SDK IRQ
// worker). No allocation or blocking: whole packets queued or counted lost;
// partial USB writes retain their offset. stdout disabled throughout binary
// transmission.
class UsbPcmCapture {
  static constexpr unsigned slots = 32, max_payload = 1024, header = 32;
  struct Packet {
    uint8_t data[header + max_payload + 4];
    unsigned size = 0, offset = 0;
  };
  Packet queue[slots];
  unsigned head = 0, count = 0;
  uint32_t seq = 0;
  uint32_t crc_table[256]{};
  uint32_t rate = 0, channels = 0, epoch = 0, frames = 0, lost = 0, reason = 0;
  uint32_t started = 0, baseline_dropped = 0, baseline_failures = 0;
  bool recording = false, finishing = false, end_queued = false;
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = uint8_t(v >> (8 * i));
  }
  void packet(unsigned type, const uint8_t *data, unsigned size,
              unsigned nframes) {
    const uint32_t id = seq++;
    if (count == slots) {
      lost += nframes;
      return;
    }
    auto &p = queue[(head + count) % slots];
    std::memcpy(p.data, "PCMUSB01", 8);
    put(p.data + 8, type);
    put(p.data + 12, id);
    put(p.data + 16, size);
    put(p.data + 20, rate);
    put(p.data + 24, channels);
    put(p.data + 28, epoch);
    if (size)
      std::memcpy(p.data + header, data, size);
    uint32_t crc = ~0u;
    for (unsigned i = 0; i < header + size; ++i)
      crc = crc_table[(crc ^ p.data[i]) & 255] ^ (crc >> 8);
    put(p.data + header + size, ~crc);
    p.size = header + size + 4;
    p.offset = 0;
    ++count;
  }

public:
  UsbPcmCapture() {
    for (unsigned n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (unsigned k = 0; k < 8; ++k)
        c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
      crc_table[n] = c;
    }
  }
  bool busy() const { return recording || finishing; }
  bool start(uint32_t r, unsigned ch, uint32_t ep, uint32_t now, uint32_t drops,
             uint32_t failures) {
    if (busy() || (r != 16000 && r != 32000 && r != 44100 && r != 48000) ||
        !ch || ch > 2)
      return false;
    rate = r;
    channels = ch;
    epoch = ep;
    started = now;
    baseline_dropped = drops;
    baseline_failures = failures;
    frames = lost = reason = seq = 0;
    head = count = 0;
    end_queued = false;
    recording = true;
    stdio_set_driver_enabled(&stdio_usb, false);
    packet(1, nullptr, 0, 0);
    return true;
  }
  void consume(const int16_t *pcm, unsigned n, uint32_t r, unsigned ch,
               uint32_t ep) {
    if (!recording)
      return;
    if (r != rate || ch != channels || ep != epoch) {
      reason = 1;
      recording = false;
      finishing = true;
      return;
    }
    unsigned left = rate * 5 - frames;
    if (n > left)
      n = left;
    while (n) {
      unsigned k = n;
      if (k > max_payload / (channels * 2))
        k = max_payload / (channels * 2);
      packet(2, reinterpret_cast<const uint8_t *>(pcm), k * channels * 2, k);
      frames += k;
      pcm += k * channels;
      n -= k;
    }
    if (frames == rate * 5) {
      recording = false;
      finishing = true;
    }
  }
  void poll(uint32_t now, bool streaming, uint32_t drops, uint32_t failures) {
    if (recording && (!streaming || uint32_t(now - started) > 7000)) {
      reason = streaming ? 2 : 3;
      recording = false;
      finishing = true;
    }
    if (finishing && !end_queued && count < slots) {
      uint8_t end[32];
      put(end, frames);
      put(end + 4, lost);
      put(end + 8, drops - baseline_dropped);
      put(end + 12, failures - baseline_failures);
      put(end + 16, reason);
      put(end + 20, now - started);
      put(end + 24, rate * 5);
      put(end + 28, seq);
      packet(3, end, sizeof end, 0);
      end_queued = true;
    }
    // Fill available USB space across packet boundaries; never spend a
    // whole poll on a tiny previous-packet tail. Both work and memory bound.
    unsigned budget = 4096;
    while (count && budget && tud_cdc_connected()) {
      auto &p = queue[head];
      unsigned n = tud_cdc_write_available();
      if (!n)
        break;
      if (n > p.size - p.offset)
        n = p.size - p.offset;
      if (n > budget)
        n = budget;
      const unsigned written = tud_cdc_write(p.data + p.offset, n);
      if (!written)
        break;
      p.offset += written;
      budget -= written;
      if (p.offset == p.size) {
        head = (head + 1) % slots;
        --count;
      }
    }
    tud_cdc_write_flush();
    if (finishing && end_queued && !count) {
      finishing = false;
      stdio_set_driver_enabled(&stdio_usb, true);
    }
    // USB host failure is bounded too. Missing END makes host reject capture.
    if (busy() && uint32_t(now - started) > 12000) {
      recording = finishing = false;
      head = count = 0;
      stdio_set_driver_enabled(&stdio_usb, true);
    }
  }
};
