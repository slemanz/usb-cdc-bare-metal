#ifndef INC_DRIVER_CLOCK_H_
#define INC_DRIVER_CLOCK_H_

#include <stdint.h>

/*
 *  PLL configuration structure
 *
 *  VCO input  = source / PLLM         (must be 1–2 MHz)
 *  VCO output = VCO_in * PLLN         (must be 100–432 MHz)
 *  SYSCLK     = VCO_out / PLLP
 *  USB clock  = VCO_out / PLLQ        (must be exactly 48 MHz for USB)
 */
typedef struct
{
    uint32_t PLLM;      /* Input divider:      2..63 */
    uint32_t PLLN;      /* VCO multiplier:     50..432 */
    uint32_t PLLP;      /* SYSCLK divider:     2, 4, 6 or 8 */
    uint32_t PLLQ;      /* USB/SDIO divider:   2..15 */
    uint8_t  APB1_PRE;  /* APB1 prescaler:     1, 2, 4, 8 or 16 */
    uint8_t  APB2_PRE;  /* APB2 prescaler:     1, 2, 4, 8 or 16 */
} Clock_PLL_Config_t;

void clock_init_pll(const Clock_PLL_Config_t *pConfig);

uint32_t clock_getSYSCLK(void);     /* SYSCLK: HSI, HSE or PLL output */
uint32_t clock_getHCLK(void);       /* AHB (= SYSCLK / AHB prescaler) */
uint32_t clock_getPCLK1(void);      /* APB1 (UART2, I2C, TIM2-5, SPI2/3) */
uint32_t clock_getPCLK2(void);      /* APB2 (UART1/6, SPI1/4, ADC) */

uint32_t clock_get(void);           /* returns HCLK */

#endif /* INC_DRIVER_CLOCK_H_ */