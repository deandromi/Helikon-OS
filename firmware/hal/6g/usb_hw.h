/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S5L8702 USB device controller: addresses, register offsets and the seam the
 * driver talks to the core through.
 *
 * Every value here is a donor fact, not a Helikon measurement: the bases and
 * clock gates come from the pinned Rockbox `export/s5l87xx.h`, the PHY sequence
 * from `target/arm/s5l8702/usb-s5l8702.c`, and the FIFO budget and core
 * sequence from `drivers/usb-designware.c`. `docs/usb-device-stack.md` §2
 * records each one against its source. Nothing here has been executed on a 6G.
 *
 * The seam exists so the register sequence can be exercised on the host
 * (`tests/usb/test_usb_hal.c` drives a mock register file) without compiling
 * `s5l8702_regs.h` or touching a device. `usb.c` is the only file that binds the
 * seam to real memory.
 */
#ifndef HELIKON_HAL_6G_USB_HW_H
#define HELIKON_HAL_6G_USB_HW_H

#include <stdint.h>

/* --- S5L8702 topology (donor s5l87xx.h) ---------------------------------- */

#define HELIKON_USB_OTG_BASE 0x38400000u /* S5L8701 uses 0x38800000; not the 6G */
#define HELIKON_USB_PHY_BASE 0x3c400000u
#define HELIKON_USB_IRQ 19u /* IRQ_USB_FUNC, VIC bank 0 */

#define HELIKON_USB_CLOCKGATE_OTG 2u   /* PWRCON(0) bit 2 */
#define HELIKON_USB_CLOCKGATE_PHY 35u  /* PWRCON(1) bit 3 */

/* PHY registers, addressed from HELIKON_USB_PHY_BASE. */
#define HELIKON_USB_PHY_OPHYPWR 0x00u
#define HELIKON_USB_PHY_OPHYCLK 0x04u
#define HELIKON_USB_PHY_ORSTCON 0x08u
#define HELIKON_USB_PHY_OPHYUNK3 0x18u
#define HELIKON_USB_PHY_OPHYUNK1 0x1cu
#define HELIKON_USB_PHY_OPHYUNK2 0x44u

/* Control endpoint packet size; the device descriptor states the same value. */
#define HELIKON_USB_EP0_MAX_PACKET 64u

#define HELIKON_USB_PHY_UNK1_VALUE 0x1u
#define HELIKON_USB_PHY_UNK2_VALUE 0xe3fu
#define HELIKON_USB_PHY_UNK3_VALUE 0x600u
#define HELIKON_USB_PHY_CLOCK_VALUE 0u /* USB_DW_CLOCK 0 (ipod6g.h) */

/* PHY and core bring-up delays, in microseconds (donor sequence). */
#define HELIKON_USB_PHY_RESET_US 10u
#define HELIKON_USB_PHY_CLOCK_US 400u
#define HELIKON_USB_SOFT_CONNECT_US 3000u

/* --- DesignWare device core registers ------------------------------------ */

