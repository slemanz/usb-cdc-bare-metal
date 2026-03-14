#include "driver_i2c.h"
#include "driver_clock.h"
#include "driver_systick.h"


/*********************************************************************
 * Private Helper Functions (Internal Use Only)
 *********************************************************************/

static void I2C_ClearADDRFlag(I2C_RegDef_t *pI2Cx)
{
    // Sequence to clear ADDR: Read SR1 -> Read SR2
    uint32_t dummy_read;
    dummy_read = pI2Cx->SR1;
    dummy_read = pI2Cx->SR2;
    (void)dummy_read;
}

static I2C_Error_e I2C_CheckErrors(I2C_RegDef_t *pI2Cx)
{
    uint32_t sr1 = pI2Cx->SR1;
    I2C_Error_e error = I2C_OK;

    if ((sr1 & I2C_FLAG_BERR) != 0U)
    {
        error = I2C_ERROR_BERR;
        pI2Cx->SR1 &= ~I2C_FLAG_BERR;  /* Clear flag */
    }
    else if ((sr1 & I2C_FLAG_ARLO) != 0U)
    {
        error = I2C_ERROR_ARLO;
        pI2Cx->SR1 &= ~I2C_FLAG_ARLO;
    }
    else if ((sr1 & I2C_FLAG_AF) != 0U)
    {
        error = I2C_ERROR_AF;
        pI2Cx->SR1 &= ~I2C_FLAG_AF;
    }
    else if ((sr1 & I2C_FLAG_OVR) != 0U)
    {
        error = I2C_ERROR_OVR;
        pI2Cx->SR1 &= ~I2C_FLAG_OVR;
    }
    else if ((sr1 & I2C_FLAG_TIMEOUT) != 0U)
    {
        error = I2C_ERROR_TIMEOUT;
        pI2Cx->SR1 &= ~I2C_FLAG_TIMEOUT;
    }

    return error;
}

/*********************************************************************
 * Public API Implementations
 *********************************************************************/


void I2C_PeripheralControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
	if(EnorDi == ENABLE)
	{
		pI2Cx->CR1 |= (1 << I2C_CR1_PE);
	}else
	{
		pI2Cx->CR1 &= ~(1 << I2C_CR1_PE);
	}
}

uint8_t I2C_GetFlagStatus(I2C_RegDef_t *pI2Cx, uint32_t FlagName)
{
    if(pI2Cx->SR1 & FlagName)
    {
        return FLAG_SET;
    }
    return FLAG_RESET;
}

I2C_Error_e I2C_GenereteStart(I2C_RegDef_t *pI2Cx)
{
    pI2Cx->CR1 |= (1 << I2C_CR1_START);
    return I2C_WaitForFlag(pI2Cx, I2C_FLAG_SB, true, I2C_DEFAULT_TIMEOUT);
}

void I2C_GenereteStop(I2C_RegDef_t *pI2Cx)
{
    pI2Cx->CR1 |= (1 << I2C_CR1_STOP);
}

bool I2C_IsBusy(I2C_RegDef_t *pI2Cx)
{
    return ((pI2Cx->SR2 & I2C_SR2_BUSY_MSK) != 0);
}

I2C_Error_e I2C_WaitBusy(I2C_RegDef_t *pI2Cx)
{
    ticks_timeout_t timeout;
    ticks_timeoutInit(&timeout, I2C_DEFAULT_TIMEOUT);

    while(I2C_IsBusy(pI2Cx))
    {
        I2C_Error_e error = I2C_CheckErrors(pI2Cx);
        if(error != I2C_OK)
        {
            return error;
        }
        
        if(ticks_timeoutIsExpired(&timeout))
        {
            return I2C_ERROR_TIMEOUT;
        }
    }

    return I2C_OK;
}


I2C_Error_e I2C_WaitForFlag(I2C_RegDef_t *pI2Cx, uint32_t flag, bool status, uint32_t timeoutMs)
{
    ticks_timeout_t timeout;
    ticks_timeoutInit(&timeout, timeoutMs);

    while(((pI2Cx->SR1 & flag) != 0U) != status)
    {
        I2C_Error_e error = I2C_CheckErrors(pI2Cx);
        if(error != I2C_OK)
        {
            return error;
        }
        
        if(ticks_timeoutIsExpired(&timeout))
        {
            return I2C_ERROR_TIMEOUT;
        }
    }

    return I2C_OK;
}

void I2C_ManageAcking(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if(EnorDi == ENABLE)
    {
        pI2Cx->CR1 |= (1 << I2C_CR1_ACK);
    }else
    {
        pI2Cx->CR1 &= ~(1 << I2C_CR1_ACK);
    }
}

I2C_Error_e I2C_SendAddress(I2C_RegDef_t *pI2Cx, uint8_t address, uint8_t sendType)
{
    I2C_Error_e error;
    address = (address << 1);
    address |= (sendType << 0);

    pI2Cx->DR = address;
    error = I2C_WaitForFlag(pI2Cx, I2C_FLAG_ADDR, true, I2C_DEFAULT_TIMEOUT);
    if(error == I2C_OK)
    {
        I2C_ClearADDRFlag(pI2Cx);
    }
    return error;
}


