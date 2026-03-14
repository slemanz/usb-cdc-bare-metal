#include "driver_usb.h"
#include "driver_gpio.h"
#include "driver_interrupt.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  Register bit masks and constants
 * ═══════════════════════════════════════════════════════════════════════════ */

 /* GAHBCFG */
#define GAHBCFG_GINTMSK         (1U << 0)

/* GUSBCFG */
#define GUSBCFG_PHYSEL          (1U << 6)
#define GUSBCFG_FDMOD           (1U << 30)

/* GRSTCTL */
#define GRSTCTL_CSRST           (1U << 0)
#define GRSTCTL_RXFFLSH         (1U << 4)
#define GRSTCTL_TXFFLSH         (1U << 5)
#define GRSTCTL_TXFNUM_POS      6
#define GRSTCTL_AHBIDL          (1U << 31)

/* GINTSTS / GINTMSK – global interrupt flags */
#define GINT_RXFLVL             (1U << 4)    /* RX FIFO non-empty */
#define GINT_USBSUSP            (1U << 11)   /* Suspend */
#define GINT_USBRST             (1U << 12)   /* Bus reset from host */
#define GINT_ENUMDNE            (1U << 13)   /* Enumeration done */
#define GINT_IEPINT             (1U << 18)   /* IN endpoint event */
#define GINT_OEPINT             (1U << 19)   /* OUT endpoint event */
#define GINT_WKUPINT            (1U << 31)   /* Wakeup */

/* GRXSTSP – status register fields (pop) */
#define RXSTS_EPNUM_MSK         0x0FU
#define RXSTS_BCNT_POS          4
#define RXSTS_BCNT_MSK          (0x7FFU << 4)
#define RXSTS_PKTSTS_POS        17
#define RXSTS_PKTSTS_MSK        (0x0FU << 17)
#define PKTSTS_SETUP_DATA       6U
#define PKTSTS_OUT_DATA         2U

/* GCCFG */
#define GCCFG_PWRDWN            (1U << 16)
#define GCCFG_NOVBUSSENS        (1U << 21)

/* DCFG */
#define DCFG_DSPD_FS            (0x3U << 0)
#define DCFG_DAD_POS            4
#define DCFG_DAD_MSK            (0x7FU << 4)

/* DCTL */
#define DCTL_SDIS               (1U << 1)

/* DIEPCTLn / DOEPCTLn – common endpoint control register bits */
#define DEPCTL_MPS_MSK          0x7FFU
#define DEPCTL_USBAEP           (1U << 15)
#define DEPCTL_EPTYP_POS        18
#define DEPCTL_STALL            (1U << 21)
#define DEPCTL_TXFNUM_POS       22
#define DEPCTL_CNAK             (1U << 26)
#define DEPCTL_SNAK             (1U << 27)
#define DEPCTL_SD0PID           (1U << 28)
#define DEPCTL_EPENA            (1U << 31)

/* DIEPINTn / DOEPINTn */
#define DEPINT_XFRC             (1U << 0)    /* Transfer complete */
#define DEPINT_STUP             (1U << 3)    /* SETUP phase done (OUT EP0) */

/* DIEPTSIZn / DOEPTSIZn */
#define DEPTSIZ_PKTCNT_POS      19
#define DEPTSIZ_STUPCNT_POS     29

/* DAINTMSK helpers */
#define DAINT_IN(n)             (1U << (n))
#define DAINT_OUT(n)            (1U << ((n) + 16))

/* Sizes */
#define EP0_MPS                 64U
#define CDC_MPS                 64U
#define CDC_NOTIFY_MPS          8U

/* FIFO sizes in 32-bit words. Total = 320 words (1.25 KB) */
#define RX_FIFO_SZ              128U   /* shared by all OUT EPs */
#define TX0_FIFO_SZ             64U    /* EP0 IN */
#define TX1_FIFO_SZ             64U    /* EP1 IN (CDC bulk) */
#define TX2_FIFO_SZ             64U    /* EP2 IN (CDC notify) */

/* Endpoint numbers used by CDC */
#define CDC_IN_EP               1U
#define CDC_OUT_EP              1U
#define CDC_NOTIFY_EP           2U

