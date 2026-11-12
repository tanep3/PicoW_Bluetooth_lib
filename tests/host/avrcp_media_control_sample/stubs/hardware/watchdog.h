#pragma once
#include "Model.h"
inline void watchdog_enable(uint32_t t,bool p){model::mutation();assert(t==5000&&p);++model::watchdogs;}
inline void watchdog_update(){model::mutation();}
