# 04 — STM32F411 OTG-FS Peripheral

> **Learning goal:** Map the USB concepts from `00-usb-overview.md` to the
> actual hardware registers you'll be writing to.

Reference: **RM0383**, Chapter 22 "USB on-the-go full-speed (OTG_FS)".

---

## Peripheral Base Address

```c
#define USB_OTG_FS_BASE   0x50000000UL

// Convenience offsets (add to base)
#define GLOBAL_BASE       0x000   // Global CSR
#define DEVICE_BASE       0x800   // Device-mode CSR
#define FIFO_BASE(n)      (0x1000 + (n) * 0x1000)  // Data FIFOs
```

---

## Key Registers You Will Use

### Global registers (offset from `USB_OTG_FS_BASE`)

```
0x000  GOTGCTL   OTG control & status
0x008  GAHBCFG   AHB config (global interrupt enable, burst)
0x00C  GUSBCFG   USB config (force device mode, PHY selection)
0x010  GRSTCTL   Core reset
0x014  GINTSTS   Global interrupt status  [W1C]
0x018  GINTMSK   Global interrupt mask
0x01C  GRXSTSR   RxFIFO status (read without popping)
0x020  GRXSTSP   RxFIFO status (pop — side effect!)
0x024  GRXFSIZ   RxFIFO size (in 32-bit words)
0x028  DIEPTXF0  EP0 TX FIFO size & start address
0x104  DIEPTXF1  EP1 TX FIFO size & start address
0x108  DIEPTXF2  EP2 TX FIFO size & start address
```

### Device registers (offset from `USB_OTG_FS_BASE + 0x800`)

```
0x000  DCFG      Device config (speed, address)
0x004  DCTL      Device control (soft-disconnect, remote wakeup)
0x008  DSTS      Device status (enum speed, frame number)
0x010  DIEPMSK   IN endpoint interrupt mask
0x014  DOEPMSK   OUT endpoint interrupt mask
0x018  DAINT     All endpoint interrupt status
0x01C  DAINTMSK  Endpoint interrupt mask
```

### Endpoint registers (IN endpoints at `0x900 + n*0x20`, OUT at `0xB00 + n*0x20`)

```
// IN EP n
0x900 + n*0x20  DIEPCTLn   IN EP control (enable, disable, NAK, type, MPS)
0x908 + n*0x20  DIEPINTn   IN EP interrupt status  [W1C]
0x910 + n*0x20  DIEPTSIZn  IN EP transfer size (packets, bytes)
0x918 + n*0x20  DTXFSTSn   IN EP TX FIFO space available (words)

// OUT EP n
0xB00 + n*0x20  DOEPCTLn   OUT EP control
0xB08 + n*0x20  DOEPINTn   OUT EP interrupt status  [W1C]
0xB10 + n*0x20  DOEPTSIZn  OUT EP transfer size
```

### Data FIFOs

```
0x1000          FIFO[0]   — EP0 RX and EP0 TX
0x2000          FIFO[1]   — EP1 TX (write) / all RX (read via GRXSTSP)
0x3000          FIFO[2]   — EP2 TX
```

**Important:** The RxFIFO is **shared** among all OUT endpoints. You always
read received data from `FIFO[0]` (address `0x50001000`), regardless of which
endpoint received it. The `GRXSTSP` register tells you which endpoint the
popped data came from.

Each TxFIFO is **per endpoint** and is written to the corresponding
`FIFO[n]` address.

---

## FIFO Sizing

The OTG-FS has a total internal SRAM of **1.25 KB = 320 32-bit words**.
You must partition this between RxFIFO and TxFIFOs. The partition must not
exceed 320 words total.

The sample uses (from `usbd_conf.c`):
```c
HAL_PCDEx_SetRxFiFo(&hpcd, 0x80);   // RxFIFO  = 128 words = 512 bytes
HAL_PCDEx_SetTxFiFo(&hpcd, 0, 0x40); // EP0 TX  =  64 words = 256 bytes
HAL_PCDEx_SetTxFiFo(&hpcd, 1, 0x80); // EP1 TX  = 128 words = 512 bytes
// Total: 128+64+128 = 320 words ✓
```

