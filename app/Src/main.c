#include "stm32f411xx.h"
#include "driver_clock.h"

/* PC13 — active low LED on Blackpill */
#define LED_ON()   (GPIOC->ODR &= ~(1U << 13))
#define LED_OFF()  (GPIOC->ODR |=  (1U << 13))
#define LED_TOGGLE() (GPIOC->ODR ^=  (1U << 13))

static void led_init(void)
{
    RCC->AHB1ENR |= (1U << 2);                 /* GPIOC clock */
    GPIOC->MODER &= ~(3U << 26);
    GPIOC->MODER |=  (1U << 26);               /* PC13 output */
}

static void delay_ms(volatile uint32_t ms)
{
    /* rough busy-wait at 96 MHz — replace with SysTick later */
    while (ms--) {
        for (volatile uint32_t i = 0; i < 8000; i++) {}
    }
}

int main(void)
{
    Clock_PLL_Config_t pll = {
        .PLLM     = 25,
        .PLLN     = 192,
        .PLLP     = 2,
        .PLLQ     = 4,
        .APB1_PRE = 2,
        .APB2_PRE = 1,
    };
    clock_init_pll(&pll);

    led_init();
    LED_OFF();

    /* TODO Phase 1: usb_hw_init() */

    while (1) {
        LED_TOGGLE();
        delay_ms(500);
    }
}
