#include "driver_clock.h"
#include "driver_systick.h"
#include "driver_gpio.h"
#include "driver_usb.h"

#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_NO_13

int main(void)
{
    /*
     *  1. Configure PLL: HSE 25 MHz -> SYSCLK 96 MHz, USB 48 MHz
     *
     *  VCO_in  = 25/25   = 1 MHz
     *  VCO_out = 1*192   = 192 MHz
     *  SYSCLK  = 192/2   = 96 MHz
     *  USB_CLK = 192/4   = 48 MHz  (required by USB)
     *  APB1    = 96/2    = 48 MHz  (max 50 MHz)
     *  APB2    = 96/1    = 96 MHz
     */
    Clock_PLL_Config_t pll =
    {
        .PLLM    = 25,
        .PLLN    = 192,
        .PLLP    = 2,
        .PLLQ    = 4,
        .APB1_PRE = 2,
        .APB2_PRE = 1,
    };
    clock_init_pll(&pll);
    systick_init(TICK_HZ);

    GPIO_PinConfig_t led =
    {
        .pGPIOx          = LED_PORT,
        .GPIO_PinNumber  = LED_PIN,
        .GPIO_PinMode    = GPIO_MODE_OUT,
        .GPIO_PinSpeed   = GPIO_SPEED_LOW,
        .GPIO_PinOPType  = GPIO_OP_TYPE_PP,
        .GPIO_PinPuPdControl = GPIO_NO_PUPD,
        .GPIO_PinAltFunMode  = GPIO_PIN_NO_ALTFN,
    };
    GPIO_Init(&led);
    GPIO_WriteToOutputPin(LED_PORT, LED_PIN, GPIO_PIN_SET); 

    USB_CDC_Init();

    for (int i = 0; i < 6; i++)
    {
        GPIO_ToggleOutputPin(LED_PORT, LED_PIN);
        ticks_delay(500);
    }

    uint8_t buf[64];

    while(1)
    {
        uint16_t n = USB_CDC_Read(buf, sizeof(buf));
 
        if (n > 0)
        {
            USB_CDC_Transmit(buf, n);
            GPIO_ToggleOutputPin(LED_PORT, LED_PIN);
        }
    }
}