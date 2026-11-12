#pragma once

#include "btstack.h"

class IAddressConnectable {
public:
    virtual void connect(bd_addr_t addr) = 0;
};
