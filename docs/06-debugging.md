# 06 — Debugging Bare-Metal USB

## The Debugging Stack (use in order)

### Level 1 — LED Blink Codes (free, always works)

Before you have any other debug channel, use the PC13 LED (active low on Blackpill).

```c
// Blink N times with a pause between groups
void led_blink(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        GPIOC->BSRR = (1 << (13 + 16));  // LED on
        delay_ms(100);
        GPIOC->BSRR = (1 << 13);         // LED off
        delay_ms(100);
    }
    delay_ms(500);
}
```

Use it to signal which state the ISR reached:
- 1 blink = USB Reset received ✓
- 2 blinks = ENUMDNE received ✓
- 3 blinks = GET_DESCRIPTOR(Device) received ✓
- 4 blinks = SET_CONFIGURATION received ✓
- 5 blinks = first TX done ✓

### Level 2 — `dmesg -w` on Linux

```bash
sudo dmesg -w
```

Plug and unplug the device. You will see:

```
# Good — device enumerated:
usb 1-1: new full-speed USB device number 5 using xhci_hcd
usb 1-1: New USB device found, idVendor=0483, idProduct=5740, bcdDevice= 2.00
usb 1-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
usb 1-1: Product: STM32 Virtual COM Port
usb 1-1: Manufacturer: usb-bare-metal
usb 1-1: SerialNumber: 00000001
cdc_acm 1-1:1.0: ttyACM0: USB ACM device

# Bad — descriptor read failed:
usb 1-1: new full-speed USB device number 5 using xhci_hcd
usb 1-1: device descriptor read/64, error -71

# Bad — enumerated but no CDC:
usb 1-1: New USB device found, idVendor=0483, idProduct=5740
usb 1-1: config 1 has an invalid interface number: 0 but max is 0

# Bad — device disconnected immediately:
usb 1-1: USB disconnect, device number 5
```

**Error -71** = `EPROTO` — protocol error. The device responded but sent
garbage. Usually means EP0 OUT was not primed, or the descriptor length is wrong.

**"Invalid interface number"** — `bNumInterfaces` doesn't match the actual
interface descriptors, or interfaces are not numbered 0, 1, 2... sequentially.

### Level 3 — USBPcap + Wireshark (Windows)

