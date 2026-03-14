#ifndef INC_DRIVER_TIMER_H_
#define INC_DRIVER_TIMER_H_

#include "stm32f411xx.h"

typedef struct
{
    TIM_RegDef_t   *pTIMx;
    uint32_t        prescaler;
    uint32_t        period;         /**< ARR value (period - 1 counts) */
} TIM_Config_t;

/********************************************************************************
 *  Bit position definitions of TIM peripheral
 ********************************************************************************/

/* TIM_CR1 */
#define TIM_CR1_CEN         0
#define TIM_CR1_UDIS        1
#define TIM_CR1_URS         2
#define TIM_CR1_OPM         3
#define TIM_CR1_DIR         4
#define TIM_CR1_CMS         5
#define TIM_CR1_ARPE        7

/* TIM_SR */
#define TIM_SR_UIF          0
#define TIM_SR_CC1IF        1
#define TIM_SR_CC2IF        2
#define TIM_SR_CC3IF        3
#define TIM_SR_CC4IF        4

/* TIM_CCMR OCxM field (PWM mode 1) */
#define TIM_CCMR_OC_PWM1    0x6U

/* TIM_CCER CCxE (output enable) bit positions */
#define TIM_CCER_CC1E       0
#define TIM_CCER_CC2E       4
#define TIM_CCER_CC3E       8
#define TIM_CCER_CC4E       12

/* TIM_EGR */
#define TIM_EGR_UG          0

/******************************************************************************************
 *                          POSSIBLE CONFIGURATIONS
 ******************************************************************************************/

/* @TIM_CHANNEL */
#define TIM_CHANNEL_1       0U
#define TIM_CHANNEL_2       1U
#define TIM_CHANNEL_3       2U
#define TIM_CHANNEL_4       3U

/**
 *   TIM_PWM_CALC_PRESCALER(16000000, 1000, 1000)  ->  prescaler to get 1kHz with ARR=999
 *   prescaler = (sysclk / (resolution * freq_hz)) - 1
 */
#define TIM_PWM_CALC_PRESCALER(sysclk_hz, freq_hz, resolution) \
    (((sysclk_hz) / ((resolution) * (freq_hz))) - 1U)


/********************************************************************************************
 *                                  Driver API
 ********************************************************************************************/

void TIM_PeriClockControl(TIM_RegDef_t *pTIMx, uint8_t EnorDi);

void TIM_PWM_Init(TIM_Config_t *pTIMConfig);
void TIM_PWM_SetDuty(TIM_Config_t *pTIMConfig, uint8_t channel, float dutyPercent);

void TIM_Start(TIM_RegDef_t *pTIMx);
void TIM_Stop(TIM_RegDef_t *pTIMx);

#endif /* INC_DRIVER_TIMER_H_ */