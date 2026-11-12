#pragma once
#include <cstdint>
using absolute_time_t = uint32_t;
void stdio_init_all();
absolute_time_t get_absolute_time();
uint32_t to_ms_since_boot(absolute_time_t time);
int getchar_timeout_us(uint32_t timeout);
void tight_loop_contents();
