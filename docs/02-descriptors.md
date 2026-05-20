# USB Descriptors

Descriptors are read-only data structures stored in flash. The host reads them
during enumeration to learn the device's identity, configuration, and
capabilities. All multi-byte fields are **little-endian**.

---

## Device Descriptor (18 bytes)

One per device. Identifies the device to the host.

```c
const uint8_t device_desc[] = {
    0x12,       // bLength = 18
    0x01,       // bDescriptorType = DEVICE
    0x00, 0x02, // bcdUSB = 0x0200 (USB 2.0)
    0x02,       // bDeviceClass = CDC
    0x00,       // bDeviceSubClass
    0x00,       // bDeviceProtocol
    0x40,       // bMaxPacketSize0 = 64 (EP0 max packet, full-speed)
    0x83, 0x04, // idVendor  = 0x0483 (STMicroelectronics)
    0x40, 0x57, // idProduct = 0x5740 (ST Virtual COM Port clone)
    0x00, 0x02, // bcdDevice = 0x0200
    0x01,       // iManufacturer = string index 1
    0x02,       // iProduct      = string index 2
    0x03,       // iSerialNumber = string index 3
    0x01,       // bNumConfigurations = 1
};
```

**Key fields:**
- `bDeviceClass = 0x02` tells the host this is a CDC device (class defined at
  device level, not interface level — required for Windows CDC-ACM)
- `bMaxPacketSize0 = 64` — EP0 can handle up to 64 bytes per transaction

---

## Configuration Descriptor Block (67 bytes total)

One configuration. Contains all interface and endpoint descriptors
concatenated. The host reads the whole block in one `GET_DESCRIPTOR` request.

### Config Header (9 bytes)

```c
0x09,       // bLength
0x02,       // bDescriptorType = CONFIGURATION
0x43, 0x00, // wTotalLength = 67 (all descriptors combined)
0x02,       // bNumInterfaces = 2 (control + data)
0x01,       // bConfigurationValue = 1
0x00,       // iConfiguration = no string
0xC0,       // bmAttributes = self-powered
0x32,       // bMaxPower = 100 mA (50 × 2 mA)
```

### Interface Association Descriptor — IAD (8 bytes)

Required on Windows for CDC devices that span multiple interfaces.

```c
0x08,       // bLength
0x0B,       // bDescriptorType = IAD
0x00,       // bFirstInterface = 0
0x02,       // bInterfaceCount = 2
0x02,       // bFunctionClass = CDC
0x02,       // bFunctionSubClass = ACM
0x01,       // bFunctionProtocol = AT commands
0x00,       // iFunction
```

### CDC Control Interface (9 bytes)

```c
0x09,       // bLength
0x04,       // bDescriptorType = INTERFACE
0x00,       // bInterfaceNumber = 0
0x00,       // bAlternateSetting
0x01,       // bNumEndpoints = 1 (notification EP)
0x02,       // bInterfaceClass = CDC
0x02,       // bInterfaceSubClass = ACM
0x01,       // bInterfaceProtocol = AT commands
0x00,       // iInterface
```

### CDC Functional Descriptors (19 bytes total)

These are mandatory for the host to load `cdc_acm`. Missing even one of these
causes `ttyACM0` to disappear immediately after appearing.

```c
// Header Functional (5 bytes)
0x05, 0x24, 0x00, 0x10, 0x01,

// Call Management (5 bytes) — we don't handle calls, set bmCapabilities=0
0x05, 0x24, 0x01, 0x00, 0x01,

// ACM Functional (4 bytes) — bmCapabilities=0x02: supports SET/GET_LINE_CODING
0x04, 0x24, 0x02, 0x02,

// Union Functional (5 bytes) — control interface=0, data interface=1
0x05, 0x24, 0x06, 0x00, 0x01,
```

### Notification Endpoint (7 bytes)

```c
0x07,       // bLength
0x05,       // bDescriptorType = ENDPOINT
0x82,       // bEndpointAddress = EP2 IN (0x80 | 2)
0x03,       // bmAttributes = Interrupt
0x08, 0x00, // wMaxPacketSize = 8
0x0A,       // bInterval = 10 ms
```

This endpoint is required by the CDC spec but we never send anything on it.
The host must still see it in the descriptor or enumeration fails.

### CDC Data Interface (9 bytes)

```c
0x09,       // bLength
0x04,       // bDescriptorType = INTERFACE
0x01,       // bInterfaceNumber = 1
0x00,       // bAlternateSetting
0x02,       // bNumEndpoints = 2 (bulk IN + bulk OUT)
0x0A,       // bInterfaceClass = CDC Data
0x00,       // bInterfaceSubClass
0x00,       // bInterfaceProtocol
0x00,       // iInterface
```

### Bulk OUT Endpoint (7 bytes) — host→device

```c
0x07,       // bLength
0x05,       // bDescriptorType = ENDPOINT
0x01,       // bEndpointAddress = EP1 OUT
0x02,       // bmAttributes = Bulk
0x40, 0x00, // wMaxPacketSize = 64
0x00,       // bInterval (ignored for bulk)
```

### Bulk IN Endpoint (7 bytes) — device→host

```c
0x07,       // bLength
0x05,       // bDescriptorType = ENDPOINT
0x81,       // bEndpointAddress = EP1 IN (0x80 | 1)
0x02,       // bmAttributes = Bulk
0x40, 0x00, // wMaxPacketSize = 64
0x00,       // bInterval (ignored for bulk)
```

---

## String Descriptors

All strings use UTF-16LE encoding. Each descriptor starts with a 2-byte header.

```c
// String 0 — Language ID list
const uint8_t str0[] = {
    0x04,       // bLength = 4
    0x03,       // bDescriptorType = STRING
    0x09, 0x04, // wLANGID[0] = 0x0409 (English US)
};

// String 1 — Manufacturer
// String 2 — Product
// String 3 — Serial Number
// Each: bLength = 2 + (strlen * 2), bDescriptorType = 0x03, then UTF-16LE chars
```

**Helper macro for UTF-16LE strings:**
```c
#define USB_STRING(s) \
    { 2 + sizeof(s) - 2, 0x03, s }
// where s is a wide-char literal: u"STM32 Virtual COM Port"
```

---

## Descriptor Layout Summary

```
Total config block = 9 + 8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7 = 75 bytes
```

Wait — the standard CDC-ACM layout above is **67 bytes**. Recount:

```
Config header:          9
IAD:                    8
Control interface:      9
  Header functional:    5
  Call management:      5
  ACM functional:       4
  Union functional:     5
Notification EP:        7
Data interface:         9
Bulk OUT EP:            7
Bulk IN EP:             7
─────────────────────────
Total:                 75 bytes  ← use this as wTotalLength
```

> **Note:** CLAUDE.md originally listed 67 bytes — that was missing the IAD (8
> bytes). The correct total for IAD + all CDC functional descriptors is **75**.
> `wTotalLength` must match exactly or the host will truncate or reject.

---

## Descriptor Type Constants

| Name | Value |
|------|-------|
| DEVICE | 0x01 |
| CONFIGURATION | 0x02 |
| STRING | 0x03 |
| INTERFACE | 0x04 |
| ENDPOINT | 0x05 |
| IAD | 0x0B |
| CS_INTERFACE (CDC functional) | 0x24 |
