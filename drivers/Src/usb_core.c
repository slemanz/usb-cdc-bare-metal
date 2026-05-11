#include "stm32f411xx.h"
#include "usb_core.h"
#include "usb_desc.h"

/* ── Bit-field helpers ────────────────────────────────────────────────────── */

/* GINTMSK / GINTSTS */
#define GINTSTS_RXFLVL  (1U << 4)
#define GINTSTS_USBRST  (1U << 12)
#define GINTSTS_ENUMDNE (1U << 13)
#define GINTSTS_IEPINT  (1U << 18)
#define GINTSTS_OEPINT  (1U << 19)

/* GAHBCFG */
#define GAHBCFG_GINTMSK_RXFLVLM  (1U << 4)
#define GAHBCFG_GINTMSK_IEPINT   (1U << 18)
#define GAHBCFG_GINTMSK_OEPINT   (1U << 19)

/* DIEPMSK */
#define DIEPMSK_XFRCM   (1U << 0)   /* transfer complete */

/* DOEPMSK */
#define DOEPMSK_XFRCM   (1U << 0)   /* transfer complete */
#define DOEPMSK_STUPM   (1U << 3)   /* SETUP phase done */

/* DIEPINT / DOEPINT */
#define DEPINT_XFRC     (1U << 0)
#define DOEPINT_STUP    (1U << 3)   /* SETUP phase done */

/* GRXSTSP packet status codes (bits [20:17]) */
#define GRXSTSP_EPNUM(r)  ((r) & 0xFU)
#define GRXSTSP_BCNT(r)   (((r) >> 4) & 0x7FFU)
#define GRXSTSP_PKTSTS(r) (((r) >> 17) & 0xFU)
#define PKTSTS_OUT_DATA   2U   /* OUT data packet received */
#define PKTSTS_SETUP_COMP 4U   /* SETUP transaction completed */
#define PKTSTS_SETUP_DATA 6U   /* SETUP data packet received */

/* DIEPCTL / DOEPCTL common bits */
#define DEPCTL_EPENA  (1U << 31)
#define DEPCTL_EPDIS  (1U << 30)
#define DEPCTL_CNAK   (1U << 26)
#define DEPCTL_SNAK   (1U << 27)
#define DEPCTL_STALL  (1U << 21)

/* DCTL */
#define DCTL_CGINAK   (1U << 8)   /* clear global IN NAK */

/* GRSTCTL */
#define GRSTCTL_TXFFLSH      (1U << 5)
#define GRSTCTL_TXFNUM_ALL   (0x10U << 6)   /* flush all TX FIFOs */

/* DCFG device address field: bits [10:4] */
#define DCFG_DAD_SHIFT  4U
#define DCFG_DAD_MASK   (0x7FU << 4)

/* Standard USB requests */
#define REQ_GET_DESCRIPTOR      6
#define REQ_SET_ADDRESS         5
#define REQ_SET_CONFIGURATION   9
#define REQ_GET_CONFIGURATION   8

/* CDC class requests (bmRequestType.Type == Class) */
#define CDC_SET_LINE_CODING         0x20
#define CDC_GET_LINE_CODING         0x21
#define CDC_SET_CONTROL_LINE_STATE  0x22

/* bmRequestType direction bit */
#define REQTYPE_DIR_IN  (1U << 7)

/* ── EP0 state machine ────────────────────────────────────────────────────── */

typedef enum {
    EP0_IDLE,
    EP0_DATA_IN,    /* sending descriptor/data to host */
    EP0_DATA_OUT,   /* receiving data from host (SET_LINE_CODING) */
    EP0_STATUS_IN,  /* sending ZLP status to host */
} EP0State;

static EP0State      ep0_state;
static uint32_t      setup_words[2];     /* raw 8-byte setup packet (word-aligned) */
static const uint8_t *ep0_tx_ptr;        /* next byte to send */
static uint16_t      ep0_tx_len;         /* bytes remaining to send */
static uint8_t       ep0_pending_addr;   /* SET_ADDRESS: address to apply after status ZLP */
static uint8_t       line_coding_buf[7]; /* scratch buffer for SET_LINE_CODING data phase */

/* Default line coding: 9600 baud, 8 data bits, no parity, 1 stop bit */
static uint8_t line_coding[7] = {
    0x80, 0x25, 0x00, 0x00, /* dwDTERate: 9600 (LE) */
    0x00,                   /* bCharFormat: 1 stop bit */
    0x00,                   /* bParityType: none */
    0x08,                   /* bDataBits: 8 */
};

