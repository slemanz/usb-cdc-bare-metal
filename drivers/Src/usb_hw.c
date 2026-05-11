#include "stm32f411xx.h"
#include "usb_hw.h"

/* GCCFG bits (offset 0x038) */
#define GCCFG_PWRDWN        (1U << 16)  /* power up the FS transceiver */
#define GCCFG_NOVBUSSENS    (1U << 21)  /* disable VBUS sensing (no VBUS pin on Blackpill) */

/* GRSTCTL bits */
#define GRSTCTL_CSRST       (1U << 0)   /* core soft reset */
#define GRSTCTL_AHBIDL      (1U << 31)  /* AHB master idle — set when reset is done */

/* GUSBCFG bits */
#define GUSBCFG_PHYSEL      (1U << 6)   /* select internal FS PHY (always 1 on OTG-FS) */
#define GUSBCFG_FDMOD       (1U << 30)  /* force device mode */

/* DCFG bits */
#define DCFG_DSPD_FS        (3U << 0)   /* full-speed using 48 MHz internal PHY */

/* GINTMSK bits */
#define GINTMSK_RXFLVLM     (1U << 4)   /* RxFIFO non-empty */
#define GINTMSK_USBRST      (1U << 12)  /* USB reset */
#define GINTMSK_ENUMDNEM    (1U << 13)  /* enumeration done */
#define GINTMSK_IEPINT      (1U << 18)  /* IN endpoint interrupt */
#define GINTMSK_OEPINT      (1U << 19)  /* OUT endpoint interrupt */

/* GAHBCFG bits */
#define GAHBCFG_GINT        (1U << 0)   /* global interrupt enable */

/* DCTL bits */
#define DCTL_SDIS           (1U << 1)   /* soft disconnect — clear to connect D+ */

void usb_hw_init(void)
{
    /* ── 1. GPIO ──────────────────────────────────────────────────────────── */
    RCC->AHB1ENR |= (1U << 0);          /* GPIOA clock enable */

    /* PA11 (DM) and PA12 (DP): alternate function mode (MODER = 10) */
    GPIOA->MODER &= ~((3U << 22) | (3U << 24));
    GPIOA->MODER |=  ((2U << 22) | (2U << 24));

    GPIOA->OTYPER  &= ~((1U << 11) | (1U << 12)); /* push-pull */
    GPIOA->OSPEEDR |=  ((3U << 22) | (3U << 24)); /* very high speed */
    GPIOA->PUPDR   &= ~((3U << 22) | (3U << 24)); /* no pull */

    /* AF10 = OTG_FS; PA11 → AFRH bits [15:12], PA12 → AFRH bits [19:16] */
    GPIOA->AFR[1] &= ~((0xFU << 12) | (0xFU << 16));
    GPIOA->AFR[1] |=  ((10U  << 12) | (10U  << 16));

    /* ── 2. OTG-FS clock ─────────────────────────────────────────────────── */
    RCC->AHB2ENR |= (1U << 7);          /* OTGFSEN */

    /* ── 3. Power up transceiver ─────────────────────────────────────────── */
    USB_OTG_GCCFG = GCCFG_PWRDWN | GCCFG_NOVBUSSENS;

    /* ── 4. Core soft reset ───────────────────────────────────────────────── */
    USB_OTG_GRSTCTL |= GRSTCTL_CSRST;
    while (USB_OTG_GRSTCTL & GRSTCTL_CSRST)  {}   /* wait for reset to complete */
    while (!(USB_OTG_GRSTCTL & GRSTCTL_AHBIDL)) {} /* wait for AHB master idle */

    /* ── 5. Force device mode + internal PHY ─────────────────────────────── */
    USB_OTG_GUSBCFG |= GUSBCFG_PHYSEL | GUSBCFG_FDMOD;

    /* RM0383 §22.17.1: wait ≥ 25 ms after FDMOD before accessing device regs */
    for (volatile uint32_t i = 0; i < 2400000U; i++) {}

    /* ── 6. Device config: full-speed ────────────────────────────────────── */
    USB_OTG_DCFG = DCFG_DSPD_FS;       /* bits[1:0] = 11: FS internal PHY */

    /* ── 7. FIFO layout (all sizes in 32-bit words):
     *      [0 .. 127]   RxFIFO (shared receive buffer, 128 words)
     *      [128 .. 191] EP0 TxFIFO (64 words)                      */
    USB_OTG_GRXFSIZ  = 128U;
    USB_OTG_DIEPTXF0 = (64U << 16) | 128U; /* depth=64, start=128 */

    /* ── 8. Clear any stale interrupt flags ──────────────────────────────── */
    USB_OTG_GINTSTS = 0xFFFFFFFFU;

    /* ── 9. Unmask hardware-level interrupts ────────────────────────────────── */
    /* RXFLVLM, IEPINT, OEPINT are added by usb_core_init() after the protocol
     * layer is ready to drain the FIFO safely. */
    USB_OTG_GINTMSK = GINTMSK_USBRST
                    | GINTMSK_ENUMDNEM;

    /* ── 10. Enable global interrupt in AHB config ───────────────────────── */
    USB_OTG_GAHBCFG |= GAHBCFG_GINT;

    /* ── 11. Enable OTG_FS IRQ in NVIC (IRQ 67 → ISER2 bit 3) ──────────── */
    *NVIC_ISER2 |= (1U << (IRQ_NO_OTG_FS - 64U));

    /* ── 12. Soft-connect: clear SDIS to assert D+ pull-up ──────────────── */
    USB_OTG_DCTL &= ~DCTL_SDIS;
}

/* ISR is now implemented in usb_core.c */
