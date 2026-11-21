/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S5L8702 USB device controller driver (DesignWare OTG core, PIO).
 *
 * The driver owns the PHY, the clocks, the core registers and EP0/EP1/EP2. It
 * moves bytes and reports events; it does not know what a descriptor, a CBW or
 * a SCSI command is. `firmware/usb/usb_control.c` and `usb_msc.c` sit above it.
 *
 * Ground rules, from `docs/usb-device-stack.md` §3:
 *  - PIO first. The internal DMA path needs a recorded cache/MMU state, so it is
 *    not used and no cache maintenance is implied here.
 *  - Static buffers only. No allocation, no threading, no nested IRQs.
 *  - The IRQ is short: it drains the core's status registers, copies at most the
 *    bytes the core already holds, and pushes an event. The main loop does the
 *    descriptor and SCSI work in `helikon_usb_pump()`.
 *  - Every wait is bounded. A stuck core returns HELIKON_ERR_IO instead of
 *    hanging the firmware.
 *
 * NOTHING HERE HAS RUN ON A 6G. The register sequence follows the pinned donor
 * driver and the PHY sequence in `usb-s5l8702.c`; the host suite drives a mock
 * register file to prove the sequence, not the silicon.
 */
#include "usb.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "usb_hw.h"

#define HELIKON_USB_EP0 0u
#define HELIKON_USB_EP1 1u
#define HELIKON_USB_EP2 2u
#define HELIKON_USB_EP_COUNT 3u

#define HELIKON_USB_EP0_MPS HELIKON_USB_EP0_MAX_PACKET
#define HELIKON_USB_EP1_MPS_HIGH 512u
#define HELIKON_USB_EP_BULK_MPS_FULL 64u

#define HELIKON_USB_EVENT_CAPACITY 16u
#define HELIKON_USB_SETUP_BYTES HELIKON_USB_SETUP_LENGTH
/* Bound on one status-register drain and on the packets moved per call. */
#define HELIKON_USB_RX_DRAIN_LIMIT 32u
#define HELIKON_USB_TX_PACKETS_PER_CALL 4u
/* Register polling: generous for a 300 MHz core, finite for a stuck bus. */
#define HELIKON_USB_RESET_SPINS 1000000u

struct helikon_usb_state {
    const struct helikon_usb_hw *hw;
    int initialized;
    int configured;
    int high_speed;
    uint8_t address;

    struct {
        uint8_t type;
        uint8_t setup[HELIKON_USB_SETUP_BYTES];
    } events[HELIKON_USB_EVENT_CAPACITY];
    unsigned int event_head;
    unsigned int event_tail;

    /* IN pumps, indexed by endpoint number. */
    const uint8_t *in_data[HELIKON_USB_EP_COUNT];
    uint32_t in_length[HELIKON_USB_EP_COUNT];
    uint32_t in_done[HELIKON_USB_EP_COUNT];
    int in_active[HELIKON_USB_EP_COUNT];

    /* OUT buffers, indexed by endpoint number. */
    uint8_t *out_data[HELIKON_USB_EP_COUNT];
    uint32_t out_length[HELIKON_USB_EP_COUNT];
    uint32_t out_done[HELIKON_USB_EP_COUNT];
    int out_active[HELIKON_USB_EP_COUNT];

    uint8_t setup[HELIKON_USB_SETUP_BYTES];

    uint32_t irq_count;
    uint32_t spurious_count;
    uint32_t error_count;
};

static struct helikon_usb_state usb;

/* --- register access ----------------------------------------------------- */

static uint32_t usb_read(uint32_t offset)
{
    return usb.hw->read(usb.hw->context, HELIKON_USB_OTG_BASE + offset);
}

static void usb_write(uint32_t offset, uint32_t value)
{
    usb.hw->write(usb.hw->context, HELIKON_USB_OTG_BASE + offset, value);
}

static void usb_set(uint32_t offset, uint32_t bits)
{
    usb_write(offset, usb_read(offset) | bits);
}

static void usb_clear(uint32_t offset, uint32_t bits)
{
    usb_write(offset, usb_read(offset) & ~bits);
}

static void phy_write(uint32_t offset, uint32_t value)
{
    usb.hw->write(usb.hw->context, HELIKON_USB_PHY_BASE + offset, value);
}

static void delay_us(uint32_t microseconds)
{
    if (usb.hw->delay_us != NULL)
        usb.hw->delay_us(usb.hw->context, microseconds);
}