void I2C_PeriClockControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
	if(EnorDi == ENABLE)
	{
		if(pI2Cx == I2C1)
		{
			I2C1_PCLK_EN();
		}else if(pI2Cx == I2C2)
		{
			I2C2_PCLK_EN();
		}else if(pI2Cx == I2C3)
		{
			I2C3_PCLK_EN();
		}
	}else
	{
		if(pI2Cx == I2C1)
		{
			I2C1_PCLK_DI();
		}else if(pI2Cx == I2C2)
		{
			I2C2_PCLK_DI();
		}else if(pI2Cx == I2C3)
		{
			I2C3_PCLK_DI();
		}
	}
}

void I2C_Init(I2C_Config_t *pI2CConfig)
{
    uint32_t tempreg = 0;

    I2C_PeriClockControl(pI2CConfig->pI2Cx, ENABLE);

    /* Software reset to clear any stuck state */
    pI2CConfig->pI2Cx->CR1 |= (1 << I2C_CR1_SWRST);
    pI2CConfig->pI2Cx->CR1 &= ~(1 << I2C_CR1_SWRST);

    uint32_t pclk1 = clock_get();
    uint32_t freq_mhz = pclk1 / 1000000U;

    /* Configure CR2 FREQ field — APB1 clock in MHz */
    tempreg = pI2CConfig->pI2Cx->CR2;
    tempreg &= ~(0x3FU << I2C_CR2_FREQ);
    tempreg |= (freq_mhz & 0x3FU) << I2C_CR2_FREQ;
    pI2CConfig->pI2Cx->CR2 = tempreg;

    /* Configure OAR1 */
    tempreg = 0;
    tempreg |= pI2CConfig->I2C_DeviceAddress << 1;
    tempreg |= (1 << 14); /* Bit 14 must be kept at 1 by software */
    pI2CConfig->pI2Cx->OAR1 = tempreg;

    /* Configure CCR */
    uint16_t ccr_value = 0;
    tempreg = 0;

    if(pI2CConfig->I2C_SCLSpeed <= I2C_SCL_SPEED_SM)
    {
        /* Standard Mode */
        ccr_value = (pclk1 / (2 * pI2CConfig->I2C_SCLSpeed));
        tempreg |= (ccr_value & 0xFFF);
    }
    else
    {
        /* Fast Mode */
        tempreg |= (1 << 15);
        tempreg |= (pI2CConfig->I2C_FMDutyCycle << 14);
        if(pI2CConfig->I2C_FMDutyCycle == I2C_FM_DUTY_2)
            ccr_value = (pclk1 / (3 * pI2CConfig->I2C_SCLSpeed));
        else
            ccr_value = (pclk1 / (25 * pI2CConfig->I2C_SCLSpeed));

        tempreg |= (ccr_value & 0xFFF);
    }
    pI2CConfig->pI2Cx->CCR = tempreg;

    /* Configure TRISE */
    if(pI2CConfig->I2C_SCLSpeed <= I2C_SCL_SPEED_SM)
    {
        tempreg = freq_mhz + 1;              /* Trise(SCL) = 1000ns / Tpclk + 1 */
    }
    else
    {
        tempreg = ((freq_mhz * 300) / 1000) + 1;  /* Trise(SCL) = 300ns / Tpclk + 1 */
    }
    pI2CConfig->pI2Cx->TRISE = (tempreg & 0x3F);
}

void I2C_DeInit(I2C_RegDef_t *pI2Cx)
{
    pI2Cx->CR1 |= (1 << I2C_CR1_SWRST);
    pI2Cx->CR1 &= ~(1 << I2C_CR1_SWRST);
    I2C_PeriClockControl(pI2Cx, DISABLE);
}

/*
 * Data send and receive
 */


uint32_t I2C_Send(I2C_RegDef_t *pI2Cx, uint8_t *pTxbuffer, uint32_t Len)
{
    I2C_Error_e error;

    for(uint32_t i = 0; i < Len; i++)
    {
        pI2Cx->DR = pTxbuffer[i];
        error = I2C_WaitForFlag(pI2Cx, I2C_FLAG_TXE, true, I2C_DEFAULT_TIMEOUT);
        if(error != I2C_OK)
        {
            return i;
        }
    }
    I2C_WaitForFlag(pI2Cx, I2C_FLAG_BTF, true, I2C_DEFAULT_TIMEOUT);

    return Len;
}

uint32_t I2C_Receive(I2C_RegDef_t *pI2Cx, uint8_t *pRxbuffer, uint32_t Len)
{
    I2C_Error_e error;

    I2C_ManageAcking(pI2Cx, ENABLE);

    for(uint32_t i = 0; i < Len; i++)
    {
        if(i == Len -1) // send nack
        {
            I2C_ManageAcking(pI2Cx, DISABLE);
        }

        error = I2C_WaitForFlag(pI2Cx, I2C_FLAG_RXNE, true, I2C_DEFAULT_TIMEOUT);
        if(error != I2C_OK)
        {
            return i;
        }
        pRxbuffer[i] = pI2Cx->DR;
    }
    return Len;
}