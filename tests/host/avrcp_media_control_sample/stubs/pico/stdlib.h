#pragma once
#include "Model.h"
constexpr int PICO_ERROR_TIMEOUT=-1;
inline uint32_t get_absolute_time(){return model::ms;}
inline uint32_t to_ms_since_boot(uint32_t t){return t;}
inline void stdio_init_all(){++model::stdios;}
inline int getchar_timeout_us(uint32_t t){assert(!t);++model::reads;if(model::on_read)model::on_read();return model::input_at<model::input.size()?static_cast<unsigned char>(model::input[model::input_at++]):PICO_ERROR_TIMEOUT;}
inline void sleep_ms(uint32_t t){assert(t==1&&model::reads<=16);model::reads=0;++model::ms;++model::sleeps;if(model::on_sleep)model::on_sleep();}
