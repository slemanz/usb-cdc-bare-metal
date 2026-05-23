# 05 — Driver Architecture

## Design Goals

1. **Fits in ≤ 16 KB flash** — bootloader budget
2. **No dynamic allocation** — all buffers are statically declared
3. **No OS, no HAL** — direct register access only
4. **Readable** — every non-obvious register write has a comment
5. **Incrementally testable** — each layer can be verified independently

## Layer Diagram

```
┌─────────────────────────────────────────────────────┐
│                 Application (app/main.c)             │
│  usb_cdc_write() / usb_cdc_read() / usb_cdc_connected() │
└───────────────────┬─────────────────────────────────┘
                    │  Public API
┌───────────────────▼─────────────────────────────────┐
│             CDC Layer  (driver/src/usb_cdc.c)        │
│  - Manages TX/RX buffers                             │
│  - Handles CDC class requests (line coding, DTR)     │
│  - Opens/closes bulk + interrupt endpoints           │
└───────────────────┬─────────────────────────────────┘
                    │  Callbacks + endpoint ops
┌───────────────────▼─────────────────────────────────┐
│            Core Layer  (driver/src/usb_core.c)       │
│  - USB Reset / Enumeration FSM                       │
│  - EP0 control pipe (setup packet handler)           │
│  - Descriptor dispatch (GET_DESCRIPTOR)              │
│  - SET_ADDRESS, SET_CONFIGURATION                    │
└───────────────────┬─────────────────────────────────┘
                    │  HW primitives
┌───────────────────▼─────────────────────────────────┐
│           HW Layer  (driver/src/usb_hw.c)            │
│  - Clock + GPIO init                                 │
│  - OTG-FS core init (FIFO sizing, PHY config)        │
│  - FIFO read/write helpers                           │
│  - Endpoint open/close/prime                         │
│  - OTG_FS_IRQHandler (dispatches to core/cdc)        │
└─────────────────────────────────────────────────────┘
```

## File Responsibilities

### `driver/src/usb_hw.c` + `driver/include/usb_hw.h`

What: Direct register access. Knows about OTG-FS but not about USB protocol.

Exports:
```c
void     usb_hw_init(void);
void     usb_hw_ep_open(uint8_t addr, uint8_t type, uint16_t mps);
void     usb_hw_ep_close(uint8_t addr);
void     usb_hw_ep_prime_out(uint8_t epnum, uint8_t *buf, uint16_t len);
void     usb_hw_ep_transmit(uint8_t epnum, const uint8_t *buf, uint16_t len);
void     usb_hw_ep0_stall(void);
uint32_t usb_hw_fifo_read(uint8_t *dst, uint16_t bcnt);
```

Does NOT know:
- What a descriptor is
- What CDC is
- What state the USB enumeration is in

### `driver/src/usb_core.c` + `driver/include/usb_core.h`

What: Enumeration state machine. Understands SETUP packets and standard
USB requests (Chapter 9 of the USB spec).

Exports:
```c
void usb_core_init(void);
void usb_core_reset(void);               // called from ISR on USBRST
void usb_core_enum_done(void);           // called from ISR on ENUMDNE
void usb_core_setup_received(void);      // called from ISR on STUP
void usb_core_ep0_in_done(void);         // called from ISR on EP0 XFRC (IN)
```

Internal:
- `usb_handle_setup()` — decodes `bmRequestType` + `bRequest`
- `usb_send_descriptor()` — sends descriptor data in chunks ≤ 64 bytes
- `pending_addr` — stores address from SET_ADDRESS until status phase

Registers a callback for class-specific requests:
```c
// Set by usb_cdc.c during init
void usb_core_set_class_handler(
    int8_t (*handler)(uint8_t req, uint8_t *buf, uint16_t len)
);
```

### `driver/src/usb_desc.c` + `driver/include/usb_desc.h`

What: Descriptor data stored in flash (`const`). Zero runtime overhead.

Exports:
```c
extern const uint8_t usb_desc_device[18];
extern const uint8_t usb_desc_config[75];
extern const uint8_t usb_desc_str_langid[4];
extern const uint8_t usb_desc_str_manufacturer[];
extern const uint8_t usb_desc_str_product[];
extern const uint8_t usb_desc_str_serial[];

// Helper: find string descriptor by index
const uint8_t *usb_desc_get_string(uint8_t index, uint16_t *len);
```

### `driver/src/usb_cdc.c` + `driver/include/usb_cdc.h`

What: CDC-ACM class logic. Public API for the application.

