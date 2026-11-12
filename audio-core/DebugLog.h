// Lightweight, configurable logging and sleep macros for PicoW Bluetooth library
#pragma once

#include <cstdio>
#include "pico/stdlib.h"

// Sink selection (compile-time):
// - Define PWB_LOG_USE_DISPLAY to route logs to Lovyan display (display.display_msg)
// - Define PWB_LOG_USE_SERIAL to route logs to stdio (printf)
// - If neither is defined, logs are disabled (no-op)

// Sleep control:
// - Define PWB_SLEEP_DISABLED to make sleep macro a no-op

#if defined(PWB_LOG_USE_DISPLAY)
#include "display_ST7735S.h"
// Expect a global/object named `display` with display_msg(const char*)
#define PWB_LOG_MSG(msg) do { display.display_msg(msg); } while(0)
#elif defined(PWB_LOG_USE_SERIAL)
#define PWB_LOG_MSG(msg) do { printf("%s\n", msg); } while(0)
#else
#define PWB_LOG_MSG(msg) do {} while(0)
#endif

#if defined(PWB_SLEEP_DISABLED)
#define PWB_SLEEP_MS(ms) do {} while(0)
#else
#define PWB_SLEEP_MS(ms) do { sleep_ms(ms); } while(0)
#endif