/* USB standard request codes */
#define REQ_GET_STATUS          0x00U
#define REQ_CLEAR_FEATURE       0x01U
#define REQ_SET_FEATURE         0x03U
#define REQ_SET_ADDRESS         0x05U
#define REQ_GET_DESCRIPTOR      0x06U
#define REQ_GET_CONFIGURATION   0x08U
#define REQ_SET_CONFIGURATION   0x09U

/* Descriptor types */
#define DESC_DEVICE             0x01U
#define DESC_CONFIGURATION      0x02U
#define DESC_STRING             0x03U
#define DESC_DEVICE_QUALIFIER   0x06U

/* CDC class request codes */
#define CDC_SET_LINE_CODING         0x20U
#define CDC_GET_LINE_CODING         0x21U
#define CDC_SET_CONTROL_LINE_STATE  0x22U
#define CDC_SEND_BREAK              0x23U


/* ═══════════════════════════════════════════════════════════════════════════
 *  SECTION 2: USB Descriptors
 *
 *  These byte arrays describe the device to the host (PC).
 *  Format defined by USB 2.0 specification, chapter 9.
 * ═══════════════════════════════════════════════════════════════════════════ */

 /* Setup packet (8 bytes received at the start of each control transfer) */
typedef struct __attribute__((packed))
{
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} SetupPacket_t;


/* Device Descriptor – "who am I" */
static const uint8_t desc_device[] =
{
    18,              /* bLength */
    DESC_DEVICE,     /* bDescriptorType */
    0x00, 0x02,      /* bcdUSB = USB 2.0 */
    0x02,            /* bDeviceClass = CDC */
    0x00,            /* bDeviceSubClass (defined at interface level) */
    0x00,            /* bDeviceProtocol */
    EP0_MPS,         /* bMaxPacketSize0 */
    0x83, 0x04,      /* idVendor  = 0x0483 (STMicroelectronics) */
    0x40, 0x57,      /* idProduct = 0x5740 (Virtual COM Port) */
    0x00, 0x02,      /* bcdDevice = 2.00 */
    1,               /* iManufacturer (string index 1) */
    2,               /* iProduct      (string index 2) */
    3,               /* iSerialNumber (string index 3) */
    1,               /* bNumConfigurations */
};

/*
 *  Configuration Descriptor – the full interface/endpoint tree.
 *
 *  CDC ACM layout:
 *    Interface 0: Communication (1 interrupt IN endpoint for notifications)
 *    Interface 1: Data          (2 endpoints: bulk IN + bulk OUT for data)
 */
#define CONFIG_TOTAL_SIZE  67U

static const uint8_t desc_config[] =
{
    /* Configuration */
    9, DESC_CONFIGURATION, CONFIG_TOTAL_SIZE, 0, 2, 1, 0, 0x80, 250,

    /* Interface 0: CDC Communication */
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,

    /* CDC Header Functional Descriptor */
    5, 0x24, 0x00, 0x10, 0x01,

    /* CDC Call Management */
    5, 0x24, 0x01, 0x00, 1,

    /* CDC Abstract Control Management */
    4, 0x24, 0x02, 0x02,

    /* CDC Union: master=0, slave=1 */
    5, 0x24, 0x06, 0, 1,

    /* EP2 IN – Interrupt (notification) */
    7, 0x05, 0x82, 0x03, CDC_NOTIFY_MPS, 0, 16,

    /* Interface 1: CDC Data */
    9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,

    /* EP1 OUT – Bulk (host -> device) */
    7, 0x05, 0x01, 0x02, CDC_MPS, 0, 0,

    /* EP1 IN – Bulk (device -> host) */
    7, 0x05, 0x81, 0x02, CDC_MPS, 0, 0,
};

/* Device Qualifier (some hosts request it even for FS-only devices) */
static const uint8_t desc_qualifier[] =
{
    10, DESC_DEVICE_QUALIFIER, 0x00, 0x02, 0x02, 0x02, 0x00, EP0_MPS, 1, 0,
};

/* String 0: Language ID */
static const uint8_t str_langid[] = { 4, DESC_STRING, 0x09, 0x04 };

/* String 1: Manufacturer */
static const uint8_t str_mfr[] = {
    22, DESC_STRING,
    'S',0, 'T',0, 'M',0, '3',0, '2',0, ' ',0, 'B',0, 'a',0, 'r',0, 'e',0,
};

