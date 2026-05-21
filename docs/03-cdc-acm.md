# 03 — CDC-ACM: USB Virtual Serial Port

> **Learning goal:** Understand exactly what descriptors and class requests
> make the OS see a virtual serial port — and why each field matters.

Reference: *USB CDC 1.2 Specification* (free at usb.org)

---

## Why CDC-ACM?

CDC-ACM (Abstract Control Model) is the simplest way to create a USB virtual
serial port that works on Windows, Linux, and macOS **without installing any
driver**:

- Linux: built-in `cdc_acm` module → `/dev/ttyACM0`
- Windows 10+: built-in `usbser.sys` → `COMx`
- macOS: built-in `AppleUSBCDCACMData` → `/dev/tty.usbmodemXXX`

The protocol is a thin wrapper over raw bulk endpoints. The serial port
parameters (baud rate, parity, stop bits) that the host sends are essentially
advisory — a USB device doesn't have a real UART, so it can accept any
`SET_LINE_CODING` and silently ignore the values.

---

## Descriptor Structure

CDC-ACM requires a specific descriptor layout. Every byte matters — a wrong
`wTotalLength` or missing functional descriptor causes enumeration failure.

### Full Configuration Block (67 bytes total)

```
[0]  Configuration Descriptor          9 bytes
[1]  Interface Association Descriptor  8 bytes   ← tells host these 2 interfaces are one function
[2]  Control Interface Descriptor      9 bytes   (bInterfaceClass=0x02 CDC, bInterfaceSubClass=0x02 ACM)
[3]  CDC Header Functional             5 bytes
[4]  CDC Call Management Functional    5 bytes
[5]  CDC ACM Functional                4 bytes
[6]  CDC Union Functional              5 bytes
[7]  Notification Endpoint             7 bytes   (EP 0x82, Interrupt IN)
[8]  Data Interface Descriptor         9 bytes   (bInterfaceClass=0x0A CDC-Data)
[9]  Bulk OUT Endpoint                 7 bytes   (EP 0x01)
[10] Bulk IN Endpoint                  7 bytes   (EP 0x81)
```

Total: 9+8+9+5+5+4+5+7+9+7+7 = **75 bytes**

Wait — why 75 not 67? The sample uses 67, some implementations use 75.
The difference is whether the IAD is included. Always include it — Windows
may misbehave without it.

---

## Descriptor Bytes, Field by Field

### Configuration Descriptor (9 bytes)

```c
0x09,         // bLength
0x02,         // bDescriptorType = Configuration
0x4B, 0x00,   // wTotalLength = 75 (all bytes including this descriptor)
0x02,         // bNumInterfaces = 2
0x01,         // bConfigurationValue = 1
0x00,         // iConfiguration = 0 (no string)
0xC0,         // bmAttributes = 0xC0 (self-powered, bit 6; bus-powered = 0x80)
0x32,         // bMaxPower = 50 → 100 mA (units of 2 mA)
```

### Interface Association Descriptor (8 bytes)

Required when one function spans multiple interfaces.

```c
0x08,         // bLength
0x0B,         // bDescriptorType = IAD
0x00,         // bFirstInterface = 0
0x02,         // bInterfaceCount = 2
0x02,         // bFunctionClass = CDC
0x02,         // bFunctionSubClass = ACM
0x01,         // bFunctionProtocol = AT Commands (can be 0x00 = none)
0x00,         // iFunction = 0
```

### Control Interface Descriptor (9 bytes)

```c
0x09,         // bLength
0x04,         // bDescriptorType = Interface
0x00,         // bInterfaceNumber = 0
0x00,         // bAlternateSetting = 0
0x01,         // bNumEndpoints = 1 (notification EP only)
0x02,         // bInterfaceClass = CDC
0x02,         // bInterfaceSubClass = ACM
0x01,         // bInterfaceProtocol = AT Commands
0x00,         // iInterface = 0
```

### CDC Header Functional Descriptor (5 bytes)

```c
0x05,         // bLength
0x24,         // bDescriptorType = CS_INTERFACE (class-specific)
0x00,         // bDescriptorSubtype = Header
0x10, 0x01,   // bcdCDC = 0x0110 = CDC spec 1.10
```

### CDC Call Management Functional Descriptor (5 bytes)

Tells the host whether the device handles call management itself.

```c
0x05,         // bLength
0x24,         // bDescriptorType = CS_INTERFACE
0x01,         // bDescriptorSubtype = Call Management
0x00,         // bmCapabilities = 0x00 (device does not handle call management)
0x01,         // bDataInterface = 1 (data interface number)
```

### CDC ACM Functional Descriptor (4 bytes)

Bitmask of supported class requests:

```c
0x04,         // bLength
0x24,         // bDescriptorType = CS_INTERFACE
0x02,         // bDescriptorSubtype = ACM
0x02,         // bmCapabilities:
              //   bit 0: device supports COMM_FEATURE requests (we don't)
              //   bit 1: device supports LINE_CODING and SERIAL_STATE (we do ✓)
              //   bit 2: device supports SEND_BREAK (we don't)
              //   bit 3: device supports NETWORK_CONNECTION (we don't)
```

### CDC Union Functional Descriptor (5 bytes)

Groups the control and data interfaces.

```c
0x05,         // bLength
0x24,         // bDescriptorType = CS_INTERFACE
0x06,         // bDescriptorSubtype = Union
0x00,         // bControlInterface = 0 (master/control interface)
0x01,         // bSubordinateInterface0 = 1 (data interface)
```

### Notification Endpoint Descriptor (7 bytes)

