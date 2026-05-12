# usb-bare-metal

A bare-metal USB CDC-ACM driver for the WeAct STM32F411CEU6 "Blackpill" board.

No HAL. No middleware. No RTOS. Direct register access.

**Goal:** a USB virtual serial port driver that fits in ≤ 16 KB of flash,
suitable for use inside a bootloader.

## Why this exists

Implementing USB from scratch is a rite of passage for embedded developers,
but the STM32 HAL + ST Middleware stack makes it hard to understand what's
actually happening. This project builds the driver layer by layer, with
documentation explaining every register write.

The `sample/` directory contains the original HAL-based reference implementation
for comparison.

## Documentation

Read these in order before writing any code:

| File | Topic |
|------|-------|
| [docs/00-usb-overview.md](docs/00-usb-overview.md) | USB fundamentals: physical layer, packets, endpoints, transfers |
| [docs/01-usb-enumeration.md](docs/01-usb-enumeration.md) | Enumeration sequence step by step |
| [docs/02-descriptors.md](docs/02-descriptors.md) | Descriptor structure and all required fields |
| [docs/03-cdc-acm.md](docs/03-cdc-acm.md) | CDC-ACM class: descriptors, class requests, TX/RX flow |
| [docs/04-stm32f411-otg-fs.md](docs/04-stm32f411-otg-fs.md) | OTG-FS peripheral: registers, FIFOs, startup sequence |
| [docs/05-driver-architecture.md](docs/05-driver-architecture.md) | Our driver layering and design decisions |
| [docs/06-debugging.md](docs/06-debugging.md) | How to debug bare-metal USB (tools, failure modes, fixes) |

For Claude Code: see [CLAUDE.md](CLAUDE.md) for the full implementation guide
including development phases, coding rules, and register reference.

## Development Phases

| Phase | Goal | Verify |
|-------|------|--------|
| 1 | HW init — D+ pull-up visible, USB reset received | `dmesg` shows "new full-speed USB device" |
| 2 | Enumeration — OS reads descriptors | `dmesg` shows "ttyACM0: USB ACM device" |
| 3 | TX — send data to PC | `cat /dev/ttyACM0` shows "Hello World" |
| 4 | RX — receive data from PC | Echo test works |
| 5 | Polish — size check, edge cases | binary ≤ 16 KB |

## Hardware

- **Board:** WeAct STM32F411CEU6 Blackpill
- **USB:** OTG-FS via PA11 (D−) / PA12 (D+)
- **Crystal:** 25 MHz HSE → 96 MHz SYSCLK, 48 MHz USB clock
- **LED:** PC13 (active low)

## Build

```bash
sudo apt install gcc-arm-none-eabi make openocd
make
make flash   # requires ST-Link
make size    # show flash/RAM usage
```

## Quick Tests

sudo dmesg | tail -10

## References

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification)
- [CDC 1.2 Specification](https://www.usb.org/document-library/class-definitions-communication-devices-12)
- RM0383 — STM32F411 Reference Manual (Chapter 22: OTG-FS)
- [TinyUSB](https://github.com/hathach/tinyusb) — reference bare-metal implementation
