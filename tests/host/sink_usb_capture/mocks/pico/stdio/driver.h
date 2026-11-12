#pragma once
inline bool console_enabled = true;
inline void stdio_set_driver_enabled(int *, bool value) { console_enabled = value; }
