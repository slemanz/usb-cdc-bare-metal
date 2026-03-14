#include "driver_uart.h"
#include "driver_clock.h"
#include "driver_systick.h"

/*********************************************************************
 * Private Helper Functions
 *********************************************************************/

 static uint16_t compute_baud_div(uint32_t peripheralClk, uint32_t baudRate)
 {
    return (uint16_t)((peripheralClk + (baudRate/2U))/baudRate);
 }

 static UART_Error_e UART_CheckErrors(UART_RegDef_t *pUARTx)
 {
    uint32_t sr = pUARTx->SR;
    UART_Error_e error = UART_OK;

    if((sr & (1U << UART_SR_ORE)) != 0U)
    {
        error = UART_ERROR_ORE;
        (void)pUARTx->DR;
    }else if((sr & (1U << UART_SR_FE)) != 0U)
    {
        error = UART_ERROR_FE;
        (void)pUARTx->DR;
    }
    else if ((sr & (1U << UART_SR_NE)) != 0U)
    {
        error = UART_ERROR_NE;
        (void)pUARTx->DR;
    }
    else if ((sr & (1U << UART_SR_PE)) != 0U)
    {
        error = UART_ERROR_PE;
        (void)pUARTx->DR;
    }

    return error;
 }

static void configure_cr1(UART_RegDef_t *pUARTx, const UART_Config_t *pUARTConfig)
{
    uint32_t cr1 = 0U;

    switch(pUARTConfig->UART_Mode)
    {
        case UART_MODE_ONLY_RX: cr1 |= (1U << UART_CR1_RE);                         break;
        case UART_MODE_ONLY_TX: cr1 |= (1U << UART_CR1_TE);                         break;
        case UART_MODE_TXRX:    cr1 |= (1U << UART_CR1_RE) | (1U << UART_CR1_TE);   break;
        default:                                                                    break;
    }

    cr1 |= (pUARTConfig->UART_WordLength << UART_CR1_M);

    if(pUARTConfig->UART_ParityControl == UART_PARITY_EN_EVEN)
    {
        cr1 |= (1U << UART_CR1_PCE);
    }
    else if (pUARTConfig->UART_ParityControl == UART_PARITY_EN_ODD)
    {
        cr1 |= (1U << UART_CR1_PCE) | (1U << UART_CR1_PS);
    }

    pUARTx->CR1 = cr1;
}

static void configure_cr3(UART_RegDef_t *pUARTx, const UART_Config_t *pUARTConfig)
{
    uint32_t cr3 = 0U;

    switch (pUARTConfig->UART_HWFlowControl)
    {
        case UART_HW_FLOW_CTRL_CTS:     cr3 |= (1U << UART_CR3_CTSE);                           break;
        case UART_HW_FLOW_CTRL_RTS:     cr3 |= (1U << UART_CR3_RTSE);                           break;
        case UART_HW_FLOW_CTRL_CTS_RTS: cr3 |= (1U << UART_CR3_CTSE) | (1U << UART_CR3_RTSE);   break;
        default:                                                                                break;
    }

    pUARTx->CR3 = cr3;
}

/*********************************************************************
 * Public API Implementations
 *********************************************************************/

void UART_PeriClockControl(UART_RegDef_t *pUARTx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if      (pUARTx == UART1) { UART1_PCLK_EN(); }
        else if (pUARTx == UART2) { UART2_PCLK_EN(); }
        else if (pUARTx == UART6) { UART6_PCLK_EN(); }
    }
    else
    {
        if      (pUARTx == UART1) { UART1_PCLK_DI(); }
        else if (pUARTx == UART2) { UART2_PCLK_DI(); }
        else if (pUARTx == UART6) { UART6_PCLK_DI(); }
    }
}

void UART_Init(UART_Config_t *pUARTConfig)
{
    UART_PeriClockControl(pUARTConfig->pUARTx, ENABLE);

    configure_cr1(pUARTConfig->pUARTx, pUARTConfig);

    pUARTConfig->pUARTx->CR2 = (pUARTConfig->UART_NoOfStopBits << UART_CR2_STOP);

    configure_cr3(pUARTConfig->pUARTx, pUARTConfig);

    pUARTConfig->pUARTx->BRR = compute_baud_div(clock_get(), pUARTConfig->UART_Baud);
}

void UART_DeInit(UART_RegDef_t *pUARTx)
{
    if      (pUARTx == UART1) { UART1_REG_RESET(); }
    else if (pUARTx == UART2) { UART2_REG_RESET(); }
    else if (pUARTx == UART6) { UART6_REG_RESET(); }
}

void UART_PeripheralControl(UART_RegDef_t *pUARTx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        pUARTx->CR1 |= (1U << UART_CR1_UE);
    }
    else
    {
        pUARTx->CR1 &= ~(1U << UART_CR1_UE);
    }
}

bool UART_GetFlagStatus(UART_RegDef_t *pUARTx, uint32_t flag)
{
    return ((pUARTx->SR & flag) != 0U);
}

UART_Error_e UART_WaitForFlag(UART_RegDef_t *pUARTx, uint32_t flag, bool status, uint32_t timeoutMs)
{
    ticks_timeout_t timeout;
    ticks_timeoutInit(&timeout, timeoutMs);

    while (((pUARTx->SR & flag) != 0U) != status)
    {
        UART_Error_e error = UART_CheckErrors(pUARTx);
        if (error != UART_OK)
        {
            return error;
        }

        if (ticks_timeoutIsExpired(&timeout))
        {
            return UART_ERROR_TIMEOUT;
        }
    }

    return UART_OK;
}

UART_Error_e UART_WriteByte(UART_RegDef_t *pUARTx, uint8_t data)
{
    UART_Error_e error = UART_WaitForFlag(pUARTx, UART_FLAG_TXE, true, UART_DEFAULT_TIMEOUT);
    if (error != UART_OK)
    {
        return error;
    }

    pUARTx->DR = data;
    return UART_OK;
}

UART_Error_e UART_Write(UART_RegDef_t *pUARTx, const uint8_t *pTxBuffer, uint32_t Len)
{
    for (uint32_t i = 0; i < Len; i++)
    {
        UART_Error_e error = UART_WriteByte(pUARTx, pTxBuffer[i]);
        if (error != UART_OK)
        {
            return error;
        }
    }

    return UART_WaitForFlag(pUARTx, UART_FLAG_TC, true, UART_DEFAULT_TIMEOUT);
}

uint8_t UART_ReadByte(UART_RegDef_t *pUARTx)
{
    return (uint8_t)pUARTx->DR;
}

void UART_InterruptControl(UART_RegDef_t *pUARTx, uint32_t interruptMask, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        pUARTx->CR1 |= interruptMask;
    }
    else
    {
        pUARTx->CR1 &= ~interruptMask;
    }
}