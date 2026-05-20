# USB Enumeration

Enumeration is the process by which the host discovers what a device is and
loads the right driver. It happens every time a device is plugged in.

## 1. Physical Connection

When the device connects, it pulls D+ high through a 1.5 kΩ resistor. The host
detects this within a few milliseconds and knows a full-speed device is present.

On the STM32F411 OTG-FS, this pull-up is controlled in software:
- `DCTL |= DCTL_SDIS` → soft-disconnect (D+ pulled low, host sees nothing)
- `DCTL &= ~DCTL_SDIS` → connect (D+ pull-up active, host sees device)

## 2. USB Reset

The host asserts SE0 (both D+ and D− low) for at least 10 ms. This resets the
device to its default state:

- Device address → 0
- All endpoints except EP0 disabled
- EP0 ready to receive setup packets

The OTG-FS fires `GINTSTS_USBRST` at the start of reset. The driver must:
1. Clear endpoint state
2. Re-enable EP0 OUT to receive the first SETUP packet

## 3. Speed Negotiation (Enumeration Done)

After reset the host sends a short SOF burst to determine speed. The OTG-FS
fires `GINTSTS_ENUMDNE`. Read `DSTS_ENUMSPD`:
- `0b11` = full-speed (48 MHz, 12 Mbit/s) — what we expect
- `0b01` = high-speed — not supported by OTG-FS internal PHY

Set EP0 max packet size based on the negotiated speed (64 bytes for full-speed).

## 4. Host Reads Device Descriptor (partial)

The host sends a `GET_DESCRIPTOR(Device, length=64)` setup packet to address 0,
EP0. It only reads the first 8 bytes to learn `bMaxPacketSize0`.

Driver response:
1. SETUP packet arrives via RxFIFO → decode in ISR
2. Send first 8 bytes of device descriptor via EP0 IN
3. Host sends a zero-length STATUS OUT to acknowledge

## 5. Host Issues Reset Again

After reading the partial device descriptor the host issues a second USB reset.
This is normal — it re-synchronises before assigning an address.

## 6. SET_ADDRESS

The host assigns a non-zero address (1–127):

```
SETUP: bmRequestType=0x00, bRequest=SET_ADDRESS, wValue=<addr>
```

**Important:** the new address must NOT be applied until after the STATUS phase
(zero-length IN) completes. Apply it in the `XFRC` interrupt for EP0 IN:

```c
USB_OTG_FS->DCFG = (USB_OTG_FS->DCFG & ~USB_OTG_DCFG_DAD) | (addr << 4);
```

## 7. Host Reads Full Device Descriptor

Now at the assigned address, the host re-reads the full 18-byte device
descriptor (`GET_DESCRIPTOR(Device, length=18)`).

## 8. Host Reads Configuration Descriptor

`GET_DESCRIPTOR(Configuration, length=255)` — host asks for up to 255 bytes.
The device sends the full configuration block (67 bytes for CDC-ACM).

The host reads this to learn:
- Number of interfaces
- Endpoint addresses and types
- CDC functional descriptors (required for ACM)

## 9. Host Reads String Descriptors

Optional but expected. Host sends `GET_DESCRIPTOR(String, index=N)`:
- Index 0 → list of supported language IDs
- Index 1 → Manufacturer string
- Index 2 → Product string
- Index 3 → Serial number string

## 10. SET_CONFIGURATION

```
SETUP: bmRequestType=0x00, bRequest=SET_CONFIGURATION, wValue=1
```

The host selects configuration 1 (our only configuration). The device must:
1. Open all data endpoints (EP1 IN/OUT, EP2 IN)
2. Prime EP1 OUT to receive the first bulk data packet
3. ACK with zero-length STATUS IN

After this the device is fully enumerated and ready to transfer data.

## Enumeration State Machine (summary)

```
PLUGGED IN
    │
    ▼
USB RESET received (GINTSTS_USBRST)
    │  → reset EP0, re-prime OUT
    ▼
ENUMDNE (GINTSTS_ENUMDNE)
    │  → set EP0 max packet = 64
    ▼
GET_DESCRIPTOR(Device) → send 18 bytes
    ▼
USB RESET again (normal)
    ▼
SET_ADDRESS → stage addr, apply after STATUS IN
    ▼
GET_DESCRIPTOR(Device) again → send 18 bytes
    ▼
GET_DESCRIPTOR(Configuration) → send 67 bytes
    ▼
GET_DESCRIPTOR(String) × N
    ▼
SET_CONFIGURATION → open EP1/EP2, prime OUT
    ▼
ENUMERATED — ttyACM0 appears on host
```

## Common Enumeration Bugs

| Symptom | Likely cause |
|---------|-------------|
| Host sees reset, then nothing | EP0 OUT not re-primed after USB reset |
| `error -71` in dmesg forever | Device descriptor never sent (EP0 IN not armed) |
| Enumeration starts, then fails | Config descriptor total length wrong |
| `ttyACM0` appears then disappears | CDC functional descriptors missing or wrong |
| SET_ADDRESS seems ignored | Address applied during SETUP phase, not after STATUS |
