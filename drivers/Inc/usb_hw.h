#ifndef USB_HW_H
#define USB_HW_H

/*
 * usb_hw.h — OTG-FS hardware init (GPIO, FIFO, interrupts, soft-connect).
 * Clock init is NOT here; call clock_init_pll() in main() before usb_hw_init().
 */

void usb_hw_init(void);

#endif /* USB_HW_H */
