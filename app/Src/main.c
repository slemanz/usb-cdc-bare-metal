#include "driver_gpio.h"
#include "driver_systick.h"

#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_NO_13

int main(void)
{
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


    while(1)
    {
        for (int i = 0; i < 6; i++)
        {
            GPIO_ToggleOutputPin(LED_PORT, LED_PIN);
            ticks_delay(100);
        }
    }
}