/* ── FIFO helpers ─────────────────────────────────────────────────────────── */

/* Write up to `len` bytes to EP0 TxFIFO, padded to whole 32-bit words */
static void fifo_write(const uint8_t *buf, uint16_t len)
{
    uint16_t words = (len + 3U) / 4U;
    for (uint16_t i = 0; i < words; i++) {
        uint16_t off = i * 4U;
        uint32_t w   = (uint32_t)buf[off];
        if (off + 1U < len) w |= (uint32_t)buf[off + 1U] << 8;
        if (off + 2U < len) w |= (uint32_t)buf[off + 2U] << 16;
        if (off + 3U < len) w |= (uint32_t)buf[off + 3U] << 24;
        USB_OTG_FIFO(0) = w;
    }
}

/* Read `bcnt` bytes from EP0 RxFIFO into buf (must read whole words) */
static void fifo_read(uint8_t *buf, uint16_t bcnt)
{
    uint16_t words = (bcnt + 3U) / 4U;
    for (uint16_t i = 0; i < words; i++) {
        uint32_t w   = USB_OTG_FIFO(0);
        uint16_t off = i * 4U;
        if (off     < bcnt) buf[off]     = (uint8_t)(w);
        if (off + 1 < bcnt) buf[off + 1] = (uint8_t)(w >> 8);
        if (off + 2 < bcnt) buf[off + 2] = (uint8_t)(w >> 16);
        if (off + 3 < bcnt) buf[off + 3] = (uint8_t)(w >> 24);
    }
}

/* ── EP0 control primitives ───────────────────────────────────────────────── */

/* Prime EP0 OUT to accept the next OUT packet (STATUS ZLP or DATA or SETUP) */
static void ep0_prime_out(void)
{
    /* STUPCNT=3 back-to-back setups, PKTCNT=1, XFRSIZ=64 */
    USB_OTG_DOEPTSIZ(0) = (3U << 29) | (1U << 19) | 64U;
    USB_OTG_DOEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
}

/* Send one chunk (≤64 bytes) of ep0_tx data; advances ep0_tx_ptr/len */
static void ep0_start_in(void)
{
    uint16_t chunk = (ep0_tx_len > 64U) ? 64U : ep0_tx_len;
    USB_OTG_DIEPTSIZ(0) = (1U << 19) | chunk;          /* PKTCNT=1, XFRSIZ=chunk */
    USB_OTG_DIEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
    fifo_write(ep0_tx_ptr, chunk);
    ep0_tx_ptr += chunk;
    ep0_tx_len -= chunk;
}

/* Arm a DATA_IN transfer (respects wLength limit from setup packet) */
static void ep0_send(const uint8_t *buf, uint16_t len, uint16_t wlength)
{
    if (len > wlength) len = wlength;
    ep0_tx_ptr = buf;
    ep0_tx_len = len;
    ep0_state  = EP0_DATA_IN;
    ep0_start_in();
}

/* Send a zero-length status packet (STATUS_IN phase or no-data response) */
static void ep0_send_zlp(void)
{
    USB_OTG_DIEPTSIZ(0) = (1U << 19) | 0U;             /* PKTCNT=1, XFRSIZ=0 */
    USB_OTG_DIEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
    ep0_state = EP0_STATUS_IN;
}

/* Stall EP0 IN and OUT (used for unsupported/invalid requests) */
static void ep0_stall(void)
{
    USB_OTG_DIEPCTL(0) |= DEPCTL_STALL;
    USB_OTG_DOEPCTL(0) |= DEPCTL_STALL;
    ep0_state = EP0_IDLE;
}

/* ── Setup packet handler ─────────────────────────────────────────────────── */

