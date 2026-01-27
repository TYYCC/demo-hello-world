#ifndef UNIT_TEST

#include "RFAMP_hal.h"
#include "logging.h"
#include "soc/gpio_struct.h"

RFAMP_hal *RFAMP_hal::instance = nullptr;

RFAMP_hal::RFAMP_hal()
{
    instance = this;
}

void RFAMP_hal::init()
{
    DBGLN("RFAMP_hal Init");

    rx_enabled = false;
    tx_enabled = false;

    if (GPIO_PIN_PA_ENABLE != UNDEF_PIN)
    {
        DBGLN("Use PA enable pin: %d", GPIO_PIN_PA_ENABLE);
        pinMode(GPIO_PIN_PA_ENABLE, OUTPUT);
        digitalWrite(GPIO_PIN_PA_ENABLE, LOW);
    }

    if (GPIO_PIN_TX_ENABLE != UNDEF_PIN)
    {
        DBGLN("Use TX pin: %d", GPIO_PIN_TX_ENABLE);
        pinMode(GPIO_PIN_TX_ENABLE, OUTPUT);
        digitalWrite(GPIO_PIN_TX_ENABLE, LOW);
    }

    if (GPIO_PIN_RX_ENABLE != UNDEF_PIN)
    {
        DBGLN("Use RX pin: %d", GPIO_PIN_RX_ENABLE);
        pinMode(GPIO_PIN_RX_ENABLE, OUTPUT);
        digitalWrite(GPIO_PIN_RX_ENABLE, LOW);
    }

    if (GPIO_PIN_TX_ENABLE_2 != UNDEF_PIN)
    {
        DBGLN("Use TX_2 pin: %d", GPIO_PIN_TX_ENABLE_2);
        pinMode(GPIO_PIN_TX_ENABLE_2, OUTPUT);
        digitalWrite(GPIO_PIN_TX_ENABLE_2, LOW);
    }

    if (GPIO_PIN_RX_ENABLE_2 != UNDEF_PIN)
    {
        DBGLN("Use RX_2 pin: %d", GPIO_PIN_RX_ENABLE_2);
        pinMode(GPIO_PIN_RX_ENABLE_2, OUTPUT);
        digitalWrite(GPIO_PIN_RX_ENABLE_2, LOW);
    }
}

void ICACHE_RAM_ATTR RFAMP_hal::TXenable(SX12XX_Radio_Number_t radioNumber)
{
    DBGLN("TXenable entered");
#if defined(PLATFORM_ESP32_C3)
    if (radioNumber == SX12XX_Radio_All)
    {
        GPIO.out_w1ts.out_w1ts = tx_all_enable_set_bits;
        GPIO.out_w1tc.out_w1tc = tx_all_enable_clr_bits;
    }
    else if (radioNumber == SX12XX_Radio_2)
    {
        GPIO.out_w1ts.out_w1ts = tx2_enable_set_bits;
        GPIO.out_w1tc.out_w1tc = tx2_enable_clr_bits;
    }
    else
    {
        GPIO.out_w1ts.out_w1ts = tx1_enable_set_bits;
        GPIO.out_w1tc.out_w1tc = tx1_enable_clr_bits;
    }
#else
    if (!tx_enabled && !rx_enabled)
    {
        if (GPIO_PIN_PA_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_PA_ENABLE, HIGH);
        }
    }
    if (rx_enabled)
    {
        if (GPIO_PIN_RX_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_RX_ENABLE, LOW);
        }
        rx_enabled = false;
    }
    if (!tx_enabled)
    {
        if (GPIO_PIN_TX_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_TX_ENABLE, HIGH);
        }
        tx_enabled = true;
    }
#endif
}

void ICACHE_RAM_ATTR RFAMP_hal::RXenable()
{
    DBGLN("RXenable entered");
#if defined(PLATFORM_ESP32_C3)
    GPIO.out_w1ts.out_w1ts = rx_enable_set_bits;
    GPIO.out_w1tc.out_w1tc = rx_enable_clr_bits;
#else
    if (!rx_enabled)
    {
        if (!tx_enabled && GPIO_PIN_PA_ENABLE != UNDEF_PIN)
            digitalWrite(GPIO_PIN_PA_ENABLE, HIGH);

        if (tx_enabled && GPIO_PIN_TX_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_TX_ENABLE, LOW);
            tx_enabled = false;
        }

        if (GPIO_PIN_RX_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_RX_ENABLE, HIGH);
        }
        rx_enabled = true;
    }
#endif
}

void ICACHE_RAM_ATTR RFAMP_hal::TXRXdisable()
{
#if defined(PLATFORM_ESP32_C3)
    GPIO.out_w1tc.out_w1tc = txrx_disable_clr_bits;
#else
    if (rx_enabled)
    {
        if (GPIO_PIN_RX_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_RX_ENABLE, LOW);
        }
        rx_enabled = false;
    }
    if (tx_enabled)
    {
        if (GPIO_PIN_PA_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_PA_ENABLE, LOW);
        }
        if (GPIO_PIN_TX_ENABLE != UNDEF_PIN)
        {
            digitalWrite(GPIO_PIN_TX_ENABLE, LOW);
        }
        tx_enabled = false;
    }
#endif
}

#endif // UNIT_TEST
