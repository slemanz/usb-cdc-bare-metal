#ifndef INC_DRIVER_I2C_H_
#define INC_DRIVER_I2C_H_

#include "stm32f411xx.h"

typedef struct
{
    I2C_RegDef_t 	*pI2Cx;
	uint32_t I2C_SCLSpeed;
	uint8_t I2C_DeviceAddress;
	uint8_t I2C_ACKControl;
	uint32_t I2C_FMDutyCycle;
}I2C_Config_t;

typedef enum {
    I2C_OK              = 0x00U,    /**< No error */
    I2C_ERROR_TIMEOUT   = 0x01U,    /**< Timeout occurred */
    I2C_ERROR_BERR      = 0x02U,    /**< Bus error */
    I2C_ERROR_ARLO      = 0x04U,    /**< Arbitration lost */
    I2C_ERROR_AF        = 0x08U,    /**< Acknowledge failure (NACK) */
    I2C_ERROR_OVR       = 0x10U,    /**< Overrun/Underrun */
    I2C_ERROR_INVALID   = 0x20U,    /**< Invalid parameter */
    I2C_ERROR_BUSY      = 0x40U     /**< Bus is busy */
} I2C_Error_e;

#define I2C_DEFAULT_TIMEOUT         10

/********************************************************************************
 * 	Bit position definition of I2C peripheral
 ********************************************************************************/


/*
 * Bit position definions I2C_CR1
 */

#define I2C_CR1_PE			0
#define I2C_CR1_SMBUS		1
#define I2C_CR1_SMBTYPE		3
#define I2C_CR1_ENARP		4
#define I2C_CR1_ENPEC		5
#define I2C_CR1_ENGC		6
#define I2C_CR1_NOSTRETCH	7
#define I2C_CR1_START		8
#define I2C_CR1_STOP		9
#define I2C_CR1_ACK			10
#define I2C_CR1_POS			11
#define I2C_CR1_PEC			12
#define I2C_CR1_ALERT		13
#define I2C_CR1_SWRST		15


/*
 * Bit position definions I2C_CR2
 */

#define I2C_CR2_FREQ		0
#define I2C_CR2_ITERREN		8
#define I2C_CR2_ITEVTEN		9
#define I2C_CR2_ITBUFEN		10
#define I2C_CR2_DMAEN		11
#define I2C_CR2_LAST		12


/*
 * Bit position definions I2C_OAR1
 */

#define I2C_OAR1_ADD0		0
#define I2C_OAR1_ADD71		1
#define I2C_OAR1_ADD98		8
#define I2C_OAR1_ADDMODE	15


/*
 * Bit position definions I2C_SR1
 */

#define I2C_SR1_SB			0
#define I2C_SR1_ADDR		1
#define I2C_SR1_BTF			2
#define I2C_SR1_ADD10		3
#define I2C_SR1_STOPF		4
#define I2C_SR1_RXNE		6
#define I2C_SR1_TXE			7
#define I2C_SR1_BERR		8
#define I2C_SR1_ARLO 		9
#define I2C_SR1_AF			10
#define I2C_SR1_OVR			11
#define I2C_SR1_PEC_ERR		12
#define I2C_SR1_TIMEOUT		14
#define I2C_SR1_SMBALERT	15


/*
 * Bit position definions I2C_SR2
 */

#define I2C_SR2_MSL			0
#define I2C_SR2_BUSY		1
#define I2C_SR2_TRA			2
#define I2C_SR2_GENCALL		4
#define I2C_SR2_SMBDEFAULT	5
#define I2C_SR2_SMBHOST		6
#define I2C_SR2_DUALF		7
#define I2C_SR2_PEC			8


/*
 * Bit position definions I2C_CCR
 */

#define I2C_CCR_CCR			0
#define I2C_CCR_DUTY		14
#define I2C_CCR_FS			15


/******************************************************************************************
 *                          POSSIBLE CONFIGURATIONS
 ******************************************************************************************/


 /*
  * @I2C_SCLSpeed
  */

#define I2C_SCL_SPEED_SM		100000
#define I2C_SCL_SPEED_FM4K		400000
#define I2C_SCL_SPEED_FM2K		200000

/*
 *	@I2C_ACKControl
 */

#define I2C_ACK_ENABLE			1
#define I2C_ACK_DISABLE			0

/*
 *	@I2C_FMDutyCycle
 */

#define I2C_FM_DUTY_2			0
#define I2C_FM_DUTY_16_9		1

/*
 *	I2C related status flag definitions
 */

#define I2C_FLAG_TXE   		( 1 << I2C_SR1_TXE)
#define I2C_FLAG_RXNE   	( 1 << I2C_SR1_RXNE)
#define I2C_FLAG_SB			( 1 << I2C_SR1_SB)
#define I2C_FLAG_OVR  		( 1 << I2C_SR1_OVR)
#define I2C_FLAG_AF   		( 1 << I2C_SR1_AF)
#define I2C_FLAG_ARLO 		( 1 << I2C_SR1_ARLO)
#define I2C_FLAG_BERR 		( 1 << I2C_SR1_BERR)
#define I2C_FLAG_STOPF 		( 1 << I2C_SR1_STOPF)
#define I2C_FLAG_ADD10 		( 1 << I2C_SR1_ADD10)
#define I2C_FLAG_BTF  		( 1 << I2C_SR1_BTF)
#define I2C_FLAG_ADDR 		( 1 << I2C_SR1_ADDR)
#define I2C_FLAG_TIMEOUT 	( 1 << I2C_SR1_TIMEOUT)

#define I2C_SR2_BUSY_MSK    (1 << I2C_SR2_BUSY)


#define I2C_DISABLE_SR 		DISABLE
#define I2C_ENABLE_SR		ENABLE

#define I2C_SEND_WRITE      0
#define I2C_SEND_READ       1

/********************************************************************************************
 * 								APIs supported by this driver
 * 					for more information check the function definitions
 ********************************************************************************************/

/*
 * Peripheral Clock Setup
 */

void I2C_PeriClockControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);


/*
 * Init and De-init
 */

void I2C_Init(I2C_Config_t *pI2CConfig);
void I2C_DeInit(I2C_RegDef_t *pI2Cx);

/*
 * Data send and receive
 */

bool I2C_IsBusy(I2C_RegDef_t *pI2Cx);
I2C_Error_e I2C_WaitForFlag(I2C_RegDef_t *pI2Cx, uint32_t flag, bool status, uint32_t timeoutMs);
I2C_Error_e I2C_GenereteStart(I2C_RegDef_t *pI2Cx);
I2C_Error_e I2C_WaitBusy(I2C_RegDef_t *pI2Cx);
I2C_Error_e I2C_SendAddress(I2C_RegDef_t *pI2Cx, uint8_t address, uint8_t sendType);
void I2C_GenereteStop(I2C_RegDef_t *pI2Cx);

uint32_t I2C_Send(I2C_RegDef_t *pI2Cx, uint8_t *pTxbuffer, uint32_t Len);
uint32_t I2C_Receive(I2C_RegDef_t *pI2Cx, uint8_t *pRxbuffer, uint32_t Len);

/*
 * Other peripheral control API
 */


void I2C_PeripheralControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
uint8_t I2C_GetFlagStatus(I2C_RegDef_t *pI2Cx, uint32_t FlagName);
void I2C_ManageAcking(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);



#endif /* INC_DRIVER_I2C_H_ */