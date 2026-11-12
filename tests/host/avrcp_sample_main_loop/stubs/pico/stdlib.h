#pragma once
#include <cstdint>
constexpr int PICO_ERROR_TIMEOUT = -1;
void stdio_init_all(); int getchar_timeout_us(uint32_t); void sleep_ms(uint32_t);
uint32_t get_absolute_time(); inline uint32_t to_ms_since_boot(uint32_t t) { return t; }
