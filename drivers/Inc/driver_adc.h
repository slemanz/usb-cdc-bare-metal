#ifndef INC_DRIVER_ADC_H_
#define INC_DRIVER_ADC_H_

#include "stm32f411xx.h"

typedef struct
{
    ADC_RegDef_t    *pADCx;
    uint8_t         ADC_Resolution;
    uint8_t         ADC_SampleTime;
    uint8_t         ADC_DataAlignment;
}ADC_Config_t;

typedef enum
{
    ADC_OK              = 0x00U,
    ADC_ERROR_TIMEOUT   = 0x01U,
    ADC_ERROR_OVR       = 0x02U,
    ADC_ERROR_INVALID   = 0x04U,
} ADC_Error_e;

#define ADC_DEFAULT_TIMEOUT     10U

 /********************************************************************************
 *  Bit position definitions of ADC peripheral
 ********************************************************************************/

/* ADC_SR */
#define ADC_SR_AWD          0
#define ADC_SR_EOC          1
#define ADC_SR_JEOC         2
#define ADC_SR_JSTRT        3
#define ADC_SR_STRT         4
#define ADC_SR_OVR          5

/* ADC_CR1 */
#define ADC_CR1_AWDCH       0
#define ADC_CR1_EOCIE       5
#define ADC_CR1_AWDIE       6
#define ADC_CR1_JEOCIE      7
#define ADC_CR1_SCAN        8
#define ADC_CR1_AWDSGL      9
#define ADC_CR1_JAUTO       10
#define ADC_CR1_DISCEN      11
#define ADC_CR1_JDISCEN     12
#define ADC_CR1_DISCNUM     13
#define ADC_CR1_JAWDEN      22
#define ADC_CR1_AWDEN       23
#define ADC_CR1_RES         24
#define ADC_CR1_OVRIE       26

/* ADC_CR2 */
#define ADC_CR2_ADON        0
#define ADC_CR2_CONT        1
#define ADC_CR2_DMA         8
#define ADC_CR2_DDS         9
#define ADC_CR2_EOCS        10
#define ADC_CR2_ALIGN       11
#define ADC_CR2_JEXTSEL     16
#define ADC_CR2_JEXTEN      20
#define ADC_CR2_JSWSTART    22
#define ADC_CR2_EXTSEL      24
#define ADC_CR2_EXTEN       28
#define ADC_CR2_SWSTART     30

/* ADC_SQR1 */
#define ADC_SQR1_L          20   /* Total number of conversions in regular sequence */

/* ADC flags */
#define ADC_FLAG_EOC        (1U << ADC_SR_EOC)
#define ADC_FLAG_OVR        (1U << ADC_SR_OVR)
#define ADC_FLAG_STRT       (1U << ADC_SR_STRT)


/******************************************************************************************
 *                          POSSIBLE CONFIGURATIONS
 ******************************************************************************************/

/* @ADC_Resolution */
#define ADC_RESOLUTION_12BIT    0U
#define ADC_RESOLUTION_10BIT    1U
#define ADC_RESOLUTION_8BIT     2U
#define ADC_RESOLUTION_6BIT     3U

/* @ADC_SampleTime (cycles) */
#define ADC_SAMPLETIME_3        0U
#define ADC_SAMPLETIME_15       1U
#define ADC_SAMPLETIME_28       2U
#define ADC_SAMPLETIME_56       3U
#define ADC_SAMPLETIME_84       4U
#define ADC_SAMPLETIME_112      5U
#define ADC_SAMPLETIME_144      6U
#define ADC_SAMPLETIME_480      7U

/* @ADC_DataAlignment */
#define ADC_ALIGN_RIGHT         0U
#define ADC_ALIGN_LEFT          1U

/* ADC channel numbers (STM32F411 ADC1) */
#define ADC_CHANNEL_0           0U
#define ADC_CHANNEL_1           1U
#define ADC_CHANNEL_4           4U
#define ADC_CHANNEL_8           8U
#define ADC_CHANNEL_9           9U
#define ADC_CHANNEL_10          10U
#define ADC_CHANNEL_11          11U
#define ADC_CHANNEL_12          12U
#define ADC_CHANNEL_13          13U
#define ADC_CHANNEL_TEMP        16U
#define ADC_CHANNEL_VREF        17U


/********************************************************************************************
 *                                  Driver API
 ********************************************************************************************/

void         ADC_PeriClockControl(ADC_RegDef_t *pADCx, uint8_t EnorDi);

void         ADC_Init(ADC_Config_t *pADCConfig);
void         ADC_DeInit(ADC_RegDef_t *pADCx);

void         ADC_PeripheralControl(ADC_RegDef_t *pADCx, uint8_t EnorDi);
bool         ADC_GetFlagStatus(ADC_RegDef_t *pADCx, uint32_t flag);

ADC_Error_e  ADC_WaitForFlag(ADC_RegDef_t *pADCx, uint32_t flag, bool status, uint32_t timeoutMs);
ADC_Error_e  ADC_ReadChannel(ADC_RegDef_t *pADCx, uint8_t channel, uint16_t *pResult);


#endif /* INC_DRIVER_ADC_H_ */