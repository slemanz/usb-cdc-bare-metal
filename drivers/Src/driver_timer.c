#include "driver_timer.h"
#include "driver_clock.h"


/*********************************************************************
 * Private Helper Functions
 *********************************************************************/

static void configure_pwm_channel(TIM_RegDef_t *pTIMx, uint8_t channel)
{
    uint8_t ccmr_index  = channel / 2U;
    uint8_t ccmr_shift  = (channel % 2U) * 8U;

    /* Set PWM mode 1 on OCxM bits, enable OCxPE (output compare preload) */
    pTIMx->CCMR[ccmr_index] &= ~(0xFFU << ccmr_shift);
    pTIMx->CCMR[ccmr_index] |=  ((TIM_CCMR_OC_PWM1 << 4U) | (1U << 3U)) << ccmr_shift;

    /* Enable channel output */
    pTIMx->CCER |= (1U << (channel * 4U));
}

static uint32_t duty_to_ccr(uint32_t period, float dutyPercent)
{
    if (dutyPercent <= 0.0f)  return 0U;
    if (dutyPercent >= 100.0f) return period;

    return (uint32_t)((float)period * (dutyPercent / 100.0f));
}

 /*********************************************************************
 * Public API Implementations
 *********************************************************************/

void TIM_PeriClockControl(TIM_RegDef_t *pTIMx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if      (pTIMx == TIM2) { TIM2_PCLK_EN(); }
        else if (pTIMx == TIM3) { TIM3_PCLK_EN(); }
        else if (pTIMx == TIM4) { TIM4_PCLK_EN(); }
        else if (pTIMx == TIM5) { TIM5_PCLK_EN(); }
    }
    else
    {
        if      (pTIMx == TIM2) { TIM2_PCLK_DI(); }
        else if (pTIMx == TIM3) { TIM3_PCLK_DI(); }
        else if (pTIMx == TIM4) { TIM4_PCLK_DI(); }
        else if (pTIMx == TIM5) { TIM5_PCLK_DI(); }
    }
}

void TIM_PWM_Init(TIM_Config_t *pTIMConfig)
{
    TIM_PeriClockControl(pTIMConfig->pTIMx, ENABLE);

    pTIMConfig->pTIMx->PSC  = pTIMConfig->prescaler;
    pTIMConfig->pTIMx->ARR  = pTIMConfig->period;
    pTIMConfig->pTIMx->CNT  = 0U;

    /* Auto-reload preload enable */
    pTIMConfig->pTIMx->CR1 |= (1U << TIM_CR1_ARPE);

    /* Force update to load PSC and ARR into shadow registers */
    pTIMConfig->pTIMx->EGR |= (1U << TIM_EGR_UG);
}

void TIM_PWM_SetDuty(TIM_Config_t *pTIMConfig, uint8_t channel, float dutyPercent)
{
    if (channel > TIM_CHANNEL_4) return;

    configure_pwm_channel(pTIMConfig->pTIMx, channel);
    pTIMConfig->pTIMx->CCR[channel] = duty_to_ccr(pTIMConfig->period, dutyPercent);
}

void TIM_Start(TIM_RegDef_t *pTIMx)
{
    pTIMx->CR1 |= (1U << TIM_CR1_CEN);
}

void TIM_Stop(TIM_RegDef_t *pTIMx)
{
    pTIMx->CR1 &= ~(1U << TIM_CR1_CEN);
}