#define HELIKON_USB_REG_GOTGCTL 0x000u
#define HELIKON_USB_REG_GAHBCFG 0x008u
#define HELIKON_USB_REG_GUSBCFG 0x00cu
#define HELIKON_USB_REG_GRSTCTL 0x010u
#define HELIKON_USB_REG_GINTSTS 0x014u
#define HELIKON_USB_REG_GINTMSK 0x018u
#define HELIKON_USB_REG_GRXSTSP 0x020u
#define HELIKON_USB_REG_GRXFSIZ 0x024u
#define HELIKON_USB_REG_GNPTXFSIZ 0x028u
#define HELIKON_USB_REG_GNPTXSTS 0x02cu
#define HELIKON_USB_REG_GHWCFG2 0x048u
#define HELIKON_USB_REG_GHWCFG3 0x04cu
#define HELIKON_USB_REG_GHWCFG4 0x050u
#define HELIKON_USB_REG_HPTXFSIZ 0x100u
#define HELIKON_USB_REG_DIEPTXF(n) (0x104u + 4u * (n))
#define HELIKON_USB_REG_DCFG 0x800u
#define HELIKON_USB_REG_DCTL 0x804u
#define HELIKON_USB_REG_DSTS 0x808u
#define HELIKON_USB_REG_DIEPMSK 0x810u
#define HELIKON_USB_REG_DOEPMSK 0x814u
#define HELIKON_USB_REG_DAINT 0x818u
#define HELIKON_USB_REG_DAINTMSK 0x81cu
#define HELIKON_USB_REG_DVBUSDIS 0x828u
#define HELIKON_USB_REG_DVBUSPULSE 0x82cu
#define HELIKON_USB_REG_DIEPCTL(n) (0x900u + 0x20u * (n))
#define HELIKON_USB_REG_DIEPINT(n) (0x908u + 0x20u * (n))
#define HELIKON_USB_REG_DIEPTSIZ(n) (0x910u + 0x20u * (n))
#define HELIKON_USB_REG_DIEPDMA(n) (0x914u + 0x20u * (n))
#define HELIKON_USB_REG_DTXFSTS(n) (0x918u + 0x20u * (n))
#define HELIKON_USB_REG_DOEPCTL(n) (0xb00u + 0x20u * (n))
#define HELIKON_USB_REG_DOEPINT(n) (0xb08u + 0x20u * (n))
#define HELIKON_USB_REG_DOEPTSIZ(n) (0xb10u + 0x20u * (n))
#define HELIKON_USB_REG_DOEPDMA(n) (0xb14u + 0x20u * (n))
#define HELIKON_USB_REG_PCGCCTL 0xe00u
#define HELIKON_USB_REG_FIFO(n) (0x1000u * (1u + (n)))

/* GRSTCTL */
#define HELIKON_USB_GRSTCTL_CSRST (1u << 0)
#define HELIKON_USB_GRSTCTL_RXFFLSH (1u << 4)
#define HELIKON_USB_GRSTCTL_TXFFLSH (1u << 5)
#define HELIKON_USB_GRSTCTL_TXFNUM(x) ((uint32_t)(x) << 6)
#define HELIKON_USB_GRSTCTL_AHBIDLE (1u << 31)
#define HELIKON_USB_TXFNUM_ALL 0x10u

/* GAHBCFG */
#define HELIKON_USB_GAHBCFG_GINT (1u << 0)
#define HELIKON_USB_GAHBCFG_HBSTLEN_INCR8 (3u << 1)

/* GUSBCFG */
#define HELIKON_USB_GUSBCFG_PHYIF16 (1u << 3)
#define HELIKON_USB_GUSBCFG_TRDT(x) ((uint32_t)(x) << 10)
#define HELIKON_USB_GUSBCFG_FDMOD (1u << 30)

/* GINTSTS / GINTMSK */
#define HELIKON_USB_GINT_RXFLVL (1u << 4)
#define HELIKON_USB_GINT_USBRST (1u << 12)
#define HELIKON_USB_GINT_ENUMDNE (1u << 13)
#define HELIKON_USB_GINT_IEPINT (1u << 18)
#define HELIKON_USB_GINT_OEPINT (1u << 19)
#define HELIKON_USB_GINT_DISCINT (1u << 29)

/* GRXSTSP packet status, bits 17..20. */
#define HELIKON_USB_RXSTS_PKTSTS(x) (((uint32_t)(x) >> 17) & 0x0fu)
#define HELIKON_USB_RXSTS_EPNUM(x) ((uint32_t)(x) & 0x0fu)
#define HELIKON_USB_RXSTS_BCNT(x) (((uint32_t)(x) >> 4) & 0x07ffu)
#define HELIKON_USB_PKTSTS_OUT_DATA 0x1u
#define HELIKON_USB_PKTSTS_OUT_STATUS 0x2u
#define HELIKON_USB_PKTSTS_SETUP 0x3u
#define HELIKON_USB_PKTSTS_SETUP_DATA 0x4u

/* DCFG */
#define HELIKON_USB_DCFG_DAD(x) ((uint32_t)(x) << 4)
#define HELIKON_USB_DCFG_NZLSOHSK (1u << 2)
#define HELIKON_USB_DCFG_DSPD_HIGH (0u)

/* DCTL */
#define HELIKON_USB_DCTL_RWUSIG (1u << 0)
#define HELIKON_USB_DCTL_SDIS (1u << 1)
#define HELIKON_USB_DCTL_TCTL(x) ((uint32_t)(x) << 4)

/* DSTS */
#define HELIKON_USB_DSTS_ENUMSPD(x) (((uint32_t)(x) >> 1) & 0x3u)
#define HELIKON_USB_ENUMSPD_HIGH 0u
#define HELIKON_USB_ENUMSPD_FULL 1u

