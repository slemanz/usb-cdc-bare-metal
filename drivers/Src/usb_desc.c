#include "usb_desc.h"

/* ── Device Descriptor (18 bytes) ─────────────────────────────────────────── */
const uint8_t usb_device_descriptor[18] = {
    18,         /* bLength */
    0x01,       /* bDescriptorType: Device */
    0x00, 0x02, /* bcdUSB: 2.00 (LE) */
    0x02,       /* bDeviceClass: CDC */
    0x00,       /* bDeviceSubClass */
    0x00,       /* bDeviceProtocol */
    64,         /* bMaxPacketSize0 */
    0x83, 0x04, /* idVendor:  0x0483 — ST Microelectronics (LE) */
    0x40, 0x57, /* idProduct: 0x5740 — Virtual COM Port clone (LE) */
    0x00, 0x02, /* bcdDevice: 2.00 (LE) */
    1,          /* iManufacturer */
    2,          /* iProduct */
    3,          /* iSerialNumber */
    1,          /* bNumConfigurations */
};

/* ── Configuration Descriptor block (75 bytes total) ─────────────────────── *
 * Config(9) + IAD(8) + CmdIf(9) + Header(5) + CallMgmt(5) + ACM(4) +
 * Union(5) + NotifEP(7) + DataIf(9) + BulkOUT(7) + BulkIN(7) = 75
 * ─────────────────────────────────────────────────────────────────────────── */
const uint8_t usb_config_descriptor[75] = {
    /* Configuration Descriptor */
    9,          /* bLength */
    0x02,       /* bDescriptorType: Configuration */
    75, 0,      /* wTotalLength: 75 (LE) */
    2,          /* bNumInterfaces */
    1,          /* bConfigurationValue */
    0,          /* iConfiguration */
    0x80,       /* bmAttributes: bus-powered, no remote wakeup */
    50,         /* bMaxPower: 100 mA (50 × 2 mA) */

    /* Interface Association Descriptor — tells Windows/Linux these two
     * interfaces form a single CDC-ACM function */
    8,          /* bLength */
    0x0B,       /* bDescriptorType: IAD */
    0,          /* bFirstInterface */
    2,          /* bInterfaceCount */
    0x02,       /* bFunctionClass: CDC */
    0x02,       /* bFunctionSubClass: ACM */
    0x01,       /* bFunctionProtocol: AT commands */
    0,          /* iFunction */

    /* CDC Control Interface */
    9,          /* bLength */
    0x04,       /* bDescriptorType: Interface */
    0,          /* bInterfaceNumber */
    0,          /* bAlternateSetting */
    1,          /* bNumEndpoints (notification EP only) */
    0x02,       /* bInterfaceClass: CDC */
    0x02,       /* bInterfaceSubClass: ACM */
    0x01,       /* bInterfaceProtocol: AT commands */
    0,          /* iInterface */

    /* CDC Header Functional Descriptor */
    5,          /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x00,       /* bDescriptorSubtype: Header */
    0x10, 0x01, /* bcdCDC: 1.10 (LE) */

    /* CDC Call Management Functional Descriptor */
    5,          /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x01,       /* bDescriptorSubtype: Call Management */
    0x00,       /* bmCapabilities: device handles call management itself */
    1,          /* bDataInterface */

    /* CDC ACM Functional Descriptor */
    4,          /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x02,       /* bDescriptorSubtype: Abstract Control Management */
    0x02,       /* bmCapabilities: supports Set/Get_Line_Coding, Set_Control_Line_State */

    /* CDC Union Functional Descriptor */
    5,          /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x06,       /* bDescriptorSubtype: Union */
    0,          /* bControlInterface: interface 0 */
    1,          /* bSubordinateInterface0: interface 1 */

    /* Notification Endpoint: EP2 IN, Interrupt, 8 bytes, 10 ms poll */
    7,          /* bLength */
    0x05,       /* bDescriptorType: Endpoint */
    0x82,       /* bEndpointAddress: EP2 IN */
    0x03,       /* bmAttributes: Interrupt */
    8, 0,       /* wMaxPacketSize: 8 (LE) */
    10,         /* bInterval: 10 ms */

    /* CDC Data Interface */
    9,          /* bLength */
    0x04,       /* bDescriptorType: Interface */
    1,          /* bInterfaceNumber */
    0,          /* bAlternateSetting */
    2,          /* bNumEndpoints */
    0x0A,       /* bInterfaceClass: CDC Data */
    0x00,       /* bInterfaceSubClass */
    0x00,       /* bInterfaceProtocol */
    0,          /* iInterface */

    /* Bulk OUT: EP1 OUT, 64 bytes */
    7,          /* bLength */
    0x05,       /* bDescriptorType: Endpoint */
    0x01,       /* bEndpointAddress: EP1 OUT */
    0x02,       /* bmAttributes: Bulk */
    64, 0,      /* wMaxPacketSize: 64 (LE) */
    0,          /* bInterval (ignored for bulk) */

    /* Bulk IN: EP1 IN, 64 bytes */
    7,          /* bLength */
    0x05,       /* bDescriptorType: Endpoint */
    0x81,       /* bEndpointAddress: EP1 IN */
    0x02,       /* bmAttributes: Bulk */
    64, 0,      /* wMaxPacketSize: 64 (LE) */
    0,          /* bInterval */
};

/* ── String Descriptors ───────────────────────────────────────────────────── */

/* String 0: supported LangIDs — English US (0x0409) */
static const uint8_t str0[] = {4, 0x03, 0x09, 0x04};

/* String 1: Manufacturer — "usb-bare-metal" (14 chars → 30 bytes) */
static const uint8_t str1[] = {
    30, 0x03,
    'u',0,'s',0,'b',0,'-',0,'b',0,'a',0,'r',0,'e',0,'-',0,'m',0,'e',0,'t',0,'a',0,'l',0,
};

/* String 2: Product — "STM32 Virtual COM Port" (22 chars → 46 bytes) */
static const uint8_t str2[] = {
    46, 0x03,
    'S',0,'T',0,'M',0,'3',0,'2',0,' ',0,
    'V',0,'i',0,'r',0,'t',0,'u',0,'a',0,'l',0,' ',0,
    'C',0,'O',0,'M',0,' ',0,'P',0,'o',0,'r',0,'t',0,
};

/* String 3: Serial — "00000001" (8 chars → 18 bytes) */
static const uint8_t str3[] = {
    18, 0x03,
    '0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'1',0,
};

static const uint8_t * const str_table[] = {str0, str1, str2, str3};
static const uint16_t        str_len[]   = {4, 30, 46, 18};

const uint8_t *usb_get_string_descriptor(uint8_t index, uint16_t *out_len)
{
    if (index >= 4) return 0;
    *out_len = str_len[index];
    return str_table[index];
}