/* String 2: Product */
static const uint8_t str_prod[] = {
    36, DESC_STRING,
    'V',0, 'i',0, 'r',0, 't',0, 'u',0, 'a',0, 'l',0, ' ',0,
    'C',0, 'O',0, 'M',0, ' ',0, 'P',0, 'o',0, 'r',0, 't',0, ' ',0,
};

/* String 3: Serial Number */
static const uint8_t str_serial[] = {
    26, DESC_STRING,
    '0',0, '0',0, '0',0, '0',0, '0',0, '0',0,
    '0',0, '0',0, '0',0, '0',0, '0',0, '1',0,
};


/* ═══════════════════════════════════════════════════════════════════════════
 *  Internal driver state
 * ═══════════════════════════════════════════════════════════════════════════ */

/* CDC Line Coding (7 bytes, USB CDC PSTN subclass format) */
typedef struct __attribute__((packed))
{
    uint32_t dwDTERate;     /* baud rate */
    uint8_t  bCharFormat;   /* 0=1stop, 1=1.5stop, 2=2stop */
    uint8_t  bParityType;   /* 0=none, 1=odd, 2=even */
    uint8_t  bDataBits;     /* 5, 6, 7, 8 */
} LineCoding_t;

/* Receive ring buffer */
#define RX_BUF_SIZE  256U

/* Everything the driver needs to know */
static struct
{
    /* EP0 control transfer */
    SetupPacket_t   setup;
    const uint8_t  *tx_ptr;         /* data being sent via EP0 IN */
    uint16_t        tx_remaining;
    uint8_t         pending_addr;   /* address to apply after STATUS IN */
    uint8_t         configured;     /* SET_CONFIGURATION received */

    /* CDC */
    LineCoding_t    line_coding;
    uint8_t         control_lines;  /* DTR (bit0) and RTS (bit1) */
    volatile uint8_t tx_busy;       /* EP1 IN transfer in progress */