Install USBPcap, start a capture, plug the device.
In Wireshark filter: `usb.addr == "x.y.0"` (replace x.y with your device's bus.device).

You will see each SETUP packet and the response. This is invaluable for:
- Verifying the exact bytes of each descriptor the device sends
- Catching truncated descriptors (wrong length)
- Seeing which request the host sends right before enumeration fails

### Level 4 — SWD + GDB Breakpoints

```bash
# In one terminal:
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# In another:
arm-none-eabi-gdb build/firmware.elf
(gdb) target remote :3333
(gdb) break OTG_FS_IRQHandler
(gdb) continue
```

Inside the ISR, inspect `GINTSTS`:
```gdb
(gdb) p/x *(volatile uint32_t*)0x50000014
```

Set conditional breakpoints:
```gdb
(gdb) break usb_handle_setup if setup_buf.bRequest == 5  # SET_ADDRESS
```

**Warning:** SWD debugging affects USB timing at Full Speed (12 Mbit/s).
Simple setup packet handling is fine, but SOF timing may drift.
If you see intermittent behavior, test without the debugger attached.

### Level 5 — Logic Analyzer

A cheap 8-channel logic analyzer (Cypress FX2 clone, ~$10) with
[PulseView](https://sigrok.org/wiki/PulseView) shows raw USB packets.

- Probe PA11 (D−) and PA12 (D+)
- Set protocol decoder to "USB Full Speed"
- Sample at ≥ 24 MHz

This lets you see SYNC, PID, address, endpoint, data, CRC at the bit level.
Essential when the host doesn't even attempt enumeration.

## Failure Modes and Fixes

### "Device not recognized" immediately after plug-in

The host couldn't even start enumeration.

Checklist:
- [ ] Is `DCTL.SDIS = 0`? (soft-disconnect OFF = pull-up connected)
- [ ] Is the 48 MHz USB clock correct? Check PLLQ.
  `RCC->CFGR & RCC_CFGR_PPRE1` should give PCLK1 = 48 MHz.
- [ ] Are PA11/PA12 configured as AF10? Check `GPIOA->MODER` and `GPIOA->AFR`.
- [ ] Is `USB_OTG_FS_CLK` enabled in `RCC->AHB2ENR`?

### Device attempts enumeration then fails (-71 EPROTO)

The host sent a setup packet, the device didn't respond properly.

Checklist:
- [ ] Is `GINTMSK.RXFLVLM` set? (unmasked)
- [ ] Is EP0 OUT primed after USB Reset? (`DOEPCTL0.EPENA + CNAK`)
- [ ] Does `DOEPTSIZ0.STUPCNT` have room for at least 1 setup packet?
- [ ] Is `GINTMSK.OEPINT` set?
- [ ] Is `DOEPMSK.STUPM` set? (unmask SETUP packet interrupt for OUT EPs)

### Enumeration starts but OS doesn't recognize as serial port

The device descriptor was read, but configuration failed.

Checklist:
- [ ] Is `wTotalLength` in the config descriptor correct (exact byte count)?
- [ ] Is the IAD present (bDescriptorType = 0x0B)?
- [ ] Are CDC functional descriptors in the right order (Header, CM, ACM, Union)?
- [ ] Is `bInterfaceClass = 0x02` on the control interface?
- [ ] Is `bInterfaceClass = 0x0A` on the data interface?
- [ ] Does `bNumInterfaces = 2` in the config descriptor match?

### `ttyACM0` appears then disappears

The OS loaded the driver but something went wrong.

Checklist:
- [ ] Notification endpoint (EP2 IN, Interrupt) descriptor is present
- [ ] `bmCapabilities` in ACM functional descriptor = 0x02

### TX works, RX never fires

Checklist:
- [ ] Is EP1 OUT primed? (`DOEPTSIZ1` + `DOEPCTL1.EPENA + CNAK`)
- [ ] Is `GINTMSK.OEPINT` set?
- [ ] Is `DAINTMSK` bit for EP1 OUT set? (`DAINTMSK |= (1 << 17)`)
- [ ] In the ISR, after XFRC for EP1 OUT: is EP1 OUT re-primed?

### TX data is garbage

Checklist:
- [ ] Does `DIEPTSIZ1.XFRSIZ` match the number of bytes you write to the FIFO?
- [ ] Are you writing complete 32-bit words? (pad the last word if `len % 4 != 0`)
- [ ] Is `DIEPTSIZ1.PKTCNT = ceil(len / 64)`?

### TX works once then stops forever

Checklist:
- [ ] Is `IEPINT.EP1.XFRC` cleared in the ISR? (`DIEPINT1 = XFRC`)
- [ ] Is `tx_busy` flag cleared in the ISR?
- [ ] Is `DIEPMSK.XFRCM` set? (unmask TX complete interrupt)
- [ ] Is `DAINTMSK` bit for EP1 IN set? (`DAINTMSK |= (1 << 1)`)

---

## Useful Register Reads During Debugging

Paste these in GDB to inspect USB state:

```bash
# Global interrupt status (what's pending)
p/x *(volatile uint32_t*)0x50000014

# Device status (enumeration speed: 3 = FS)
p/x *(volatile uint32_t*)0x50000808

# EP0 IN control
p/x *(volatile uint32_t*)0x50000900

# EP0 OUT control
p/x *(volatile uint32_t*)0x50000B00

# EP1 IN control
p/x *(volatile uint32_t*)0x50000920

# EP1 OUT control
p/x *(volatile uint32_t*)0x50000B20

# All endpoint interrupt status
p/x *(volatile uint32_t*)0x50000818

# RxFIFO status (peek without pop)
p/x *(volatile uint32_t*)0x5000001C
```

## Systematic Testing Protocol

For each phase, use this checklist before declaring success:

**Phase 1 (HW init):**
- [ ] LED blinks at boot (startup code works)
- [ ] `dmesg` shows "new full-speed USB device" (D+ pull-up active)
- [ ] Enumeration attempt visible in `dmesg` (host sent reset)

**Phase 2 (enumeration):**
- [ ] `dmesg` shows correct VID/PID/product strings
- [ ] `dmesg` shows `ttyACM0: USB ACM device`
- [ ] `/dev/ttyACM0` exists

**Phase 3 (TX):**
- [ ] `cat /dev/ttyACM0` shows "Hello World" every second
- [ ] `screen /dev/ttyACM0 115200` shows the same
- [ ] Works on both Linux and Windows (or at least Linux first)

**Phase 4 (RX):**
- [ ] `echo "test" > /dev/ttyACM0` → firmware echoes back
- [ ] Sending 1 byte, 63 bytes, 64 bytes, 65 bytes all work correctly
- [ ] Rapid TX+RX simultaneously works (stress test)

**Phase 5 (polish):**
- [ ] `arm-none-eabi-size firmware.elf` shows `.text` + `.rodata` ≤ 16384 bytes
- [ ] `usb_cdc_connected()` returns 0 before terminal open, 1 after
- [ ] USB unplug and replug works without MCU reset