/* Waits for `mask` to set in `offset`; returns HELIKON_ERR_IO on timeout. */
static int wait_set(uint32_t offset, uint32_t mask)
{
    for (uint32_t spin = 0u; spin < HELIKON_USB_RESET_SPINS; ++spin) {
        if ((usb_read(offset) & mask) == mask)
            return HELIKON_OK;
        delay_us(1u);
    }
    usb.error_count += 1u;
    return HELIKON_ERR_IO;
}

/* Waits for `mask` to clear in `offset`; returns HELIKON_ERR_IO on timeout. */
static int wait_clear(uint32_t offset, uint32_t mask)
{
    for (uint32_t spin = 0u; spin < HELIKON_USB_RESET_SPINS; ++spin) {
        if ((usb_read(offset) & mask) == 0u)
            return HELIKON_OK;
        delay_us(1u);
    }
    usb.error_count += 1u;
    return HELIKON_ERR_IO;
}

/* --- events -------------------------------------------------------------- */

static void push_event(uint8_t type)
{
    unsigned int next = (usb.event_head + 1u) % HELIKON_USB_EVENT_CAPACITY;
    if (next == usb.event_tail) {
        /* The main loop fell behind. Drop the newest and count it: the error
         * counter is the only honest thing to report, and the core keeps its
         * own pending state, so a dropped notification is not a lost packet. */
        usb.error_count += 1u;
        return;
    }
    usb.events[usb.event_head].type = type;
    for (unsigned int index = 0u; index < HELIKON_USB_SETUP_BYTES; ++index)
        usb.events[usb.event_head].setup[index] =
            type == HELIKON_USB_EVENT_SETUP ? usb.setup[index] : 0u;
    usb.event_head = next;
}

/* --- PHY and core bring-up ----------------------------------------------- */

static void phy_init(void)
{
    if (usb.hw->clock_enable != NULL) {
        usb.hw->clock_enable(usb.hw->context, HELIKON_USB_CLOCKGATE_OTG);
        usb.hw->clock_enable(usb.hw->context, HELIKON_USB_CLOCKGATE_PHY);
    }

    phy_write(HELIKON_USB_PHY_OPHYPWR, 0u);
    delay_us(HELIKON_USB_PHY_RESET_US);
    phy_write(HELIKON_USB_PHY_OPHYUNK1, HELIKON_USB_PHY_UNK1_VALUE);
    phy_write(HELIKON_USB_PHY_OPHYUNK2, HELIKON_USB_PHY_UNK2_VALUE);
    phy_write(HELIKON_USB_PHY_ORSTCON, 1u);
    delay_us(HELIKON_USB_PHY_RESET_US);
    phy_write(HELIKON_USB_PHY_ORSTCON, 0u);
    delay_us(HELIKON_USB_PHY_RESET_US);
    phy_write(HELIKON_USB_PHY_OPHYUNK3, HELIKON_USB_PHY_UNK3_VALUE);
    phy_write(HELIKON_USB_PHY_OPHYCLK, HELIKON_USB_PHY_CLOCK_VALUE);
    delay_us(HELIKON_USB_PHY_CLOCK_US);
}

static void fifo_sizes(void)
{
    /* RX: 0x360 words. Non-periodic TX (EP0): 0x40 words at 0x360.
     * EP1 IN: one dedicated 0x180-word FIFO at 0x3a0. The donor keeps IN3/IN5
     * for the same budget; Helikon uses one and leaves two free. */
    usb_write(HELIKON_USB_REG_GRXFSIZ, HELIKON_USB_RX_FIFO_WORDS);
    usb_write(HELIKON_USB_REG_GNPTXFSIZ,
              (HELIKON_USB_EP0_TX_FIFO_WORDS << 16) | HELIKON_USB_TX0_START);
    usb_write(HELIKON_USB_REG_DIEPTXF(0u),
              (HELIKON_USB_EP1_TX_FIFO_WORDS << 16) |
                  HELIKON_USB_EP1_TX_START);
}

static void flush_fifos(void)
{
    /* TX FIFO number 0x10 flushes all of them. */
    usb_set(HELIKON_USB_REG_GRSTCTL,
            HELIKON_USB_GRSTCTL_TXFFLSH |
                HELIKON_USB_GRSTCTL_TXFNUM(HELIKON_USB_TXFNUM_ALL));
    (void)wait_clear(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_TXFFLSH);
    usb_set(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_RXFFLSH);
    (void)wait_clear(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_RXFFLSH);
}

static int core_reset(void)
{
    /* AHBIDLE is set when the AHB bus is idle; CSRST is cleared by the core
     * when the reset finishes. Both waits are bounded. */
    int rc = wait_set(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_AHBIDLE);
    if (rc != HELIKON_OK)
        return rc;
    usb_set(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_CSRST);
    rc = wait_clear(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_CSRST);
    if (rc != HELIKON_OK)
        return rc;
    /* The databook asks for a short settle after the core reset. */
    delay_us(HELIKON_USB_PHY_RESET_US);
    return wait_set(HELIKON_USB_REG_GRSTCTL, HELIKON_USB_GRSTCTL_AHBIDLE);
}

