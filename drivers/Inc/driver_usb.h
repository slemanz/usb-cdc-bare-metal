#ifndef INC_DRIVER_USB_H_
#define INC_DRIVER_USB_H_

#include "stm32f411xx.h"

/*
 *  USB OTG FS  –  Bare-metal CDC (Virtual COM Port)
 *
 *  What this driver does:
 *    - Configures GPIO (PA11/PA12) for USB
 *    - Initialises the OTG FS peripheral in device mode
 *    - Implements USB enumeration (EP0 control transfers)
 *    - Exposes a virtual serial port (CDC ACM class)
 *
 *  Public API (only 5 functions):
 *    USB_CDC_Init()        – initialise everything (GPIO, core, CDC)
 *    USB_CDC_Transmit()    – send data to the PC
 *    USB_CDC_Read()        – read data received from the PC
 *    USB_CDC_Available()   – how many bytes in the receive buffer
 *    USB_CDC_IsConnected() – has the PC terminal opened the port?
 */

/********************************************************************************************
 *                              APIs supported by this driver
 ********************************************************************************************/

void USB_CDC_Init(void);
void USB_CDC_Transmit(const uint8_t *data, uint16_t len);
uint16_t USB_CDC_Read(uint8_t *buf, uint16_t len);
uint16_t USB_CDC_Available(void);
bool USB_CDC_IsConnected(void);

#endif /* INC_DRIVER_USB_H_ */