#ifndef USB_CORE_H
#define USB_CORE_H

/* Call once from main() after usb_hw_init() */
void usb_core_init(void);

/* ISR — called by OTG_FS_IRQHandler (defined in this module) */
void usb_core_handle_irq(void);

#endif /* USB_CORE_H */