In bare-metal this translates to:
```c
// RxFIFO: start=0, depth=128 words
USB_OTG_FS->GRXFSIZ = 128;

// EP0 TxFIFO: start=128, depth=64 words
USB_OTG_FS->DIEPTXF0 = (64 << 16) | 128;

// EP1 TxFIFO: start=192, depth=128 words
USB_OTG_FS->DIEPTXF[0] = (128 << 16) | 192;  // DIEPTXF1 is DIEPTXF[0]
```

The FIFO start addresses must be contiguous (each starts where the previous ends).

---

## Interrupt Flow

```
OTG_FS_IRQHandler()
    │
    ├── Read GINTSTS
    │
    ├── RXFLVL set? ──→ Read GRXSTSP (pops one entry from RxFIFO queue)
    │                    ├── PKTSTS == STS_SETUP_UPDT (0x6)  → read 8 bytes → setup_buf
    │                    ├── PKTSTS == STS_DATA_UPDT  (0x2)  → read BCNT bytes → rx_buf
    │                    └── PKTSTS == STS_XFER_COMP  (0x3)  → transfer complete (no data)
    │
    ├── USBRST set? ──→ Reset all endpoint states, re-prime EP0 OUT
    │
    ├── ENUMDNE set? ─→ Read DSTS.ENUMSPD (should be 3 = Full Speed)
    │                   Configure EP0 max packet size
    │
    ├── IEPINT set? ──→ Read DAINT to find which IN EP fired
    │                    ├── EP0: XFRC → status phase done (SET_ADDRESS apply, etc.)
    │                    └── EP1: XFRC → TX complete, clear TxState flag
    │
    └── OEPINT set? ──→ Read DAINT to find which OUT EP fired
                         ├── EP0: STUP → setup packet ready → call usb_handle_setup()
                         └── EP1: XFRC → RX complete → notify app, re-prime EP1 OUT
```

---

## Startup Sequence (bare-metal)

This is the exact sequence needed before any USB activity:

```c
void usb_hw_init(void) {

    // 1. Clock: enable OTG_FS AHB clock
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;

    // 2. GPIO: PA11=DM, PA12=DP → AF10, very high speed, no pull
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER  |=  (2<<22) | (2<<24);    // AF mode
    GPIOA->OSPEEDR|=  (3<<22) | (3<<24);    // Very high speed
    GPIOA->AFR[1] |=  (10<<12) | (10<<16);  // AF10

    // 3. Core reset (wait for AHB idle first)
    while (!(USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_AHBIDL));
    USB_OTG_FS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    while (USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_CSRST);

    // 4. Force device mode, internal FS PHY
    USB_OTG_FS->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD   // force device
                          | USB_OTG_GUSBCFG_PHYSEL;  // internal PHY
    // Wait for mode switch (RM says wait ≥ 25 ms)
    delay_ms(50);

    // 5. Device config: full speed
    USB_OTG_FS_DEVICE->DCFG |= (3 << 0);  // DSPD = 11b = Full Speed

    // 6. FIFO sizes
    USB_OTG_FS->GRXFSIZ        = 128;
    USB_OTG_FS->DIEPTXF0       = (64  << 16) | 128;
    USB_OTG_FS->DIEPTXF[0]     = (128 << 16) | 192;

    // 7. Flush FIFOs
    USB_OTG_FS->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | (0x10 << 6); // flush all TX
    while (USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH);
    USB_OTG_FS->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
    while (USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_RXFFLSH);

    // 8. Clear pending interrupts
    USB_OTG_FS->GINTSTS = 0xFFFFFFFF;

    // 9. Unmask interrupts
    USB_OTG_FS->GINTMSK = USB_OTG_GINTMSK_USBRST   |
                           USB_OTG_GINTMSK_ENUMDNEM  |
                           USB_OTG_GINTMSK_RXFLVLM   |
                           USB_OTG_GINTMSK_IEPINT    |
                           USB_OTG_GINTMSK_OEPINT    ;

    USB_OTG_FS_DEVICE->DIEPMSK = USB_OTG_DIEPMSK_XFRCM;  // IN xfer complete
    USB_OTG_FS_DEVICE->DOEPMSK = USB_OTG_DOEPMSK_XFRCM   // OUT xfer complete
                                | USB_OTG_DOEPMSK_STUPM;  // Setup packet

    // 10. Enable global interrupt
    USB_OTG_FS->GAHBCFG |= USB_OTG_GAHBCFG_GINT;

    // 11. NVIC
    NVIC_SetPriority(OTG_FS_IRQn, 2);
    NVIC_EnableIRQ(OTG_FS_IRQn);

    // 12. Connect — clear soft-disconnect
    USB_OTG_FS_DEVICE->DCTL &= ~USB_OTG_DCTL_SDIS;
}
```