    /* RX ring buffer */
    uint8_t         rx_buf[RX_BUF_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;

} usb;


/* ═══════════════════════════════════════════════════════════════════════════
 *  Low-level helpers (FIFO, flush, delay)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void delay(volatile uint32_t n) { while (n--) { __asm("NOP"); } }

static void flush_tx(uint8_t fifo)
{
    USB_OTG_GRSTCTL = ((uint32_t)fifo << GRSTCTL_TXFNUM_POS) | GRSTCTL_TXFFLSH;
    while (USB_OTG_GRSTCTL & GRSTCTL_TXFFLSH) {}
}

static void flush_rx(void)
{
    USB_OTG_GRSTCTL = GRSTCTL_RXFFLSH;
    while (USB_OTG_GRSTCTL & GRSTCTL_RXFFLSH) {}
}

/*
 *  Read 'len' bytes from the receive FIFO into 'dest'.
 *  The FIFO is accessed in 32-bit words.
 */
static void fifo_read(uint8_t *dest, uint16_t len)
{
    uint32_t words = (len + 3U) / 4U;
    for (uint32_t i = 0; i < words; i++)
    {
        uint32_t w = USB_OTG_FIFO(0);  /* reading any EP FIFO pops from RX */
        uint16_t left = len - (uint16_t)(i * 4U);
        uint16_t n = (left < 4U) ? left : 4U;
        for (uint16_t b = 0; b < n; b++)
        {
            *dest++ = (uint8_t)(w & 0xFFU);
            w >>= 8;
        }
    }
}

/*
 *  Write 'len' bytes into the TX FIFO of endpoint 'ep'.
 */
static void fifo_write(uint8_t ep, const uint8_t *src, uint16_t len)
{
    uint32_t words = (len + 3U) / 4U;
    for (uint32_t i = 0; i < words; i++)
    {
        uint32_t w = 0;
        uint16_t left = len - (uint16_t)(i * 4U);
        uint16_t n = (left < 4U) ? left : 4U;
        for (uint16_t b = 0; b < n; b++)
        {
            w |= ((uint32_t)(*src++) << (8U * b));
        }
        USB_OTG_FIFO(ep) = w;
    }
}

/* Ring buffer push */
static void rb_push(uint8_t byte)
{
    uint16_t next = (usb.rx_head + 1U) % RX_BUF_SIZE;
    if (next != usb.rx_tail)
    {
        usb.rx_buf[usb.rx_head] = byte;
        usb.rx_head = next;
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  EP0 – Control Transfers (enumeration)
 *
 *  Typical flow:
 *    Host sends SETUP (8 bytes) -> we process it ->
 *      DATA IN (we send data) or DATA OUT (we receive) ->
 *      STATUS (final handshake)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Prepare EP0 OUT to receive the next SETUP packet */
static void ep0_prepare_setup(void)
{
    USB_OTG_DOEPTSIZ(0) = (3U << DEPTSIZ_STUPCNT_POS)   /* accept 3 back-to-back */
                         | (1U << DEPTSIZ_PKTCNT_POS)
                         | EP0_MPS;
    USB_OTG_DOEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
}

/* Send data via EP0 IN (to the host) */
static void ep0_tx(const uint8_t *data, uint16_t len)
{
    usb.tx_ptr       = data;
    usb.tx_remaining = len;

    uint16_t chunk = (len > EP0_MPS) ? EP0_MPS : len;

    USB_OTG_DIEPTSIZ(0) = (1U << DEPTSIZ_PKTCNT_POS) | chunk;
    USB_OTG_DIEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
    fifo_write(0, data, chunk);

    usb.tx_ptr       += chunk;
    usb.tx_remaining -= chunk;
}

/* Send zero-length packet (STATUS IN) */
static void ep0_send_zlp(void)
{
    USB_OTG_DIEPTSIZ(0) = (1U << DEPTSIZ_PKTCNT_POS);
    USB_OTG_DIEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
}

/* Stall EP0 (unsupported request) */
static void ep0_stall(void)
{
    USB_OTG_DIEPCTL(0) |= DEPCTL_STALL;
    USB_OTG_DOEPCTL(0) |= DEPCTL_STALL;
    ep0_prepare_setup();
}

/* Prepare EP0 OUT to receive data (DATA OUT stage) */
static void ep0_prepare_rx(uint16_t len)
{
    uint16_t chunk = (len > EP0_MPS) ? EP0_MPS : len;
    USB_OTG_DOEPTSIZ(0) = (1U << DEPTSIZ_PKTCNT_POS) | chunk;
    USB_OTG_DOEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
}


/*
 *  Look up the descriptor requested by the host.
 *  Returns pointer and length, or NULL if not found.
 */
static const uint8_t *find_descriptor(uint8_t type, uint8_t index, uint16_t *len)
{
    switch (type)
    {
    case DESC_DEVICE:
        *len = sizeof(desc_device);
        return desc_device;

    case DESC_CONFIGURATION:
        *len = sizeof(desc_config);
        return desc_config;

    case DESC_DEVICE_QUALIFIER:
        *len = sizeof(desc_qualifier);
        return desc_qualifier;

    case DESC_STRING:
        switch (index)
        {
        case 0: *len = sizeof(str_langid); return str_langid;
        case 1: *len = sizeof(str_mfr);    return str_mfr;
        case 2: *len = sizeof(str_prod);   return str_prod;
        case 3: *len = sizeof(str_serial); return str_serial;
        }
        /* fall through */
    default:
        *len = 0;
        return NULL;
    }
}


/*
 *  Process the received SETUP packet.
 *  All enumeration and class request logic happens here.
 */
static void process_setup(void)
{
    SetupPacket_t *s = &usb.setup;
    uint8_t type   = s->bmRequestType & 0x60U;  /* Standard / Class / Vendor */
    uint8_t dir    = s->bmRequestType & 0x80U;  /* IN = device->host */

    /* ── CDC class requests ── */
    if (type == 0x20U)  /* CLASS */
    {
        switch (s->bRequest)
        {
        case CDC_SET_LINE_CODING:
            /* Host will send 7 bytes with baud/parity/stop/databits */
            ep0_prepare_rx(7);
            return;

        case CDC_GET_LINE_CODING:
            ep0_tx((const uint8_t *)&usb.line_coding, 7);
            return;

        case CDC_SET_CONTROL_LINE_STATE:
            usb.control_lines = (uint8_t)(s->wValue & 0x03U);
            ep0_send_zlp();
            return;

        case CDC_SEND_BREAK:
            ep0_send_zlp();
            return;

        default:
            ep0_stall();
            return;
        }
    }

    /* ── Standard requests ── */
    switch (s->bRequest)
    {
    case REQ_GET_DESCRIPTOR:
    {
        uint8_t  desc_type  = (uint8_t)(s->wValue >> 8);
        uint8_t  desc_index = (uint8_t)(s->wValue & 0xFF);
        uint16_t len = 0;
        const uint8_t *d = find_descriptor(desc_type, desc_index, &len);
        if (d)
        {
            if (len > s->wLength) len = s->wLength;
            ep0_tx(d, len);
        }
        else
        {
            ep0_stall();
        }
        break;
    }

    case REQ_SET_ADDRESS:
        /* Address can only be applied AFTER the STATUS IN completes */
        usb.pending_addr = (uint8_t)(s->wValue & 0x7FU);
        ep0_send_zlp();
        break;

    case REQ_SET_CONFIGURATION:
    {
        uint8_t cfg = (uint8_t)(s->wValue & 0xFFU);
        usb.configured = cfg;

        if (cfg)
        {
            /* Open the CDC endpoints */

            /* EP1 IN – Bulk (device -> host, data) */
            USB_OTG_DIEPCTL(1) = DEPCTL_USBAEP
                               | (0x02U << DEPCTL_EPTYP_POS)  /* Bulk */
                               | (1U << DEPCTL_TXFNUM_POS)    /* TX FIFO 1 */
                               | DEPCTL_SD0PID
                               | CDC_MPS;

            /* EP1 OUT – Bulk (host -> device, data) */
            USB_OTG_DOEPCTL(1) = DEPCTL_USBAEP
                               | (0x02U << DEPCTL_EPTYP_POS)  /* Bulk */
                               | DEPCTL_SD0PID
                               | CDC_MPS;

            /* EP2 IN – Interrupt (notifications, rarely used) */
            USB_OTG_DIEPCTL(2) = DEPCTL_USBAEP
                               | (0x03U << DEPCTL_EPTYP_POS)  /* Interrupt */
                               | (2U << DEPCTL_TXFNUM_POS)    /* TX FIFO 2 */
                               | DEPCTL_SD0PID
                               | CDC_NOTIFY_MPS;

            /* Enable interrupts for these endpoints */
            USB_OTG_DAINTMSK |= DAINT_IN(1) | DAINT_OUT(1) | DAINT_IN(2);

            /* Arm EP1 OUT to start receiving data from the host */
            USB_OTG_DOEPTSIZ(1) = (1U << DEPTSIZ_PKTCNT_POS) | CDC_MPS;
            USB_OTG_DOEPCTL(1) |= DEPCTL_EPENA | DEPCTL_CNAK;
        }

        ep0_send_zlp();
        break;
    }

    case REQ_GET_CONFIGURATION:
    {
        static uint8_t cfg_val;
        cfg_val = usb.configured;
        ep0_tx(&cfg_val, 1);
        break;
    }

    case REQ_GET_STATUS:
    {
        static uint16_t status_val = 0;
        ep0_tx((const uint8_t *)&status_val, 2);
        break;
    }

    case REQ_CLEAR_FEATURE:
    case REQ_SET_FEATURE:
        ep0_send_zlp();
        break;

    default:
        ep0_stall();
        break;
    }

    (void)dir;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Interrupt Service Routine
 *
 *  USB is entirely interrupt-driven:
 *    USBRST   – host reset the bus
 *    ENUMDNE  – speed enumeration completed
 *    RXFLVL   – data arrived in the RX FIFO
 *    IEPINT   – an IN endpoint completed a transfer
 *    OEPINT   – an OUT endpoint completed a transfer
 * ═══════════════════════════════════════════════════════════════════════════ */

void OTG_FS_IRQHandler(void)
{
    uint32_t gint = USB_OTG_GINTSTS & USB_OTG_GINTMSK;

    /* ── USB Reset ── */
    if (gint & GINT_USBRST)
    {
        USB_OTG_GINTSTS = GINT_USBRST;

        /* Clear address and state */
        USB_OTG_DCFG &= ~DCFG_DAD_MSK;
        usb.configured   = 0;
        usb.pending_addr  = 0;
        usb.tx_busy       = 0;

        /* Reset endpoint masks */
        USB_OTG_DAINTMSK = DAINT_IN(0) | DAINT_OUT(0);
        USB_OTG_DIEPMSK  = DEPINT_XFRC;
        USB_OTG_DOEPMSK  = DEPINT_XFRC | DEPINT_STUP;

        /* Flush FIFOs */
        flush_tx(0x10);  /* 0x10 = all TX FIFOs */
        flush_rx();

        /* Clear all EP interrupt flags */
        for (uint8_t i = 0; i < 4; i++)
        {
            USB_OTG_DIEPINT(i) = 0xFFU;
            USB_OTG_DOEPINT(i) = 0xFFU;
        }

        ep0_prepare_setup();
    }

    /* ── Enumeration done ── */
    if (gint & GINT_ENUMDNE)
    {
        USB_OTG_GINTSTS = GINT_ENUMDNE;

        /* Set USB turnaround time for Full Speed @ 96 MHz AHB */
        USB_OTG_GUSBCFG &= ~(0xFU << 10);
        USB_OTG_GUSBCFG |= (6U << 10);

        /* Ensure EP0 MPSIZ = 0 (meaning 64 bytes) */
        USB_OTG_DIEPCTL(0) &= ~DEPCTL_MPS_MSK;
    }

    /* ── RX FIFO non-empty (data arrived) ── */
    if (gint & GINT_RXFLVL)
    {
        /* Temporarily disable to prevent re-entry */
        USB_OTG_GINTMSK &= ~GINT_RXFLVL;

        uint32_t sts    = USB_OTG_GRXSTSP;  /* pop the status register */
        uint8_t  ep     = sts & RXSTS_EPNUM_MSK;
        uint16_t bcnt   = (uint16_t)((sts & RXSTS_BCNT_MSK) >> RXSTS_BCNT_POS);
        uint8_t  pktsts = (uint8_t)((sts & RXSTS_PKTSTS_MSK) >> RXSTS_PKTSTS_POS);

        if (pktsts == PKTSTS_SETUP_DATA)
        {
            /* 8-byte SETUP packet -> save into struct */
            fifo_read((uint8_t *)&usb.setup, 8);
        }
        else if (pktsts == PKTSTS_OUT_DATA && bcnt > 0)
        {
            if (ep == 0)
            {
                /* EP0 OUT data stage (e.g. SET_LINE_CODING, 7 bytes) */
                fifo_read((uint8_t *)&usb.line_coding, bcnt);
            }
            else if (ep == CDC_OUT_EP)
            {
                /* Serial data from PC terminal -> ring buffer */
                uint8_t tmp[CDC_MPS];
                fifo_read(tmp, bcnt);
                for (uint16_t i = 0; i < bcnt; i++)
                {
                    rb_push(tmp[i]);
                }
            }
        }

        USB_OTG_GINTMSK |= GINT_RXFLVL;
    }

    /* ── IN endpoint events ── */
    if (gint & GINT_IEPINT)
    {
        /* EP0 IN */
        if (USB_OTG_DAINT & DAINT_IN(0))
        {
            uint32_t epint = USB_OTG_DIEPINT(0);
            if (epint & DEPINT_XFRC)
            {
                USB_OTG_DIEPINT(0) = DEPINT_XFRC;

                if (usb.tx_remaining > 0)
                {
                    /* Continue sending the next chunk */
                    uint16_t chunk = (usb.tx_remaining > EP0_MPS)
                                   ? EP0_MPS : usb.tx_remaining;

                    USB_OTG_DIEPTSIZ(0) = (1U << DEPTSIZ_PKTCNT_POS) | chunk;
                    USB_OTG_DIEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
                    fifo_write(0, usb.tx_ptr, chunk);

                    usb.tx_ptr       += chunk;
                    usb.tx_remaining -= chunk;
                }
                else if (usb.pending_addr)
                {
                    /* STATUS IN completed -> now we can apply the address */
                    USB_OTG_DCFG &= ~DCFG_DAD_MSK;
                    USB_OTG_DCFG |= ((uint32_t)usb.pending_addr << DCFG_DAD_POS);
                    usb.pending_addr = 0;
                    ep0_prepare_setup();
                }
                else
                {
                    /* DATA IN finished -> prepare for STATUS OUT from host */
                    USB_OTG_DOEPTSIZ(0) = (1U << DEPTSIZ_PKTCNT_POS) | EP0_MPS;
                    USB_OTG_DOEPCTL(0) |= DEPCTL_EPENA | DEPCTL_CNAK;
                }
            }
        }

        /* EP1 IN (CDC bulk TX complete) */
        if (USB_OTG_DAINT & DAINT_IN(1))
        {
            uint32_t epint = USB_OTG_DIEPINT(1);
            if (epint & DEPINT_XFRC)
            {
                USB_OTG_DIEPINT(1) = DEPINT_XFRC;
                usb.tx_busy = 0;
            }
        }

        /* EP2 IN (CDC notification – just clear the flag) */
        if (USB_OTG_DAINT & DAINT_IN(2))
        {
            USB_OTG_DIEPINT(2) = USB_OTG_DIEPINT(2);
        }
    }

    /* ── OUT endpoint events ── */
    if (gint & GINT_OEPINT)
    {
        /* EP0 OUT */
        if (USB_OTG_DAINT & DAINT_OUT(0))
        {
            uint32_t epint = USB_OTG_DOEPINT(0);

            if (epint & DEPINT_STUP)
            {
                /* SETUP phase done -> process the request */
                USB_OTG_DOEPINT(0) = DEPINT_STUP;
                process_setup();
            }

            if (epint & DEPINT_XFRC)
            {
                /* EP0 OUT data complete (e.g. SET_LINE_CODING received) */
                USB_OTG_DOEPINT(0) = DEPINT_XFRC;
                ep0_send_zlp();   /* send STATUS IN to confirm */
            }
        }

        /* EP1 OUT (CDC bulk data received) */
        if (USB_OTG_DAINT & DAINT_OUT(1))
        {
            uint32_t epint = USB_OTG_DOEPINT(1);
            if (epint & DEPINT_XFRC)
            {
                USB_OTG_DOEPINT(1) = DEPINT_XFRC;
                /* Re-arm to receive the next packet */
                USB_OTG_DOEPTSIZ(1) = (1U << DEPTSIZ_PKTCNT_POS) | CDC_MPS;
                USB_OTG_DOEPCTL(1) |= DEPCTL_EPENA | DEPCTL_CNAK;
            }
        }
    }

    /* Suspend / Wakeup – clear and ignore */
    if (gint & GINT_USBSUSP) USB_OTG_GINTSTS = GINT_USBSUSP;
    if (gint & GINT_WKUPINT) USB_OTG_GINTSTS = GINT_WKUPINT;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Initialisation (GPIO + Core + Device mode)
 * ═══════════════════════════════════════════════════════════════════════════ */

void USB_CDC_Init(void)
{
    /* Zero out state */
    for (uint32_t i = 0; i < sizeof(usb); i++) ((uint8_t *)&usb)[i] = 0;

    /* Default line coding: 115200 8N1 */
    usb.line_coding.dwDTERate = 115200;
    usb.line_coding.bDataBits = 8;

    /* ── GPIO: PA11 (DM) and PA12 (DP) on AF10 ── */
    static const GPIO_PinConfig_t usb_pins[] =
    {
        { GPIOA, GPIO_PIN_NO_11, GPIO_MODE_ALTFN, GPIO_SPEED_HIGH,
          GPIO_OP_TYPE_PP, GPIO_NO_PUPD, 10 },
        { GPIOA, GPIO_PIN_NO_12, GPIO_MODE_ALTFN, GPIO_SPEED_HIGH,
          GPIO_OP_TYPE_PP, GPIO_NO_PUPD, 10 },
    };
    GPIO_Init_table(usb_pins, 2);

    /* ── Enable USB peripheral clock ── */
    USB_OTG_FS_PCLK_EN();
    delay(10000);

    /* ── Select internal PHY (Full Speed) – must be done BEFORE core reset ── */
    USB_OTG_GUSBCFG |= GUSBCFG_PHYSEL;
    while (!(USB_OTG_GRSTCTL & GRSTCTL_AHBIDL)) {}

    /* ── Core reset ── */
    USB_OTG_GRSTCTL |= GRSTCTL_CSRST;
    while (USB_OTG_GRSTCTL & GRSTCTL_CSRST) {}
    delay(100000);

    /* ── Power up transceiver ── */
    USB_OTG_GCCFG |= GCCFG_PWRDWN;

    /* ── Force device mode (HAL clears both FHMOD+FDMOD, then sets FDMOD) ── */
    USB_OTG_GUSBCFG &= ~((1U << 29) | (1U << 30));
    USB_OTG_GUSBCFG |= GUSBCFG_FDMOD;
    delay(700000);  /* HAL uses 50 ms */

    /* ── Disable VBUS sensing, force B-session valid (Black Pill has no VBUS sense) ── */
    USB_OTG_GCCFG &= ~((1U << 18) | (1U << 19));  /* clear VBUSBSEN, VBUSASEN */
    USB_OTG_GCCFG |= GCCFG_NOVBUSSENS;             /* VBUS always valid */
    USB_OTG_GOTGCTL |= (1U << 7) | (1U << 6);      /* BVALOEN | BVALOVAL */

    /* ── Restart PHY clock ── */
    USB_OTG_PCGCCTL = 0;

    /* ── Speed = Full Speed ── */
    USB_OTG_DCFG |= DCFG_DSPD_FS;

    /* ── Configure FIFO sizes ── */
    USB_OTG_GRXFSIZ   = RX_FIFO_SZ;
    USB_OTG_DIEPTXF0  = ((uint32_t)TX0_FIFO_SZ << 16) | RX_FIFO_SZ;
    USB_OTG_DIEPTXF(1) = ((uint32_t)TX1_FIFO_SZ << 16) | (RX_FIFO_SZ + TX0_FIFO_SZ);
    USB_OTG_DIEPTXF(2) = ((uint32_t)TX2_FIFO_SZ << 16) | (RX_FIFO_SZ + TX0_FIFO_SZ + TX1_FIFO_SZ);

    /* ── Flush and clear everything ── */
    flush_tx(0x10);
    flush_rx();

    USB_OTG_DIEPMSK  = 0;
    USB_OTG_DOEPMSK  = 0;
    USB_OTG_DAINTMSK = 0;

    for (uint8_t i = 0; i < 4; i++)
    {
        USB_OTG_DIEPINT(i) = 0xFFU;
        USB_OTG_DOEPINT(i) = 0xFFU;
    }

    /* ── Enable interrupts ── */
    USB_OTG_GINTMSK = GINT_USBRST | GINT_ENUMDNE
                     | GINT_IEPINT | GINT_OEPINT
                     | GINT_RXFLVL
                     | GINT_USBSUSP | GINT_WKUPINT;

    USB_OTG_DIEPMSK  = DEPINT_XFRC;
    USB_OTG_DOEPMSK  = DEPINT_XFRC | DEPINT_STUP;
    USB_OTG_DAINTMSK = DAINT_IN(0) | DAINT_OUT(0);

    /* ── Connect (remove soft-disconnect) BEFORE enabling global interrupt ── */
    USB_OTG_DCTL &= ~DCTL_SDIS;
    delay(50000);  /* HAL waits 3 ms after connect */

    /* ── Enable global interrupt (after connect, per HAL sequence) ── */
    USB_OTG_GAHBCFG |= GAHBCFG_GINTMSK;

    /* ── NVIC ── */
    interrupt_Config(IRQ_NO_OTG_FS, ENABLE);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Public API 
 * ═══════════════════════════════════════════════════════════════════════════ */

void USB_CDC_Transmit(const uint8_t *data, uint16_t len)
{
    if (!usb.configured || len == 0) return;

    while (len > 0)
    {
        /* Wait for previous TX to finish */
        while (usb.tx_busy) { __asm("NOP"); }

        uint16_t chunk = (len > CDC_MPS) ? CDC_MPS : len;
        usb.tx_busy = 1;

        USB_OTG_DIEPTSIZ(CDC_IN_EP) = (1U << DEPTSIZ_PKTCNT_POS) | chunk;
        USB_OTG_DIEPCTL(CDC_IN_EP) |= DEPCTL_EPENA | DEPCTL_CNAK;
        fifo_write(CDC_IN_EP, data, chunk);

        data += chunk;
        len  -= chunk;
    }
}


uint16_t USB_CDC_Available(void)
{
    int32_t diff = (int32_t)usb.rx_head - (int32_t)usb.rx_tail;
    if (diff < 0) diff += RX_BUF_SIZE;
    return (uint16_t)diff;
}


uint16_t USB_CDC_Read(uint8_t *buf, uint16_t len)
{
    uint16_t count = 0;
    while (count < len && usb.rx_head != usb.rx_tail)
    {
        buf[count++] = usb.rx_buf[usb.rx_tail];
        usb.rx_tail = (usb.rx_tail + 1U) % RX_BUF_SIZE;
    }
    return count;
}


bool USB_CDC_IsConnected(void)
{
    return (usb.configured && (usb.control_lines & 0x01U));
}