static uint32_t endpoint_mps(uint32_t ep)
{
    if (ep == HELIKON_USB_EP0)
        return HELIKON_USB_EP0_MPS;
    return usb.high_speed ? HELIKON_USB_EP1_MPS_HIGH
                          : HELIKON_USB_EP_BULK_MPS_FULL;
}

/* Arms EP0 OUT for the next SETUP. STUPCNT is the MC field for an OUT EP0. */
static void ep0_arm_setup(void)
{
    usb_write(HELIKON_USB_REG_DOEPTSIZ(HELIKON_USB_EP0),
              HELIKON_USB_TSIZ_XFRSIZ(HELIKON_USB_EP0_MPS) |
                  HELIKON_USB_TSIZ_PKTCNT(1u) | HELIKON_USB_TSIZ_MC(1u));
    usb_write(HELIKON_USB_REG_DOEPCTL(HELIKON_USB_EP0),
              HELIKON_USB_EPCTL_MPS(HELIKON_USB_EP0_MPS) |
                  HELIKON_USB_EPCTL_USBACTEP | HELIKON_USB_EPCTL_EPENA);
}

static void endpoint_disable(uint32_t ep, uint32_t mps, uint32_t type)
{
    uint32_t ctl = usb_read(HELIKON_USB_REG_DIEPCTL(ep));
    if ((ctl & HELIKON_USB_EPCTL_EPENA) != 0u)
        usb_write(HELIKON_USB_REG_DIEPCTL(ep),
                  HELIKON_USB_EPCTL_EPDIS | HELIKON_USB_EPCTL_SNAK);
    usb_write(HELIKON_USB_REG_DOEPCTL(ep),
              HELIKON_USB_EPCTL_MPS(mps) | type | HELIKON_USB_EPCTL_SNAK);
}

static void endpoint_configure(uint32_t ep, int in, uint32_t mps, uint32_t type)
{
    endpoint_disable(ep, mps, type);
    if (in) {
        uint32_t ctl = HELIKON_USB_EPCTL_MPS(mps) |
                       HELIKON_USB_EPCTL_USBACTEP | type |
                       HELIKON_USB_EPCTL_SETD0PID | HELIKON_USB_EPCTL_SNAK;
        if (ep == HELIKON_USB_EP1)
            ctl |= HELIKON_USB_EPCTL_TXFNUM(0u);
        usb_write(HELIKON_USB_REG_DIEPCTL(ep), ctl);
        usb_write(HELIKON_USB_REG_DIEPTSIZ(ep), 0u);
        usb_write(HELIKON_USB_REG_DIEPINT(ep), 0xffffffffu);
    } else {
        usb_write(HELIKON_USB_REG_DOEPTSIZ(ep), 0u);
        usb_write(HELIKON_USB_REG_DOEPCTL(ep),
                  HELIKON_USB_EPCTL_MPS(mps) | HELIKON_USB_EPCTL_USBACTEP |
                      type | HELIKON_USB_EPCTL_SETD0PID |
                      HELIKON_USB_EPCTL_SNAK);
        usb_write(HELIKON_USB_REG_DOEPINT(ep), 0xffffffffu);
    }
}

/* --- the init sequence --------------------------------------------------- */

