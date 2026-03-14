#include "stm32f411xx.h"
#include "driver_clock.h"

/*
 *  AHB prescaler lookup table (RCC_CFGR bits [7:4])
 *  0xxx = /1,  1000 = /2,  1001 = /4, ... 1111 = /512
 */
static const uint16_t ahb_pre_table[16] =
{
    1, 1, 1, 1, 1, 1, 1, 1,
    2, 4, 8, 16, 64, 128, 256, 512
};

/*
 *  APB prescaler lookup table (RCC_CFGR bits PPRE1[12:10] and PPRE2[15:13])
 *  0xx = /1,  100 = /2,  101 = /4,  110 = /8,  111 = /16
 */
static const uint8_t apb_pre_table[8] =
{
    1, 1, 1, 1,
    2, 4, 8, 16
};

static uint32_t apb_pre_to_bits(uint8_t pre)
{
    switch (pre)
    {
        case 2:  return 0x4U;
        case 4:  return 0x5U;
        case 8:  return 0x6U;
        case 16: return 0x7U;
        default: return 0x0U;   /* /1 */
    }
}

static uint32_t flash_latency_for_hclk(uint32_t hclk)
{
    if (hclk <= 30000000U)  return 0;
    if (hclk <= 64000000U)  return 1;
    if (hclk <= 90000000U)  return 2;
    return 3;
}

/******************************************************************************
 *  clock_init_pll  –  configure HSE -> PLL -> SYSCLK
 *
 *  Example for USB (48 MHz exact) + SYSCLK 96 MHz:
 *    PLLM=25, PLLN=192, PLLP=2, PLLQ=4, APB1_PRE=2, APB2_PRE=1
 *    VCO_in  = 25 MHz / 25  = 1 MHz
 *    VCO_out = 1 MHz * 192  = 192 MHz
 *    SYSCLK  = 192 / 2      = 96 MHz
 *    USB_CLK = 192 / 4      = 48 MHz
 *    APB1    = 96 / 2       = 48 MHz  (max 50 MHz)
 *    APB2    = 96 / 1       = 96 MHz  (max 100 MHz)
 ******************************************************************************/
void clock_init_pll(const Clock_PLL_Config_t *pConfig)
{
    /* 1. Enable HSE and wait for it to stabilise */
    RCC->CR |= (1U << 16);                         /* HSEON */
    while (!(RCC->CR & (1U << 17))) {}              /* HSERDY */
 
    /* 2. Enable PWR clock and select voltage regulator Scale 1 */
    RCC->APB1ENR |= (1U << 28);                    /* PWREN */
    PWR->CR |= (3U << 14);                         /* VOS = Scale 1 */
 
    /* 3. Calculate and set flash wait states */
    uint32_t pllp_val = pConfig->PLLP;
    uint32_t sysclk   = (HSE_CLOCK / pConfig->PLLM) * pConfig->PLLN / pllp_val;
    uint32_t latency  = flash_latency_for_hclk(sysclk);
 
    FLASH->ACR = (latency << 0)
               | (1U << 8)     /* prefetch */
               | (1U << 9)     /* instruction cache */
               | (1U << 10);   /* data cache */
 
    /* 4. Bus prescalers (AHB = /1 fixed) */
    RCC->CFGR &= ~((0xFU << 4) | (0x7U << 10) | (0x7U << 13));
    RCC->CFGR |= (apb_pre_to_bits(pConfig->APB1_PRE) << 10);
    RCC->CFGR |= (apb_pre_to_bits(pConfig->APB2_PRE) << 13);
 
    /* 5. Configure PLL: source = HSE */
    /*    PLLP register encoding: 00=2, 01=4, 10=6, 11=8 */
    uint32_t pllp_bits = (pllp_val / 2U) - 1U;
 
    RCC->PLLCFGR = (pConfig->PLLM << 0)
                  | (pConfig->PLLN << 6)
                  | (pllp_bits << 16)
                  | (1U << 22)              /* PLLSRC = HSE */
                  | (pConfig->PLLQ << 24);
 
    /* 6. Enable PLL and wait */
    RCC->CR |= (1U << 24);                         /* PLLON */
    while (!(RCC->CR & (1U << 25))) {}              /* PLLRDY */
 
    /* 7. Select PLL as system clock */
    RCC->CFGR &= ~(0x3U << 0);
    RCC->CFGR |=  (0x2U << 0);                     /* SW = PLL */
    while (((RCC->CFGR >> 2) & 0x3U) != 0x2U) {}   /* SWS = PLL */
}
 


uint32_t clock_getSYSCLK(void)
{
    uint8_t sws = (uint8_t)((RCC->CFGR >> 2) & 0x03U);
 
    switch (sws)
    {
    case 0: return HSI_CLOCK;   /* HSI */
    case 1: return HSE_CLOCK;   /* HSE */
    case 2:                     /* PLL */
    {
        uint32_t cfg  = RCC->PLLCFGR;
        uint32_t src  = (cfg & (1U << 22)) ? HSE_CLOCK : HSI_CLOCK;
        uint32_t m    = cfg & 0x3FU;
        uint32_t n    = (cfg >> 6) & 0x1FFU;
        uint32_t p    = (((cfg >> 16) & 0x03U) + 1U) * 2U;
        if (m == 0) return HSI_CLOCK;
        return (src / m) * n / p;
    }
    default: return HSI_CLOCK;
    }
}
 
 
uint32_t clock_getHCLK(void)
{
    uint8_t idx = (uint8_t)((RCC->CFGR >> 4) & 0x0FU);
    return clock_getSYSCLK() / ahb_pre_table[idx];
}
 
 
uint32_t clock_getPCLK1(void)
{
    uint8_t idx = (uint8_t)((RCC->CFGR >> 10) & 0x07U);
    return clock_getHCLK() / apb_pre_table[idx];
}
 
 
uint32_t clock_getPCLK2(void)
{
    uint8_t idx = (uint8_t)((RCC->CFGR >> 13) & 0x07U);
    return clock_getHCLK() / apb_pre_table[idx];
}
 
 
uint32_t clock_get(void)
{
    return clock_getHCLK();
}