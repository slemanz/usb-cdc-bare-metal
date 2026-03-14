#include "driver_adc.h"
#include "driver_systick.h"


/*********************************************************************
 * Private Helper Functions
 *********************************************************************/

static void set_channel_sample_time(ADC_RegDef_t *pADCx, uint8_t channel, uint8_t sampleTime)
{
    if (channel <= 9U)
    {
        uint32_t shift = channel * 3U;
        pADCx->SMPR2 &= ~(0x7U << shift);
        pADCx->SMPR2 |=  (sampleTime << shift);
    }
    else if (channel <= 18U)
    {
        uint32_t shift = (channel - 10U) * 3U;
        pADCx->SMPR1 &= ~(0x7U << shift);
        pADCx->SMPR1 |=  (sampleTime << shift);
    }
}

static void set_regular_sequence(ADC_RegDef_t *pADCx, uint8_t channel)
{
    /* Single conversion: L=0 (1 conversion), SQ1 = channel */
    pADCx->SQR1 &= ~(0xFU << ADC_SQR1_L);
    pADCx->SQR3  = (channel & 0x1FU);
}

static ADC_Error_e check_overrun(ADC_RegDef_t *pADCx)
{
    if ((pADCx->SR & ADC_FLAG_OVR) != 0U)
    {
        pADCx->SR &= ~ADC_FLAG_OVR;
        return ADC_ERROR_OVR;
    }
    return ADC_OK;
}


/*********************************************************************
 * Public API Implementations
 *********************************************************************/

void ADC_PeriClockControl(ADC_RegDef_t *pADCx, uint8_t EnorDi)
{
    if (pADCx != ADC1) return;

    if (EnorDi == ENABLE)
    {
        ADC1_PCLK_EN();
    }
    else
    {
        ADC1_PCLK_DI();
    }
}

void ADC_Init(ADC_Config_t *pADCConfig)
{
    ADC_PeriClockControl(pADCConfig->pADCx, ENABLE);

    /* Resolution */
    pADCConfig->pADCx->CR1 &= ~(0x3U << ADC_CR1_RES);
    pADCConfig->pADCx->CR1 |=  (pADCConfig->ADC_Resolution << ADC_CR1_RES);

    /* Data alignment */
    if (pADCConfig->ADC_DataAlignment == ADC_ALIGN_LEFT)
    {
        pADCConfig->pADCx->CR2 |= (1U << ADC_CR2_ALIGN);
    }
    else
    {
        pADCConfig->pADCx->CR2 &= ~(1U << ADC_CR2_ALIGN);
    }

    /* Single conversion mode (CONT = 0) */
    pADCConfig->pADCx->CR2 &= ~(1U << ADC_CR2_CONT);

    /* End-of-conversion after each regular conversion */
    pADCConfig->pADCx->CR2 |= (1U << ADC_CR2_EOCS);
}

void ADC_DeInit(ADC_RegDef_t *pADCx)
{
    ADC_PeripheralControl(pADCx, DISABLE);
    ADC_PeriClockControl(pADCx, DISABLE);
}

void ADC_PeripheralControl(ADC_RegDef_t *pADCx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        pADCx->CR2 |= (1U << ADC_CR2_ADON);
    }
    else
    {
        pADCx->CR2 &= ~(1U << ADC_CR2_ADON);
    }
}

bool ADC_GetFlagStatus(ADC_RegDef_t *pADCx, uint32_t flag)
{
    return ((pADCx->SR & flag) != 0U);
}

ADC_Error_e ADC_WaitForFlag(ADC_RegDef_t *pADCx, uint32_t flag, bool status, uint32_t timeoutMs)
{
    ticks_timeout_t timeout;
    ticks_timeoutInit(&timeout, timeoutMs);

    while (((pADCx->SR & flag) != 0U) != status)
    {
        ADC_Error_e error = check_overrun(pADCx);
        if (error != ADC_OK)
        {
            return error;
        }

        if (ticks_timeoutIsExpired(&timeout))
        {
            return ADC_ERROR_TIMEOUT;
        }
    }

    return ADC_OK;
}

ADC_Error_e ADC_ReadChannel(ADC_RegDef_t *pADCx, uint8_t channel, uint16_t *pResult)
{
    if (pResult == NULL) return ADC_ERROR_INVALID;

    set_channel_sample_time(pADCx, channel, ADC_SAMPLETIME_480);
    set_regular_sequence(pADCx, channel);

    /* Clear EOC flag then start conversion */
    pADCx->SR &= ~ADC_FLAG_EOC;
    pADCx->CR2 |= (1U << ADC_CR2_SWSTART);

    ADC_Error_e error = ADC_WaitForFlag(pADCx, ADC_FLAG_EOC, true, ADC_DEFAULT_TIMEOUT);
    if (error != ADC_OK)
    {
        return error;
    }

    *pResult = (uint16_t)(pADCx->DR);
    return ADC_OK;
}