---

## Priming EP0 OUT for Setup Packets

After a USB Reset (and after each SETUP transaction completes), you must
tell the hardware to accept the next setup packet:

```c
void usb_ep0_prime_out(void) {
    USB_OTG_FS_OUTEP(0)->DOEPTSIZ =
        USB_OTG_DOEPTSIZ_STUPCNT_1   |   // expect 1 setup packet (3 words = 8 bytes... but STUPCNT counts back-to-back setups)
        (1 << 19)                    |   // PKTCNT = 1
        64;                              // XFRSIZ = 64 (max for EP0)

    USB_OTG_FS_OUTEP(0)->DOEPCTL |=
        USB_OTG_DOEPCTL_EPENA |
        USB_OTG_DOEPCTL_CNAK;
}
```

---

## Sending Data on EP0 IN

```c
void usb_ep0_send(const uint8_t *buf, uint16_t len, uint16_t max_len) {
    if (len > max_len) len = max_len;

    // 1. Set transfer size
    USB_OTG_FS_INEP(0)->DIEPTSIZ =
        (1 << 19) |   // PKTCNT = 1
        len;          // XFRSIZ

    // 2. Enable EP, clear NAK
    USB_OTG_FS_INEP(0)->DIEPCTL |=
        USB_OTG_DIEPCTL_EPENA |
        USB_OTG_DIEPCTL_CNAK;

    // 3. Write data to TxFIFO word by word
    uint32_t word_count = (len + 3) / 4;
    const uint32_t *src = (const uint32_t *)buf;
    volatile uint32_t *fifo = (volatile uint32_t *)(USB_OTG_FS_BASE + FIFO_BASE(0));
    for (uint32_t i = 0; i < word_count; i++) {
        *fifo = src[i];
    }
}
```

---

## Reading from RxFIFO

```c
// Called when GINTSTS.RXFLVL is set
void usb_rx_fifo_read(void) {
    uint32_t status = USB_OTG_FS->GRXSTSP;  // pops the entry

    uint8_t  epnum  = (status >>  0) & 0x0F;
    uint16_t bcnt   = (status >>  4) & 0x7FF;
    uint8_t  pktsts = (status >> 17) & 0x0F;

    volatile uint32_t *fifo = (volatile uint32_t *)(USB_OTG_FS_BASE + 0x1000);

    if (pktsts == 0x06) {  // STS_SETUP_UPDT — 8 bytes, always EP0
        setup_buf[0] = *fifo;  // bytes 0-3
        setup_buf[1] = *fifo;  // bytes 4-7
    }
    else if (pktsts == 0x02) {  // STS_DATA_UPDT
        uint32_t words = (bcnt + 3) / 4;
        uint32_t *dst = (epnum == 0) ? ep0_rx_buf : ep1_rx_buf;
        for (uint32_t i = 0; i < words; i++) {
            dst[i] = *fifo;
        }
    }
    // pktsts == 0x03 (STS_XFER_COMP) and others: no data to read
}
```

---

## Next

→ [05 — Driver Architecture](05-driver-architecture.md)
