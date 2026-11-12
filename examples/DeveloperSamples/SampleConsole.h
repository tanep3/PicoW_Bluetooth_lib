#pragma once
#include "SamplePolicy.h"
#include "sample_config.h"
#include "pico/stdlib.h"
#include <cstdio>
namespace sample {
// Default USB CDC. Optional UART0: GP0 TX / GP1 RX, 115200 baud, 3.3 V.
inline bool start(Address& peer) {
  stdio_init_all();
  if (!parsePeer(SAMPLE_PEER, peer)) {
    puts("Set SAMPLE_PEER to your peer address and rebuild; radio stays off.");
    return false;
  }
  puts("b=start Bluetooth; no radio before b. ?=status; reset board to restart.");
  while (getchar_timeout_us(0) != 'b') tight_loop_contents();
  return true;
}
inline void idle() { while (true) tight_loop_contents(); }
}
