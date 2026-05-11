#include "stm32f411xx.h"
#include "usb_cdc.h"

/* ── Bit-field helpers (same subset used in usb_core.c) ──────────────────── */
#define DEPCTL_EPENA  (1U << 31)
#define DEPCTL_CNAK   (1U << 26)
#define DEPCTL_USBAEP (1U << 15)
#define DEPCTL_SD0PID (1U << 28)   /* set DATA0 PID */
#define DEPINT_XFRC   (1U << 0)

/* EPTYPE field in DIEPCTL/DOEPCTL bits [19:18] */
#define EPTYPE_BULK   (2U << 18)
#define EPTYPE_INTR   (3U << 18)

/* ── FIFO layout (all sizes in 32-bit words) ─────────────────────────────── *
 * [  0 .. 127] RxFIFO       — 128 words  (set in usb_hw_init)
 * [128 .. 191] EP0 TxFIFO   —  64 words  (set in usb_hw_init)
 * [192 .. 255] EP1 TxFIFO   —  64 words  (set here)
 * [256 .. 271] EP2 TxFIFO   —  16 words  (set here)
 * Total: 272 / 320 words used
 * ─────────────────────────────────────────────────────────────────────────── */
#define EP1_TXFIFO_START  192U
#define EP1_TXFIFO_DEPTH   64U
#define EP2_TXFIFO_START  256U
#define EP2_TXFIFO_DEPTH   16U

/* ── State ───────────────────────────────────────────────────────────────── */
static volatile uint8_t tx_busy;
static volatile uint8_t cdc_ready;
static volatile uint8_t dtr_set;   /* updated from CDC_SET_CONTROL_LINE_STATE */

/* ── Public API ───────────────────────────────────────────────────────────── */

void usb_cdc_init(void)
{
    /* ── TxFIFO allocation ───────────────────────────────────────────────── */
    /* DIEPTXF(n): bits[31:16] = depth, bits[15:0] = start address (words) */
    USB_OTG_DIEPTXF(1) = (EP1_TXFIFO_DEPTH << 16) | EP1_TXFIFO_START;
    USB_OTG_DIEPTXF(2) = (EP2_TXFIFO_DEPTH << 16) | EP2_TXFIFO_START;

    /* ── EP1 IN — Bulk IN, 64 bytes, TxFIFO 1 ───────────────────────────── */
    USB_OTG_DIEPCTL(1) = 64U           /* MPS = 64 bytes */
                       | DEPCTL_USBAEP /* mark endpoint as active */
                       | EPTYPE_BULK
                       | (1U << 22)    /* TXFNUM = 1 */
                       | DEPCTL_SD0PID;/* start with DATA0 */

    /* ── EP1 OUT — Bulk OUT, 64 bytes (not primed yet; Phase 4 will arm it) */
    USB_OTG_DOEPCTL(1) = 64U
                       | DEPCTL_USBAEP
                       | EPTYPE_BULK
                       | DEPCTL_SD0PID;

    /* ── EP2 IN — Interrupt IN, 8 bytes, TxFIFO 2 (notifications) ────────── */
    USB_OTG_DIEPCTL(2) = 8U
                       | DEPCTL_USBAEP
                       | EPTYPE_INTR
                       | (2U << 22)    /* TXFNUM = 2 */
                       | DEPCTL_SD0PID;

    /* ── Enable endpoint interrupts for EP1 IN only ──────────────────────── *
     * EP1 OUT is intentionally NOT added to DAINTMSK here: handle_oepint()
     * only services EP0, so any masked event on EP1 OUT (e.g. OTEPDIS from a
     * host OUT token to the disabled endpoint) would leave GINTSTS.OEPINT
     * permanently set and lock the ISR in a re-entry loop.  Phase 4 will both
     * prime EP1 OUT and extend handle_oepint() to drain it.
     * EP2 IN is interrupt-IN that the host polls but we never arm; if its
     * interrupt fires we'd have the same problem, so leave it masked too. */
    USB_OTG_DAINTMSK |= (1U << 1);    /* EP1 IN only */

    tx_busy   = 0;
    cdc_ready = 1;
}

void usb_cdc_reset(void)
{
    cdc_ready = 0;
    tx_busy   = 0;
    dtr_set   = 0;
}

void usb_cdc_on_tx_done(void)
{
    tx_busy = 0;
}

void usb_cdc_set_control_line(uint16_t state)
{
    dtr_set = (uint8_t)(state & 0x01U);
}

uint8_t usb_cdc_connected(void)
{
    return cdc_ready && dtr_set;
}

uint8_t usb_cdc_write(const uint8_t *buf, uint16_t len)
{
    /* Non-blocking: drop if not connected, empty, or a TX is still pending.
     * Avoids the deadlock where the host has closed the port and stops issuing
     * IN tokens, so the previous transfer's XFRC never fires. */
    if (!cdc_ready || !dtr_set || len == 0 || tx_busy) return 0;
    tx_busy = 1;

    /* Set packet count (ceil) and transfer size */
    uint32_t pktcnt = (len + 63U) / 64U;
    USB_OTG_DIEPTSIZ(1) = (pktcnt << 19) | len;

    /* Enable EP1 IN */
    USB_OTG_DIEPCTL(1) |= DEPCTL_EPENA | DEPCTL_CNAK;

    /* RM0383 §22.17.5: "To write a single non-zero length data packet, there
     * must be space to write the entire packet in the data FIFO."
     * DTXFSTS(n) reports free 32-bit word locations in TxFIFO n. */
    uint16_t words = (len + 3U) / 4U;
    while (USB_OTG_DTXFSTS(1) < words) {}

    for (uint16_t i = 0; i < words; i++) {
        uint16_t off = i * 4U;
        uint32_t w   = (uint32_t)buf[off];
        if (off + 1U < len) w |= (uint32_t)buf[off + 1U] << 8;
        if (off + 2U < len) w |= (uint32_t)buf[off + 2U] << 16;
        if (off + 3U < len) w |= (uint32_t)buf[off + 3U] << 24;
        USB_OTG_FIFO(1) = w;
    }

    return 1;
}