static void usb_handle_setup(void)
{
    const uint8_t *pkt = (const uint8_t *)setup_words;
    uint8_t  bmRT = pkt[0];
    uint8_t  bReq = pkt[1];
    uint16_t wVal = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
    uint16_t wLen = (uint16_t)pkt[6] | ((uint16_t)pkt[7] << 8);

    /* ── Standard Device/Interface requests ──────────────────────────────── */
    if ((bmRT & 0x60U) == 0x00U) {
        switch (bReq) {

        case REQ_GET_DESCRIPTOR: {
            uint8_t  dtype = (uint8_t)(wVal >> 8);
            uint8_t  didx  = (uint8_t)(wVal & 0xFF);
            const uint8_t *buf = 0;
            uint16_t       blen = 0;

            if (dtype == USB_DESC_TYPE_DEVICE) {
                buf  = usb_device_descriptor;
                blen = sizeof(usb_device_descriptor);
            } else if (dtype == USB_DESC_TYPE_CONFIGURATION) {
                buf  = usb_config_descriptor;
                blen = sizeof(usb_config_descriptor);
            } else if (dtype == USB_DESC_TYPE_STRING) {
                buf = usb_get_string_descriptor(didx, &blen);
            }

            if (buf) {
                ep0_send(buf, blen, wLen);
            } else {
                ep0_stall();   /* device qualifier and other unsupported types */
            }
            break;
        }

        case REQ_SET_ADDRESS:
            /* Address must not be applied until after the STATUS ZLP is sent */
            ep0_pending_addr = (uint8_t)(wVal & 0x7FU);
            ep0_send_zlp();
            break;

        case REQ_SET_CONFIGURATION:
            /* Phase 2: ACK only — endpoints opened in Phase 3 */
            ep0_send_zlp();
            break;

        case REQ_GET_CONFIGURATION: {
            static const uint8_t cfg = 1;
            ep0_send(&cfg, 1, wLen);
            break;
        }

        default:
            ep0_stall();
            break;
        }
        return;
    }

    /* ── CDC class requests (bmRequestType.Type == 0x01) ─────────────────── */
    if ((bmRT & 0x60U) == 0x20U) {
        switch (bReq) {

        case CDC_SET_LINE_CODING:
            /* Host will follow with a 7-byte DATA_OUT; captured in RXFLVL handler */
            ep0_state = EP0_DATA_OUT;
            break;

        case CDC_GET_LINE_CODING:
            ep0_send(line_coding, sizeof(line_coding), wLen);
            break;

        case CDC_SET_CONTROL_LINE_STATE:
            /* DTR = wVal bit 0, RTS = wVal bit 1 — store if needed in Phase 4 */
            ep0_send_zlp();
            break;

        default:
            ep0_stall();
            break;
        }
        return;
    }

    ep0_stall();
}

/* ── Interrupt sub-handlers ───────────────────────────────────────────────── */

static void handle_rxflvl(void)
{
    uint32_t status = USB_OTG_GRXSTSP;   /* pop one entry from RxFIFO status */
    uint8_t  epnum  = (uint8_t)GRXSTSP_EPNUM(status);
    uint16_t bcnt   = (uint16_t)GRXSTSP_BCNT(status);
    uint8_t  pktsts = (uint8_t)GRXSTSP_PKTSTS(status);

    if (epnum != 0) return;   /* only EP0 in Phase 2 */

    switch (pktsts) {
    case PKTSTS_SETUP_DATA:
        /* 8-byte SETUP packet — always exactly 2 words */
        setup_words[0] = USB_OTG_FIFO(0);
        setup_words[1] = USB_OTG_FIFO(0);
        break;

    case PKTSTS_OUT_DATA:
        /* DATA phase of an OUT control transfer (e.g. SET_LINE_CODING, 7 bytes) */
        if (bcnt > 0 && ep0_state == EP0_DATA_OUT) {
            fifo_read(line_coding_buf, bcnt);
        } else if (bcnt > 0) {
            /* Drain FIFO even if we don't use the data */
            fifo_read(line_coding_buf, bcnt);
        }
        break;

    case PKTSTS_SETUP_COMP:
        /* No data — hardware signals setup is done; DOEPINT.STUP will fire */
        break;

    default:
        break;
    }
}

static void handle_usbrst(void)
{
    /* Flush all TX FIFOs */
    USB_OTG_GRSTCTL = GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM_ALL;
    while (USB_OTG_GRSTCTL & GRSTCTL_TXFFLSH) {}

    /* Reset EP0 state */
    ep0_state        = EP0_IDLE;
    ep0_tx_len       = 0;
    ep0_pending_addr = 0;

    /* Unmask EP0 IN and EP0 OUT endpoint interrupts */
    USB_OTG_DAINTMSK |= (1U << 16) | (1U << 0);   /* EP0 OUT | EP0 IN */

    /* Prime EP0 OUT to receive first SETUP packet */
    ep0_prime_out();
}