The CDC spec requires this endpoint even if you never send anything on it.
Some hosts will refuse to configure the device if it's missing.

```c
0x07,         // bLength
0x05,         // bDescriptorType = Endpoint
0x82,         // bEndpointAddress = 0x82 (EP2 IN)
0x03,         // bmAttributes = Interrupt
0x08, 0x00,   // wMaxPacketSize = 8
0x10,         // bInterval = 16 ms (polling interval for Interrupt EP)
```

### Data Interface Descriptor (9 bytes)

```c
0x09,         // bLength
0x04,         // bDescriptorType = Interface
0x01,         // bInterfaceNumber = 1
0x00,         // bAlternateSetting = 0
0x02,         // bNumEndpoints = 2 (bulk IN + bulk OUT)
0x0A,         // bInterfaceClass = CDC-Data
0x00,         // bInterfaceSubClass = 0
0x00,         // bInterfaceProtocol = 0
0x00,         // iInterface = 0
```

### Bulk OUT Endpoint (7 bytes) — host → device

```c
0x07,         // bLength
0x05,         // bDescriptorType = Endpoint
0x01,         // bEndpointAddress = EP1 OUT
0x02,         // bmAttributes = Bulk
0x40, 0x00,   // wMaxPacketSize = 64
0x00,         // bInterval = 0 (ignored for Bulk)
```

### Bulk IN Endpoint (7 bytes) — device → host

```c
0x07,         // bLength
0x05,         // bDescriptorType = Endpoint
0x81,         // bEndpointAddress = EP1 IN
0x02,         // bmAttributes = Bulk
0x40, 0x00,   // wMaxPacketSize = 64
0x00,         // bInterval = 0
```

---

## Class Requests

These arrive as SETUP packets on EP0 after enumeration.

### SET_LINE_CODING (0x20)

Host → Device: 7-byte `LineCoding` structure.

```c
typedef struct {
    uint32_t dwDTERate;    // Baud rate (e.g. 115200) — we can ignore
    uint8_t  bCharFormat;  // Stop bits: 0=1, 1=1.5, 2=2
    uint8_t  bParityType;  // 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
    uint8_t  bDataBits;    // 5, 6, 7, 8, or 16
} __attribute__((packed)) LineCoding;
```

Response: ZLP (zero-length packet) as status phase ACK.

### GET_LINE_CODING (0x21)

Host asks for current line coding. Return the stored `LineCoding` struct.
Initialize it to `{115200, 0, 0, 8}` and update it when SET_LINE_CODING arrives.

### SET_CONTROL_LINE_STATE (0x22)

Host sends 2 bits in `wValue`:
- `bit 0` = DTR (Data Terminal Ready) — **1 = terminal open**
- `bit 1` = RTS (Request To Send)

This is how you know whether a terminal is connected. When DTR goes high,
a terminal has opened the port. Many programs wait for DTR before sending
anything meaningful.

Store the state: `cdc_line_state = setup.wValue & 0x03`.

Response: ZLP.

### Unrecognized CDC requests

Return STALL on EP0 IN (or just ACK with ZLP — the host usually doesn't care).

---

## TX/RX Flow After Enumeration

### Sending (device → host, EP1 IN)

1. Wait for EP1 IN to not be busy (`tx_busy == 0`)
2. Copy data into a staging buffer (or write directly to TxFIFO)
3. Set `DIEPTSIZ1`: byte count + packet count
4. Set `DIEPCTL1`: EPENA + CNAK
5. Write bytes to `FIFO[1]` (word-aligned)
6. Hardware sends the data; fires `IEPINT.EP1.XFRC` when done
7. ISR clears `tx_busy`

For data > 64 bytes: split into 64-byte packets. Set `PKTCNT` accordingly.

### Receiving (host → device, EP1 OUT)

1. After `SET_CONFIGURATION`, prime EP1 OUT (DOEPTSIZ1 + DOEPCTL1)
2. Host sends data; hardware stores it in RxFIFO
3. `GINTSTS.RXFLVL` fires → ISR pops `GRXSTSP`:
   - `epnum=1, pktsts=STS_DATA_UPDT` → read `bcnt` bytes from `FIFO[0]`
4. `OEPINT.EP1.XFRC` fires → re-prime EP1 OUT for next packet
5. Application reads from `rx_buf`

---

## Connection State Machine

```
DISCONNECTED
    │  USB Reset received (GINTSTS.USBRST)
    ▼
RESET
    │  ENUMDNE received (enumeration done)
    ▼
ENUMERATED
    │  SET_CONFIGURATION(1) received on EP0
    ▼
CONFIGURED (endpoints open)
    │  SET_CONTROL_LINE_STATE with DTR=1
    ▼
CONNECTED (terminal open, safe to send data)
    │  DTR=0 (terminal closed) or USB reset
    ▼
CONFIGURED or RESET
```

The important transition is `CONFIGURED → CONNECTED`. Do not send data
before DTR=1 — bytes will be lost because no terminal is reading them.

---

## Common Mistakes

| Symptom | Likely cause |
|---------|--------------|
| Device not recognized | Wrong `wTotalLength` in config descriptor |
| "Unknown Device" | Missing IAD, or wrong `bDeviceClass` |
| `ttyACM0` disappears after open | ACM functional descriptor missing or wrong |
| RX never fires | Forgot to re-prime EP1 OUT after first packet |
| TX works once then stops | `tx_busy` flag never cleared (XFRC interrupt masked) |
| Garbled data on TX | Byte count in `DIEPTSIZ` doesn't match bytes written to FIFO |

---

## Next

→ [04 — STM32F411 OTG-FS Peripheral](04-stm32f411-otg-fs.md)