Exports:
```c
void     usb_cdc_init(void);      // called on SET_CONFIGURATION
int      usb_cdc_write(const uint8_t *buf, uint16_t len);  // returns bytes written
int      usb_cdc_read(uint8_t *buf, uint16_t max_len);     // returns bytes read, -1 if none
int      usb_cdc_connected(void);  // 1 if DTR=1 (terminal open)
uint32_t usb_cdc_get_baud(void);   // current line coding baud rate (informational)
```

Internal:
```c
static uint8_t  tx_buf[64];
static volatile int tx_busy;

static uint8_t  rx_buf[128];
static volatile uint16_t rx_head, rx_tail;

static LineCoding line_coding;
static uint8_t    line_state;   // DTR | RTS
```

## ISR Structure

```c
void OTG_FS_IRQHandler(void) {
    uint32_t gintsts = USB_OTG_FS->GINTSTS & USB_OTG_FS->GINTMSK;

    if (gintsts & USB_OTG_GINTSTS_USBRST) {
        USB_OTG_FS->GINTSTS = USB_OTG_GINTSTS_USBRST;
        usb_core_reset();
    }

    if (gintsts & USB_OTG_GINTSTS_ENUMDNE) {
        USB_OTG_FS->GINTSTS = USB_OTG_GINTSTS_ENUMDNE;
        usb_core_enum_done();
    }

    if (gintsts & USB_OTG_GINTSTS_RXFLVL) {
        // RXFLVL is level-triggered, cleared by reading GRXSTSP
        usb_hw_handle_rxflvl();
        // This function reads GRXSTSP and dispatches:
        // - STS_SETUP_UPDT → stores setup bytes
        // - STS_DATA_UPDT  → reads data into appropriate buffer
    }

    if (gintsts & USB_OTG_GINTSTS_IEPINT) {
        uint32_t daint = USB_OTG_FS_DEVICE->DAINT & 0xFFFF;  // IN ep bits
        if (daint & (1 << 0)) usb_core_ep0_in_done();
        if (daint & (1 << 1)) usb_cdc_tx_done();
    }

    if (gintsts & USB_OTG_GINTSTS_OEPINT) {
        uint32_t daint = USB_OTG_FS_DEVICE->DAINT >> 16;    // OUT ep bits
        if (daint & (1 << 0)) {
            uint32_t doepint = USB_OTG_FS_OUTEP(0)->DOEPINT;
            USB_OTG_FS_OUTEP(0)->DOEPINT = doepint;
            if (doepint & USB_OTG_DOEPINT_STUP) usb_core_setup_received();
            if (doepint & USB_OTG_DOEPINT_XFRC) usb_hw_ep0_prime_out();
        }
        if (daint & (1 << 1)) {
            uint32_t doepint = USB_OTG_FS_OUTEP(1)->DOEPINT;
            USB_OTG_FS_OUTEP(1)->DOEPINT = doepint;
            if (doepint & USB_OTG_DOEPINT_XFRC) usb_cdc_rx_done();
        }
    }
}
```

## State Machine

```
usb_state_t:
  USB_STATE_RESET        (after init or bus reset)
  USB_STATE_ENUMERATED   (after ENUMDNE)
  USB_STATE_ADDRESSED    (after SET_ADDRESS)
  USB_STATE_CONFIGURED   (after SET_CONFIGURATION → endpoints open)
```

Global variable: `static volatile usb_state_t g_usb_state;`

The CDC layer checks `g_usb_state == USB_STATE_CONFIGURED` before writing.

## Memory Budget Analysis

Estimated flash usage:

| Module        | Estimated size |
|---------------|---------------|
| `usb_hw.c`    | ~800 bytes    |
| `usb_core.c`  | ~1.2 KB       |
| `usb_desc.c`  | ~300 bytes (data in flash) |
| `usb_cdc.c`   | ~600 bytes    |
| ISR + vectors | ~200 bytes    |
| Startup code  | ~200 bytes    |
| **Total**     | **~3.3 KB**   |

With a simple application (echo + LED blink) the whole binary should be
well under 8 KB — leaving plenty of margin for the 16 KB bootloader budget.

Estimated RAM usage:

| Buffer        | Size     |
|---------------|----------|
| `tx_buf`      | 64 bytes |
| `rx_buf`      | 128 bytes|
| `setup_buf`   | 8 bytes  |
| `ep0_buf`     | 64 bytes |
| `line_coding` | 7 bytes  |
| Stack         | ~1 KB    |
| **Total**     | **~1.3 KB** |