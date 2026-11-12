#pragma once
#include <cstdint>
using bd_addr_t = uint8_t[6];
void gap_discoverable_control(uint8_t enabled);
void gap_local_bd_addr(bd_addr_t address);
char *bd_addr_to_str(const bd_addr_t address);
