#pragma once

#include "SX12xxDriverCommon.h"
#include <targets.h>

class RFAMP_hal
{
public:
    static RFAMP_hal *instance;

    RFAMP_hal();

    void init();
    void ICACHE_RAM_ATTR TXenable(SX12XX_Radio_Number_t radioNumber);
    void ICACHE_RAM_ATTR RXenable();
    void ICACHE_RAM_ATTR TXRXdisable();

private:
    bool rx_enabled;
    bool tx_enabled;
};