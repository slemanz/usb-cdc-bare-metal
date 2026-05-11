#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

/* Called from SET_CONFIGURATION handler (inside ISR) */
void usb_cdc_init(void);

/* Called from USBRST handler (inside ISR) */
void usb_cdc_reset(void);

/* Called from IEPINT handler when EP1 IN transfer completes (inside ISR) */
void usb_cdc_on_tx_done(void);

/* Update DTR/RTS state from CDC_SET_CONTROL_LINE_STATE (inside ISR).
 * wValue bit 0 = DTR, bit 1 = RTS. */
void usb_cdc_set_control_line(uint16_t state);

/* True when device is configured AND host has DTR asserted (port open). */
uint8_t usb_cdc_connected(void);

/* Non-blocking: drops the packet and returns 0 if a previous TX is still in
 * flight or the device isn't connected.  Returns 1 if the data was queued. */
uint8_t usb_cdc_write(const uint8_t *buf, uint16_t len);

#endif /* USB_CDC_H */