static void handle_enumdne(void)
{
    /* Full-speed: EP0 MPS = 64 bytes (DIEPCTL0 bits[1:0] = 00) */
    USB_OTG_DIEPCTL(0) &= ~3U;
    /* Clear global IN NAK so EP0 IN can transmit */
    USB_OTG_DCTL |= DCTL_CGINAK;
}

static void handle_iepint(void)
{
    /* Only EP0 in Phase 2 */
    uint32_t diepint = USB_OTG_DIEPINT(0);

    if (diepint & DEPINT_XFRC) {
        USB_OTG_DIEPINT(0) = DEPINT_XFRC;   /* W1C */

        if (ep0_state == EP0_DATA_IN) {
            if (ep0_tx_len > 0) {
                ep0_start_in();   /* send next chunk */
            } else {
                ep0_state = EP0_IDLE;
                /* EP0 OUT already primed (done at STUP time); wait for STATUS ZLP */
            }
        } else if (ep0_state == EP0_STATUS_IN) {
            /* ZLP sent — now apply SET_ADDRESS if pending */
            if (ep0_pending_addr) {
                USB_OTG_DCFG = (USB_OTG_DCFG & ~DCFG_DAD_MASK)
                             | ((uint32_t)ep0_pending_addr << DCFG_DAD_SHIFT);
                ep0_pending_addr = 0;
            }
            ep0_state = EP0_IDLE;
            ep0_prime_out();   /* ready for next SETUP */
        }
    }
}

static void handle_oepint(void)
{
    /* Only EP0 in Phase 2 */
    uint32_t doepint = USB_OTG_DOEPINT(0);

    if (doepint & DOEPINT_STUP) {
        USB_OTG_DOEPINT(0) = DOEPINT_STUP | DEPINT_XFRC;   /* clear both */
        /* Re-prime EP0 OUT BEFORE processing — ensures SETUP can arrive
         * immediately after we start sending the response */
        ep0_prime_out();
        usb_handle_setup();
        return;
    }

    if (doepint & DEPINT_XFRC) {
        USB_OTG_DOEPINT(0) = DEPINT_XFRC;

        if (ep0_state == EP0_DATA_OUT) {
            /* SET_LINE_CODING data received — commit and send status ZLP */
            for (uint8_t i = 0; i < 7; i++) line_coding[i] = line_coding_buf[i];
            ep0_send_zlp();
        } else {
            /* STATUS_OUT ZLP received — transaction complete */
            ep0_state = EP0_IDLE;
            ep0_prime_out();
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void usb_core_init(void)
{
    /* Endpoint-level interrupt masks */
    USB_OTG_DIEPMSK = DIEPMSK_XFRCM;                  /* IN: transfer complete */
    USB_OTG_DOEPMSK = DOEPMSK_XFRCM | DOEPMSK_STUPM;  /* OUT: transfer complete + setup done */

    /* Add RXFLVL, IEPINT, OEPINT to global mask (USBRST+ENUMDNE already set by usb_hw_init) */
    USB_OTG_GINTMSK |= GINTSTS_RXFLVL | GINTSTS_IEPINT | GINTSTS_OEPINT;
}

/* ISR — replaces the Phase 1 stub that was in usb_hw.c */
void OTG_FS_IRQHandler(void)
{
    uint32_t gintsts = USB_OTG_GINTSTS & USB_OTG_GINTMSK;

    if (gintsts & GINTSTS_USBRST) {
        USB_OTG_GINTSTS = GINTSTS_USBRST;
        handle_usbrst();
    }

    if (gintsts & GINTSTS_ENUMDNE) {
        USB_OTG_GINTSTS = GINTSTS_ENUMDNE;
        handle_enumdne();
    }

    if (gintsts & GINTSTS_RXFLVL) {
        /* RXFLVL is level-triggered (reflects FIFO state, not W1C).
         * Mask it while processing to avoid re-entry; re-enabled by returning. */
        USB_OTG_GINTMSK &= ~GINTSTS_RXFLVL;
        handle_rxflvl();
        USB_OTG_GINTMSK |= GINTSTS_RXFLVL;
    }

    if (gintsts & GINTSTS_IEPINT) {
        handle_iepint();
    }

    if (gintsts & GINTSTS_OEPINT) {
        handle_oepint();
    }
}