int helikon_usb_init(const struct helikon_usb_hw *hw)
{
    if (hw == NULL || hw->read == NULL || hw->write == NULL ||
        hw->fifo_read == NULL || hw->fifo_write == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (usb.initialized)
        return HELIKON_ERR_ARGUMENT;

    for (uint32_t index = 0u; index < (uint32_t)sizeof(usb); ++index)
        ((uint8_t *)&usb)[index] = 0u;
    usb.hw = hw;

    if (hw->irq_enable != NULL)
        hw->irq_enable(hw->context, 0);
    phy_init();

    usb_write(HELIKON_USB_REG_PCGCCTL, 0u);
    /* PHYIF16 (UTMI 16-bit) and TRDT 5, per the 6G donor configuration. */
    usb_write(HELIKON_USB_REG_GUSBCFG,
              HELIKON_USB_GUSBCFG_PHYIF16 | HELIKON_USB_GUSBCFG_TRDT(5u) |
                  HELIKON_USB_GUSBCFG_FDMOD);

    int rc = core_reset();
    if (rc != HELIKON_OK)
        return rc;

    fifo_sizes();
    flush_fifos();

    /* PIO: no DMAEN. GINT is off until the handler exists. */
    usb_write(HELIKON_USB_REG_GAHBCFG,
              HELIKON_USB_GAHBCFG_HBSTLEN_INCR8);
    usb_write(HELIKON_USB_REG_DCFG,
              HELIKON_USB_DCFG_NZLSOHSK | HELIKON_USB_DCFG_DSPD_HIGH);
    usb_write(HELIKON_USB_REG_DIEPMSK,
              HELIKON_USB_EPINT_XFRC | HELIKON_USB_EPINT_TOC);
    usb_write(HELIKON_USB_REG_DOEPMSK,
              HELIKON_USB_EPINT_XFRC | HELIKON_USB_EPINT_STUP);
    usb_write(HELIKON_USB_REG_DAINTMSK,
              HELIKON_USB_DAINT_IN(HELIKON_USB_EP0) |
                  HELIKON_USB_DAINT_OUT(HELIKON_USB_EP0) |
                  HELIKON_USB_DAINT_IN(HELIKON_USB_EP1) |
                  HELIKON_USB_DAINT_OUT(HELIKON_USB_EP2));
    /* Cable removal matters for Disk Mode: a host that vanishes must be seen. */
    usb_write(HELIKON_USB_REG_GINTMSK,
              HELIKON_USB_GINT_USBRST | HELIKON_USB_GINT_ENUMDNE |
                  HELIKON_USB_GINT_IEPINT | HELIKON_USB_GINT_OEPINT |
                  HELIKON_USB_GINT_RXFLVL | HELIKON_USB_GINT_DISCINT);

    /* Start disconnected, hand the line to the VIC, then connect. The 3 ms is
     * the donor's soft-connect delay. */
    usb_write(HELIKON_USB_REG_DCTL, HELIKON_USB_DCTL_SDIS);
    usb_write(HELIKON_USB_REG_DAINT, 0xffffffffu);
    usb_write(HELIKON_USB_REG_GINTSTS, 0xffffffffu);
    usb_write(HELIKON_USB_REG_GAHBCFG,
              usb_read(HELIKON_USB_REG_GAHBCFG) | HELIKON_USB_GAHBCFG_GINT);
    if (hw->irq_enable != NULL)
        hw->irq_enable(hw->context, 1);
    delay_us(HELIKON_USB_SOFT_CONNECT_US);
    usb_clear(HELIKON_USB_REG_DCTL, HELIKON_USB_DCTL_SDIS);

    ep0_arm_setup();
    usb.high_speed = (HELIKON_USB_DSTS_ENUMSPD(usb_read(
                          HELIKON_USB_REG_DSTS)) == HELIKON_USB_ENUMSPD_HIGH);
    usb.initialized = 1;
    return HELIKON_OK;
}

void helikon_usb_shutdown(void)
{
    if (!usb.initialized)
        return;
    if (usb.hw->irq_enable != NULL)
        usb.hw->irq_enable(usb.hw->context, 0);
    usb_write(HELIKON_USB_REG_DCTL, HELIKON_USB_DCTL_SDIS);
    usb_write(HELIKON_USB_REG_GINTMSK, 0u);
    usb_write(HELIKON_USB_REG_PCGCCTL, 1u);
    usb.initialized = 0;
    usb.configured = 0;
}

/* --- endpoints ----------------------------------------------------------- */

int helikon_usb_set_address(uint8_t address)
{
    if (address > 127u)
        return HELIKON_ERR_ARGUMENT;
    if (!usb.initialized)
        return HELIKON_ERR_ARGUMENT;
    /* The donor writes DCFG before acknowledging SET_ADDRESS, because the core
     * needs the new address for the status stage. Keep that order until a
     * device proves otherwise. */
    usb.address = address;
    usb_write(HELIKON_USB_REG_DCFG,
              HELIKON_USB_DCFG_NZLSOHSK | HELIKON_USB_DCFG_DSPD_HIGH |
                  HELIKON_USB_DCFG_DAD(address));
    return HELIKON_OK;
}

int helikon_usb_configure(int configured)
{
    if (!usb.initialized)
        return HELIKON_ERR_ARGUMENT;
    uint32_t mps = endpoint_mps(HELIKON_USB_EP1);
    if (configured) {
        endpoint_configure(HELIKON_USB_EP1, 1, mps, HELIKON_USB_EPCTL_EPTYP_BULK);
        endpoint_configure(HELIKON_USB_EP2, 0, mps, HELIKON_USB_EPCTL_EPTYP_BULK);
        usb.configured = 1;
    } else {
        endpoint_disable(HELIKON_USB_EP1, mps, HELIKON_USB_EPCTL_EPTYP_BULK);
        endpoint_disable(HELIKON_USB_EP2, mps, HELIKON_USB_EPCTL_EPTYP_BULK);
        usb.configured = 0;
    }
    return HELIKON_OK;
}

uint8_t helikon_usb_address(void)
{
    return usb.address;
}

int helikon_usb_high_speed(void)
{
    return usb.high_speed;
}

int helikon_usb_is_configured(void)
{
    return usb.configured;
}

static uint32_t fifo_space_words(uint32_t ep)
{
    if (ep == HELIKON_USB_EP0)
        return usb_read(HELIKON_USB_REG_GNPTXSTS) & 0xffffu;
    return usb_read(HELIKON_USB_REG_DTXFSTS(ep)) & 0xffffu;
}

/* Writes up to HELIKON_USB_TX_PACKETS_PER_CALL whole packets of an IN
 * transfer. `mps` is the endpoint's max packet, so the host sees whole packets
 * and the last short packet terminates the transfer. */
static void pump_in(uint32_t ep)
{
    if (!usb.in_active[ep])
        return;
    uint32_t mps = endpoint_mps(ep);
    for (uint32_t packet = 0u; packet < HELIKON_USB_TX_PACKETS_PER_CALL;
         ++packet) {
        if (usb.in_done[ep] >= usb.in_length[ep])
            return;
        uint32_t remaining = usb.in_length[ep] - usb.in_done[ep];
        uint32_t take = remaining > mps ? mps : remaining;
        uint32_t words = (take + 3u) / 4u;
        if (fifo_space_words(ep) < words)
            return;
        uint32_t address = HELIKON_USB_OTG_BASE + HELIKON_USB_REG_FIFO(ep);
        usb.hw->fifo_write(usb.hw->context, address,
                           usb.in_data[ep] + usb.in_done[ep], take);
        usb.in_done[ep] += take;
    }
}

int helikon_usb_ep0_send(const void *data, uint32_t length)
{
    if (!usb.initialized || (data == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (length > 0x7ffffu)
        return HELIKON_ERR_ARGUMENT;

    usb.in_data[HELIKON_USB_EP0] = data;
    usb.in_length[HELIKON_USB_EP0] = length;
    usb.in_done[HELIKON_USB_EP0] = 0u;
    usb.in_active[HELIKON_USB_EP0] = 1;
    uint32_t packets = (length + HELIKON_USB_EP0_MPS - 1u) / HELIKON_USB_EP0_MPS;
    if (packets == 0u)
        packets = 1u; /* zero-length status stage */
    usb_write(HELIKON_USB_REG_DIEPTSIZ(HELIKON_USB_EP0),
              HELIKON_USB_TSIZ_XFRSIZ(length) |
                  HELIKON_USB_TSIZ_PKTCNT(packets) | HELIKON_USB_TSIZ_MC(1u));
    usb_write(HELIKON_USB_REG_DIEPCTL(HELIKON_USB_EP0),
              HELIKON_USB_EPCTL_MPS(HELIKON_USB_EP0_MPS) |
                  HELIKON_USB_EPCTL_USBACTEP | HELIKON_USB_EPCTL_EPENA |
                  HELIKON_USB_EPCTL_CNAK);
    pump_in(HELIKON_USB_EP0);
    if (length == 0u)
        push_event(HELIKON_USB_EVENT_EP0_IN_DONE);
    return HELIKON_OK;
}

int helikon_usb_ep0_receive(void *buffer, uint32_t length)
{
    if (!usb.initialized || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (length > 0x7ffffu)
        return HELIKON_ERR_ARGUMENT;
    usb.out_data[HELIKON_USB_EP0] = buffer;
    usb.out_length[HELIKON_USB_EP0] = length;
    usb.out_done[HELIKON_USB_EP0] = 0u;
    usb.out_active[HELIKON_USB_EP0] = 1;
    uint32_t packets = (length + HELIKON_USB_EP0_MPS - 1u) / HELIKON_USB_EP0_MPS;
    if (packets == 0u)
        packets = 1u;
    usb_write(HELIKON_USB_REG_DOEPTSIZ(HELIKON_USB_EP0),
              HELIKON_USB_TSIZ_XFRSIZ(length) |
                  HELIKON_USB_TSIZ_PKTCNT(packets) | HELIKON_USB_TSIZ_MC(1u));
    usb_write(HELIKON_USB_REG_DOEPCTL(HELIKON_USB_EP0),
              HELIKON_USB_EPCTL_MPS(HELIKON_USB_EP0_MPS) |
                  HELIKON_USB_EPCTL_USBACTEP | HELIKON_USB_EPCTL_EPENA);
    return HELIKON_OK;
}

void helikon_usb_ep_stall(uint32_t ep, int in)
{
    if (!usb.initialized || ep >= HELIKON_USB_EP_COUNT)
        return;
    if (in)
        usb_set(HELIKON_USB_REG_DIEPCTL(ep), HELIKON_USB_EPCTL_STALL);
    else
        usb_set(HELIKON_USB_REG_DOEPCTL(ep), HELIKON_USB_EPCTL_STALL);
}

int helikon_usb_ep0_stall(void)
{
    helikon_usb_ep_stall(HELIKON_USB_EP0, 1);
    helikon_usb_ep_stall(HELIKON_USB_EP0, 0);
    return HELIKON_OK;
}

int helikon_usb_ep1_send(const void *data, uint32_t length)
{
    if (!usb.initialized || !usb.configured ||
        (data == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (length > 0x7ffffu)
        return HELIKON_ERR_ARGUMENT;
    usb.in_data[HELIKON_USB_EP1] = data;
    usb.in_length[HELIKON_USB_EP1] = length;
    usb.in_done[HELIKON_USB_EP1] = 0u;
    usb.in_active[HELIKON_USB_EP1] = 1;
    uint32_t mps = endpoint_mps(HELIKON_USB_EP1);
    uint32_t packets = (length + mps - 1u) / mps;
    if (packets == 0u)
        packets = 1u;
    usb_write(HELIKON_USB_REG_DIEPTSIZ(HELIKON_USB_EP1),
              HELIKON_USB_TSIZ_XFRSIZ(length) |
                  HELIKON_USB_TSIZ_PKTCNT(packets) | HELIKON_USB_TSIZ_MC(1u));
    uint32_t ctl = HELIKON_USB_EPCTL_MPS(mps) | HELIKON_USB_EPCTL_USBACTEP |
                   HELIKON_USB_EPCTL_EPTYP_BULK | HELIKON_USB_EPCTL_EPENA |
                   HELIKON_USB_EPCTL_TXFNUM(0u);
    usb_write(HELIKON_USB_REG_DIEPCTL(HELIKON_USB_EP1), ctl);
    pump_in(HELIKON_USB_EP1);
    return HELIKON_OK;
}

int helikon_usb_ep2_receive(void *buffer, uint32_t length)
{
    if (!usb.initialized || !usb.configured ||
        (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (length > 0x7ffffu)
        return HELIKON_ERR_ARGUMENT;
    usb.out_data[HELIKON_USB_EP2] = buffer;
    usb.out_length[HELIKON_USB_EP2] = length;
    usb.out_done[HELIKON_USB_EP2] = 0u;
    usb.out_active[HELIKON_USB_EP2] = 1;
    uint32_t mps = endpoint_mps(HELIKON_USB_EP2);
    uint32_t packets = (length + mps - 1u) / mps;
    if (packets == 0u)
        packets = 1u;
    usb_write(HELIKON_USB_REG_DOEPTSIZ(HELIKON_USB_EP2),
              HELIKON_USB_TSIZ_XFRSIZ(length) |
                  HELIKON_USB_TSIZ_PKTCNT(packets));
    usb_write(HELIKON_USB_REG_DOEPCTL(HELIKON_USB_EP2),
              HELIKON_USB_EPCTL_MPS(mps) | HELIKON_USB_EPCTL_USBACTEP |
                  HELIKON_USB_EPCTL_EPTYP_BULK | HELIKON_USB_EPCTL_EPENA);
    return HELIKON_OK;
}

/* --- receive path -------------------------------------------------------- */

static void receive_out(uint32_t ep, uint32_t length)
{
    if (ep >= HELIKON_USB_EP_COUNT || !usb.out_active[ep])
        return;
    uint32_t room = usb.out_length[ep] - usb.out_done[ep];
    uint32_t take = length > room ? room : length;
    uint8_t *target = usb.out_data[ep];
    if (target != NULL) {
        usb.hw->fifo_read(usb.hw->context,
                          HELIKON_USB_OTG_BASE + HELIKON_USB_REG_FIFO(ep),
                          target + usb.out_done[ep], take);
    } else {
        /* A NULL buffer of length zero is a status stage; nothing to store. */
        uint8_t scratch[64];
        usb.hw->fifo_read(usb.hw->context,
                          HELIKON_USB_OTG_BASE + HELIKON_USB_REG_FIFO(ep),
                          scratch, length > sizeof(scratch) ? sizeof(scratch)
                                                            : length);
    }
    usb.out_done[ep] += take;
    if (take < length)
        usb.error_count += 1u; /* oversize OUT: the host sent more than we said */
}

static void rx_drain(void)
{
    for (uint32_t index = 0u; index < HELIKON_USB_RX_DRAIN_LIMIT; ++index) {
        uint32_t status = usb_read(HELIKON_USB_REG_GRXSTSP);
        uint32_t packet = HELIKON_USB_RXSTS_PKTSTS(status);
        uint32_t ep = HELIKON_USB_RXSTS_EPNUM(status);
        uint32_t bytes = HELIKON_USB_RXSTS_BCNT(status);
        if (packet == 0u && bytes == 0u)
            return;

        if (packet == HELIKON_USB_PKTSTS_SETUP ||
            packet == HELIKON_USB_PKTSTS_SETUP_DATA) {
            if (bytes >= HELIKON_USB_SETUP_BYTES) {
                usb.hw->fifo_read(usb.hw->context,
                                  HELIKON_USB_OTG_BASE +
                                      HELIKON_USB_REG_FIFO(HELIKON_USB_EP0),
                                  usb.setup, HELIKON_USB_SETUP_BYTES);
                push_event(HELIKON_USB_EVENT_SETUP);
            }
            continue;
        }
        if (packet == HELIKON_USB_PKTSTS_OUT_DATA && ep < HELIKON_USB_EP_COUNT) {
            if (ep == HELIKON_USB_EP0 && usb.out_active[HELIKON_USB_EP0]) {
                receive_out(HELIKON_USB_EP0, bytes);
                if (usb.out_done[HELIKON_USB_EP0] >=
                    usb.out_length[HELIKON_USB_EP0] && bytes < endpoint_mps(0)) {
                    usb.out_active[HELIKON_USB_EP0] = 0;
                    push_event(HELIKON_USB_EVENT_EP0_OUT_DONE);
                }
            } else if (ep == HELIKON_USB_EP2) {
                receive_out(HELIKON_USB_EP2, bytes);
            }
            continue;
        }
        if (packet == HELIKON_USB_PKTSTS_OUT_STATUS) {
            /* Status stage of a control IN transfer. */
            if (ep == HELIKON_USB_EP0)
                ep0_arm_setup();
            continue;
        }
    }
    usb.error_count += 1u; /* the FIFO never went empty within the bound */
}

static void endpoint_interrupts(void)
{
    uint32_t pending = usb_read(HELIKON_USB_REG_DAINT);
    if ((pending & HELIKON_USB_DAINT_IN(HELIKON_USB_EP0)) != 0u) {
        uint32_t flags = usb_read(HELIKON_USB_REG_DIEPINT(HELIKON_USB_EP0));
        usb_write(HELIKON_USB_REG_DIEPINT(HELIKON_USB_EP0), flags);
        if ((flags & HELIKON_USB_EPINT_XFRC) != 0u) {
            usb.in_active[HELIKON_USB_EP0] = 0;
            ep0_arm_setup();
            push_event(HELIKON_USB_EVENT_EP0_IN_DONE);
        }
    }
    if ((pending & HELIKON_USB_DAINT_IN(HELIKON_USB_EP1)) != 0u) {
        uint32_t flags = usb_read(HELIKON_USB_REG_DIEPINT(HELIKON_USB_EP1));
        usb_write(HELIKON_USB_REG_DIEPINT(HELIKON_USB_EP1), flags);
        if ((flags & HELIKON_USB_EPINT_XFRC) != 0u) {
            usb.in_active[HELIKON_USB_EP1] = 0;
            push_event(HELIKON_USB_EVENT_EP1_IN_DONE);
        } else {
            pump_in(HELIKON_USB_EP1);
        }
    }
    if ((pending & HELIKON_USB_DAINT_OUT(HELIKON_USB_EP0)) != 0u) {
        uint32_t flags = usb_read(HELIKON_USB_REG_DOEPINT(HELIKON_USB_EP0));
        usb_write(HELIKON_USB_REG_DOEPINT(HELIKON_USB_EP0), flags);
        if ((flags & HELIKON_USB_EPINT_STUP) != 0u) {
            /* The setup packet itself is reported by RXFLVL; the STUP flag only
             * says the endpoint saw it. Nothing to copy here. */
        }
        if ((flags & HELIKON_USB_EPINT_XFRC) != 0u) {
            usb.out_active[HELIKON_USB_EP0] = 0;
            push_event(HELIKON_USB_EVENT_EP0_OUT_DONE);
        }
    }
    if ((pending & HELIKON_USB_DAINT_OUT(HELIKON_USB_EP2)) != 0u) {
        uint32_t flags = usb_read(HELIKON_USB_REG_DOEPINT(HELIKON_USB_EP2));
        usb_write(HELIKON_USB_REG_DOEPINT(HELIKON_USB_EP2), flags);
        if ((flags & HELIKON_USB_EPINT_XFRC) != 0u) {
            usb.out_active[HELIKON_USB_EP2] = 0;
            push_event(HELIKON_USB_EVENT_EP2_OUT_DONE);
        }
    }
}

int helikon_usb_irq(void)
{
    if (!usb.initialized)
        return 0;
    uint32_t status = usb_read(HELIKON_USB_REG_GINTSTS) &
                      usb_read(HELIKON_USB_REG_GINTMSK);
    if (status == 0u) {
        usb.spurious_count += 1u;
        return 0;
    }
    usb.irq_count += 1u;
    int events = 0;

    if ((status & HELIKON_USB_GINT_USBRST) != 0u) {
        usb_write(HELIKON_USB_REG_GINTSTS, HELIKON_USB_GINT_USBRST);
        /* A bus reset clears the address and every endpoint. */
        usb.address = 0u;
        usb.configured = 0;
        usb.high_speed = 0;
        usb_write(HELIKON_USB_REG_DCFG,
                  HELIKON_USB_DCFG_NZLSOHSK | HELIKON_USB_DCFG_DSPD_HIGH);
        for (uint32_t ep = 0u; ep < HELIKON_USB_EP_COUNT; ++ep) {
            usb.in_active[ep] = 0;
            usb.out_active[ep] = 0;
        }
        flush_fifos();
        ep0_arm_setup();
        push_event(HELIKON_USB_EVENT_BUS_RESET);
        events += 1;
    }
    if ((status & HELIKON_USB_GINT_ENUMDNE) != 0u) {
        usb_write(HELIKON_USB_REG_GINTSTS, HELIKON_USB_GINT_ENUMDNE);
        uint32_t speed = HELIKON_USB_DSTS_ENUMSPD(usb_read(
            HELIKON_USB_REG_DSTS));
        usb.high_speed = speed == HELIKON_USB_ENUMSPD_HIGH;
        events += 1;
    }
    if ((status & HELIKON_USB_GINT_DISCINT) != 0u) {
        usb_write(HELIKON_USB_REG_GINTSTS, HELIKON_USB_GINT_DISCINT);
        usb.configured = 0;
        push_event(HELIKON_USB_EVENT_DISCONNECT);
        events += 1;
    }
    if ((status & HELIKON_USB_GINT_RXFLVL) != 0u) {
        /* Masking RXFLVL while draining is what the databook asks for, so the
         * core does not re-assert inside the loop. */
        usb_clear(HELIKON_USB_REG_GINTMSK, HELIKON_USB_GINT_RXFLVL);
        rx_drain();
        /* The core clears RXFLVL when the FIFO empties; clear it explicitly as
         * well, so a packet the drain refused cannot leave the line asserted. */
        usb_write(HELIKON_USB_REG_GINTSTS, HELIKON_USB_GINT_RXFLVL);
        usb_set(HELIKON_USB_REG_GINTMSK, HELIKON_USB_GINT_RXFLVL);
        events += 1;
    }
    if ((status & (HELIKON_USB_GINT_IEPINT | HELIKON_USB_GINT_OEPINT)) != 0u) {
        endpoint_interrupts();
        events += 1;
    }
    return events;
}

int helikon_usb_pump(void)
{
    if (!usb.initialized)
        return HELIKON_ERR_ARGUMENT;
    pump_in(HELIKON_USB_EP0);
    if (usb.configured)
        pump_in(HELIKON_USB_EP1);
    return HELIKON_OK;
}

int helikon_usb_pop(enum helikon_usb_event *event, uint8_t setup[8])
{
    if (event == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (usb.event_tail == usb.event_head)
        return 0;
    *event = (enum helikon_usb_event)usb.events[usb.event_tail].type;
    if (setup != NULL) {
        for (uint32_t index = 0u; index < HELIKON_USB_SETUP_BYTES; ++index)
            setup[index] = usb.events[usb.event_tail].setup[index];
    }
    usb.event_tail = (usb.event_tail + 1u) % HELIKON_USB_EVENT_CAPACITY;
    return 1;
}

uint32_t helikon_usb_irq_count(void)
{
    return usb.irq_count;
}

uint32_t helikon_usb_spurious_count(void)
{
    return usb.spurious_count;
}

uint32_t helikon_usb_error_count(void)
{
    return usb.error_count;
}

int helikon_usb_out_received(uint32_t ep, uint32_t *length)
{
    if (ep >= HELIKON_USB_EP_COUNT || length == NULL)
        return HELIKON_ERR_ARGUMENT;
    *length = usb.out_done[ep];
    return HELIKON_OK;
}