/* Endpoint control (both directions). */
#define HELIKON_USB_EPCTL_MPS(x) ((uint32_t)(x) & 0x7ffu)
#define HELIKON_USB_EPCTL_EPENA (1u << 31)
#define HELIKON_USB_EPCTL_EPDIS (1u << 30)
#define HELIKON_USB_EPCTL_SETD0PID (1u << 28)
#define HELIKON_USB_EPCTL_CNAK (1u << 26)
#define HELIKON_USB_EPCTL_SNAK (1u << 27)
#define HELIKON_USB_EPCTL_TXFNUM(x) ((uint32_t)(x) << 22)
#define HELIKON_USB_EPCTL_STALL (1u << 21)
#define HELIKON_USB_EPCTL_EPTYP_BULK (2u << 18)
#define HELIKON_USB_EPCTL_EPTYP_CONTROL (0u << 18)
#define HELIKON_USB_EPCTL_USBACTEP (1u << 15)
#define HELIKON_USB_EPCTL_NAKSTS (1u << 17)

/* Transfer size: xfersize[18:0], pktcnt[28:19], mc/stupcnt[30:29]. */
#define HELIKON_USB_TSIZ_XFRSIZ(x) ((uint32_t)(x) & 0x7ffffu)
#define HELIKON_USB_TSIZ_PKTCNT(x) (((uint32_t)(x) & 0x3ffu) << 19)
#define HELIKON_USB_TSIZ_MC(x) (((uint32_t)(x) & 0x3u) << 29)

/* Endpoint interrupts. */
#define HELIKON_USB_EPINT_XFRC (1u << 0)
#define HELIKON_USB_EPINT_STUP (1u << 3)
#define HELIKON_USB_EPINT_TOC (1u << 3)
#define HELIKON_USB_EPINT_TXFE (1u << 7)

/* DAINT: IN endpoints in bits 0..15, OUT endpoints in bits 16..31. */
#define HELIKON_USB_DAINT_IN(ep) (1u << (ep))
#define HELIKON_USB_DAINT_OUT(ep) (1u << (16u + (ep)))

/* --- FIFO budget (donor, words; one word is four bytes) ------------------ */

#define HELIKON_USB_RX_FIFO_WORDS 0x360u
#define HELIKON_USB_EP0_TX_FIFO_WORDS 0x40u
#define HELIKON_USB_EP1_TX_FIFO_WORDS 0x180u
#define HELIKON_USB_TX0_START HELIKON_USB_RX_FIFO_WORDS
#define HELIKON_USB_EP1_TX_START \
    (HELIKON_USB_RX_FIFO_WORDS + HELIKON_USB_EP0_TX_FIFO_WORDS)
#define HELIKON_USB_EP2_TX_START \
    (HELIKON_USB_EP1_TX_START + HELIKON_USB_EP1_TX_FIFO_WORDS)

/* --- The seam ------------------------------------------------------------ */

struct helikon_usb_hw {
    void *context;
    /* Register access at a full address (base already folded in). */
    uint32_t (*read)(void *context, uint32_t address);
    void (*write)(void *context, uint32_t address, uint32_t value);
    /* FIFO access: 32-bit words on the target, bytes on the host mock. */
    void (*fifo_read)(void *context, uint32_t address, void *buffer,
                      uint32_t length);
    void (*fifo_write)(void *context, uint32_t address, const void *buffer,
                       uint32_t length);
    /* Busy wait; the target uses the timer-E clock, the mock just counts. */
    void (*delay_us)(void *context, uint32_t microseconds);
    /* Enable one PWRCON clock gate. */
    void (*clock_enable)(void *context, unsigned int gate);
    /* Enable/disable the USB line in the VIC. */
    void (*irq_enable)(void *context, int enable);
};

/*
 * Target binding: the seam over real memory, timer E and VIC IRQ 19. Declared
 * here so a mode module does not include `clock.h`/`s5l8702_regs.h` itself.
 * Absent on the host, where the test supplies its own mock seam.
 */
#ifndef HELIKON_HOST_TEST
struct helikon_clock;
struct helikon_usb_hw helikon_usb_hw_s5l8702(struct helikon_clock *clock);
#endif

#endif
