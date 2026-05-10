# 00 — USB Overview

## What is USB?

USB (Universal Serial Bus) is a host-controlled, differential-signaling,
packet-based serial bus. Every transfer is **always initiated by the host**
(your PC). The device only responds.

Key characteristics of USB 2.0 Full Speed (what the STM32F411 OTG-FS uses):

| Property          | Value                         |
|---|---|
| Speed             | 12 Mbit/s                     |
| Signaling         | Differential pair (D+, D−)    |
| Topology          | Star (hub-and-spoke)          |
| Max devices       | 127 per host controller       |
| Transfer control  | Always host-initiated (polling)|
| Cable power       | VBUS = 5 V, up to 500 mA (USB 2.0) |

## The Physical Layer

Two wires carry data: **D+** and **D−**. Logic is encoded as the
*differential* voltage between them:

| State         | D+    | D−    | Meaning                        |
|---|---|---|---|
| J state       | High  | Low   | Idle / logical 1               |
| K state       | Low   | High  | Logical 0                      |
| SE0           | Low   | Low   | End of Packet, Reset           |

**NRZI encoding:** A `0` bit flips the signal; a `1` bit holds it.
Bit stuffing: after 6 consecutive `1`s, a `0` is inserted to guarantee edges
for clock recovery. The receiver removes stuffed bits.

**Full-Speed identification:** The device pulls **D+** high through a 1.5 kΩ
resistor. The host sees D+ high and knows it's a Full Speed device.
On the STM32F411 OTG-FS this pull-up is internal, controlled by
`DCTL.SDIS` (Soft Disconnect). Setting `SDIS=0` connects the pull-up.

## USB Topology

```
PC Host Controller
        │
      Root Hub
     /    |    \
 Hub    Device  Device
  │
 Device
```

Each device gets a **7-bit address** (1–127) assigned by the host during
enumeration. Before assignment, the device responds to address **0**.

## Endpoints

An **endpoint** is a unidirectional data pipe inside the device.

- **EP0** — always bidirectional (IN + OUT), used for control transfers.
  Every device must have EP0.
- **EP1–EP15** — can be IN or OUT, used for data.

Endpoint addresses in USB descriptors:
- IN endpoint (device → host): `0x80 | ep_number`  (e.g. EP1 IN = `0x81`)
- OUT endpoint (host → device): `ep_number`         (e.g. EP1 OUT = `0x01`)

"IN" and "OUT" are always from the **host's** point of view.

## Transfer Types

| Type        | Use case                     | Guaranteed delivery? | Timing? |
|-------------|------------------------------|----------------------|---------|
| Control     | Enumeration, class requests  | Yes                  | No      |
| Bulk        | Large data (CDC data, MSC)   | Yes (retried)        | No      |
| Interrupt   | Small periodic data (HID)    | Yes                  | Yes (polling interval) |
| Isochronous | Audio/video                  | No (no retry)        | Yes     |

For CDC-ACM we use:
- **Control** (EP0) — setup, class requests
- **Bulk** (EP1 IN/OUT) — data up/down
- **Interrupt** (EP2 IN) — CDC notifications (serial state, etc.)

## USB Packets

Every USB transaction consists of packets:

```
[SYNC] [PID] [Address/Data/CRC] [EOP]
```

**PID (Packet ID)** types:

| Category | PID      | Value  | Meaning                         |
|----------|----------|--------|---------------------------------|
| Token    | SETUP    | 0xB4   | Start of setup transaction      |
| Token    | IN       | 0x96   | Host requests data from device  |
| Token    | OUT      | 0x87   | Host sends data to device       |
| Data     | DATA0    | 0xC3   | Data packet (even toggle)       |
| Data     | DATA1    | 0x4B   | Data packet (odd toggle)        |
| Handshake| ACK      | 0x4B   | Packet accepted                 |
| Handshake| NAK      | 0x5A   | Not ready, retry later          |
| Handshake| STALL    | 0x1E   | Error, request not supported    |

A typical **control read** transaction (e.g. GET_DESCRIPTOR):

```
Host  → SETUP token + DATA0 (8-byte setup packet)
Device→ ACK
Host  → IN token
Device→ DATA1 (descriptor data, up to 64 bytes)
Host  → ACK
Host  → OUT token (status stage, zero-length)
Device→ DATA1 (ZLP — zero length packet)
Host  → ACK
```

## The Enumeration Sequence

When a device is plugged in:

1. Host detects D+ pull-up → Full Speed device connected
2. Host issues **USB Reset** (SE0 for ≥ 50 ms)
3. Host sends `GET_DESCRIPTOR(Device, 8 bytes)` to address 0 — reads first 8
   bytes of device descriptor to learn `bMaxPacketSize0`
4. Host issues another reset
5. Host sends `SET_ADDRESS(N)` — device now responds to address N
6. Host sends `GET_DESCRIPTOR(Device, 18 bytes)` — full device descriptor
7. Host sends `GET_DESCRIPTOR(Configuration, 9 bytes)` — reads config header
8. Host sends `GET_DESCRIPTOR(Configuration, wTotalLength)` — full config block
9. Host reads string descriptors (manufacturer, product, serial)
10. Host sends `SET_CONFIGURATION(1)` — device opens data endpoints, ready

Steps 3–10 happen on **EP0 only**. This is why EP0 must work perfectly before
any data transfer is possible.

## What CDC-ACM Is

CDC = **Communications Device Class**
ACM = **Abstract Control Model** (the subclass that emulates RS-232)

CDC-ACM makes the device look like a serial port to the OS:
- Linux: `/dev/ttyACM0`
- Windows: `COMx` (with built-in `usbser.sys` driver, no driver needed)
- macOS: `/dev/tty.usbmodemXXX`

The class has two interfaces:
1. **Control Interface** — carries CDC functional descriptors + notification EP
2. **Data Interface** — carries the actual byte stream (Bulk IN/OUT)

Class-specific requests over EP0:
- `SET_LINE_CODING` — host tells device the baud rate, parity, stop bits
  (we can ignore the actual values for a pure USB device)
- `GET_LINE_CODING` — host reads the current settings
- `SET_CONTROL_LINE_STATE` — host sends DTR/RTS bits