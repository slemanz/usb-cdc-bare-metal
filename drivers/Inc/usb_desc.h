#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

#define USB_DESC_TYPE_DEVICE        1
#define USB_DESC_TYPE_CONFIGURATION 2
#define USB_DESC_TYPE_STRING        3

extern const uint8_t usb_device_descriptor[18];
extern const uint8_t usb_config_descriptor[75];

/* Returns pointer to string descriptor and its total length, or NULL if index invalid */
const uint8_t *usb_get_string_descriptor(uint8_t index, uint16_t *out_len);

#endif /* USB_DESC_H */
