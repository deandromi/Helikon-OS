/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host test for the S5L8702 USB controller driver.
 *
 * The driver never touches memory itself: it goes through `struct
 * helikon_usb_hw` (see `firmware/hal/6g/usb_hw.h`). This file supplies a mock
 * register file, mock FIFOs and a mock VIC/clock, so the whole init sequence,
 * EP0 enumeration traffic and the bulk endpoints can be executed on the host.
 *
 * What this proves is the *sequence*: the values written to GUSBCFG, the FIFO
 * budget, the interrupt mask, the endpoint arming and the event flow. It does
 * not prove the silicon. Only a 6G can do that.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "error.h"
#include "usb.h"
#include "usb_hw.h"

static unsigned int failures;

static int expect(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        failures += 1u;
        return 1;
    }
    return 0;
}

/* --- mock register file --------------------------------------------------- */

#define MOCK_REGISTERS 128u
#define MOCK_FIFO_BYTES 8192u

struct mock_register {
    uint32_t address;
    uint32_t value;
    unsigned int writes;
};

struct mock_fifo {
    uint32_t address;
    uint8_t data[MOCK_FIFO_BYTES];
    uint32_t length;
};

struct mock_bus {
    struct mock_register registers[MOCK_REGISTERS];
    unsigned int register_count;
    struct mock_fifo fifos[4];
    unsigned int fifo_count;
    unsigned int delays;
    uint32_t delay_us_total;
    unsigned int clock_gates;
    uint32_t last_gate;
    int irq_enabled;
    unsigned int irq_enables;
    unsigned int irq_disables;
};

static struct mock_bus bus;

static struct mock_register *find_register(uint32_t address, int create)
{
    for (unsigned int index = 0u; index < bus.register_count; ++index) {
        if (bus.registers[index].address == address)
            return &bus.registers[index];
    }
    if (!create || bus.register_count == MOCK_REGISTERS)
        return NULL;
    struct mock_register *entry = &bus.registers[bus.register_count++];
    entry->address = address;
    entry->value = 0u;
    entry->writes = 0u;
    return entry;
}

static struct mock_fifo *find_fifo(uint32_t address, int create)
{
    for (unsigned int index = 0u; index < bus.fifo_count; ++index) {
        if (bus.fifos[index].address == address)
            return &bus.fifos[index];
    }
    if (!create || bus.fifo_count == 4u)
        return NULL;
    struct mock_fifo *fifo = &bus.fifos[bus.fifo_count++];
    fifo->address = address;
    fifo->length = 0u;
    return fifo;
}

static void mock_sync_daint(void);

static uint32_t mock_read(void *context, uint32_t address)
{
    (void)context;
    /* Defaults the driver polls: the core is idle, both TX paths have room. */
    if (address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GRSTCTL) {
        struct mock_register *entry = find_register(address, 0);
        return HELIKON_USB_GRSTCTL_AHBIDLE | (entry == NULL ? 0u : entry->value);
    }
    if (address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GNPTXSTS ||
        address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_DTXFSTS(1u)) {
        struct mock_register *entry = find_register(address, 0);
        if (entry == NULL || entry->value == 0u)
            return 0x8000u; /* 32768 words free */
        return entry->value;
    }
    if (address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GRXSTSP) {
        /* Read-to-pop: the mock hands the queued status out exactly once. */
        struct mock_register *entry = find_register(address, 0);
        if (entry == NULL)
            return 0u;
        uint32_t value = entry->value;
        entry->value = 0u;
        return value;
    }
    struct mock_register *entry = find_register(address, 0);
    return entry == NULL ? 0u : entry->value;
}

/* Raises bits in a register the way the core would (no clear-on-write). */
static void mock_raise(uint32_t address, uint32_t bits)
{
    struct mock_register *entry = find_register(address, 1);
    if (entry != NULL)
        entry->value |= bits;
}

/*
 * DAINT is derived, not stored: a bit appears while that endpoint's interrupt
 * register holds anything, and the global IEPINT/OEPINT follow it. Emulating
 * that keeps the mock honest when the driver clears an endpoint register.
 */
static void mock_sync_daint(void)
{
    uint32_t daint = 0u;
    for (uint32_t ep = 0u; ep < 3u; ++ep) {
        struct mock_register *in = find_register(
            HELIKON_USB_OTG_BASE + HELIKON_USB_REG_DIEPINT(ep), 1);
        struct mock_register *out = find_register(
            HELIKON_USB_OTG_BASE + HELIKON_USB_REG_DOEPINT(ep), 1);
        if (in != NULL && in->value != 0u)
            daint |= HELIKON_USB_DAINT_IN(ep);
        if (out != NULL && out->value != 0u)
            daint |= HELIKON_USB_DAINT_OUT(ep);
    }
    struct mock_register *daint_reg = find_register(
        HELIKON_USB_OTG_BASE + HELIKON_USB_REG_DAINT, 1);
    if (daint_reg != NULL)
        daint_reg->value = daint;
    struct mock_register *gint = find_register(
        HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GINTSTS, 1);
    if (gint == NULL)
        return;
    if ((daint & 0xffffu) != 0u)
        gint->value |= HELIKON_USB_GINT_IEPINT;
    else
        gint->value &= ~HELIKON_USB_GINT_IEPINT;
    if ((daint >> 16) != 0u)
        gint->value |= HELIKON_USB_GINT_OEPINT;
    else
        gint->value &= ~HELIKON_USB_GINT_OEPINT;
}

/* Raises an endpoint interrupt and lets DAINT/GINT follow, as the core does. */
static void mock_raise_endpoint(uint32_t ep, int in, uint32_t flags)
{
    mock_raise(HELIKON_USB_OTG_BASE +
                   (in ? HELIKON_USB_REG_DIEPINT(ep)
                       : HELIKON_USB_REG_DOEPINT(ep)),
               flags);
    mock_sync_daint();
}

/* Raises global status bits the way the core would. */
static void mock_set_gint(uint32_t bits)
{
    struct mock_register *entry = find_register(
        HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GINTSTS, 1);
    if (entry != NULL)
        entry->value |= bits;
}

static void mock_write(void *context, uint32_t address, uint32_t value)
{
    (void)context;
    uint32_t stored = value;
    /* Resets and FIFO flushes clear themselves; the driver waits for that. */
    if (address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GRSTCTL) {
        stored = value & ~(HELIKON_USB_GRSTCTL_CSRST |
                           HELIKON_USB_GRSTCTL_TXFFLSH |
                           HELIKON_USB_GRSTCTL_RXFFLSH);
        if ((value & HELIKON_USB_GRSTCTL_RXFFLSH) != 0u) {
            for (unsigned int index = 0u; index < bus.fifo_count; ++index)
                bus.fifos[index].length = 0u;
        }
    }
    int clear_on_write = address == HELIKON_USB_OTG_BASE +
                                         HELIKON_USB_REG_GINTSTS ||
                         address == HELIKON_USB_OTG_BASE +
                                        HELIKON_USB_REG_DAINT;
    for (uint32_t ep = 0u; ep < 3u && !clear_on_write; ++ep) {
        if (address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_DIEPINT(ep) ||
            address == HELIKON_USB_OTG_BASE + HELIKON_USB_REG_DOEPINT(ep))
            clear_on_write = 1;
    }
    if (clear_on_write) {
        /* Write one to clear, as the core does. */
        struct mock_register *entry = find_register(address, 1);
        if (entry != NULL)
            entry->value &= ~value;
        if (address != HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GINTSTS)
            mock_sync_daint();
        return;
    }
    struct mock_register *entry = find_register(address, 1);
    if (entry != NULL) {
        entry->value = stored;
        entry->writes += 1u;
    }
}

static void mock_fifo_write(void *context, uint32_t address,
                            const void *buffer, uint32_t length)
{
    (void)context;
    struct mock_fifo *fifo = find_fifo(address, 1);
    if (fifo == NULL || fifo->length + length > MOCK_FIFO_BYTES)
        return;
    memcpy(fifo->data + fifo->length, buffer, length);
    fifo->length += length;
}

static void mock_fifo_read(void *context, uint32_t address, void *buffer,
                           uint32_t length)
{
    (void)context;
    struct mock_fifo *fifo = find_fifo(address, 1);
    if (fifo == NULL)
        return;
    if (length > fifo->length)
        length = fifo->length;
    memcpy(buffer, fifo->data, length);
    memmove(fifo->data, fifo->data + length, fifo->length - length);
    fifo->length -= length;
}

static void mock_delay(void *context, uint32_t microseconds)
{
    (void)context;
    bus.delays += 1u;
    bus.delay_us_total += microseconds;
}

static void mock_clock(void *context, unsigned int gate)
{
    (void)context;
    bus.clock_gates += 1u;
    bus.last_gate = gate;
}

static void mock_irq(void *context, int enable)
{
    (void)context;
    bus.irq_enabled = enable;
    if (enable)
        bus.irq_enables += 1u;
    else
        bus.irq_disables += 1u;
}

static struct helikon_usb_hw mock_hw(void)
{
    struct helikon_usb_hw hw;
    hw.context = &bus;
    hw.read = mock_read;
    hw.write = mock_write;
    hw.fifo_read = mock_fifo_read;
    hw.fifo_write = mock_fifo_write;
    hw.delay_us = mock_delay;
    hw.clock_enable = mock_clock;
    hw.irq_enable = mock_irq;
    return hw;
}

/* --- helpers -------------------------------------------------------------- */

static uint32_t otg(uint32_t offset)
{
    return mock_read(&bus, HELIKON_USB_OTG_BASE + offset);
}

static uint32_t phy(uint32_t offset)
{
    return mock_read(&bus, HELIKON_USB_PHY_BASE + offset);
}

/* Queues one packet for the RX path and raises RXFLVL, the way the core does. */
static void inject_rx(uint32_t ep, uint32_t packet_status, const void *data,
                      uint32_t length)
{
    if (data != NULL && length != 0u)
        mock_fifo_write(&bus, HELIKON_USB_OTG_BASE + HELIKON_USB_REG_FIFO(ep),
                        data, length);
    uint32_t status = (ep & 0x0fu) | ((length & 0x7ffu) << 4) |
                      ((packet_status & 0x0fu) << 17);
    struct mock_register *entry = find_register(
        HELIKON_USB_OTG_BASE + HELIKON_USB_REG_GRXSTSP, 1);
    if (entry != NULL)
        entry->value = status;
    mock_set_gint(HELIKON_USB_GINT_RXFLVL);
}

/* --- tests ---------------------------------------------------------------- */

int main(void)
{
    memset(&bus, 0, sizeof(bus));
    struct helikon_usb_hw hw = mock_hw();

    /* --- init ------------------------------------------------------------ */
    if (expect(helikon_usb_init(&hw) == HELIKON_OK, "init returns OK"))
        return 1;

    if (expect(bus.clock_gates == 2u, "two clock gates enabled"))
        return 1;
    if (expect(bus.last_gate == HELIKON_USB_CLOCKGATE_PHY,
               "the last gate is the PHY gate"))
        return 1;
    if (expect(phy(HELIKON_USB_PHY_OPHYPWR) == 0u, "PHY power on"))
        return 1;
    if (expect(phy(HELIKON_USB_PHY_OPHYUNK1) == HELIKON_USB_PHY_UNK1_VALUE,
               "PHY UNK1"))
        return 1;
    if (expect(phy(HELIKON_USB_PHY_OPHYUNK2) == HELIKON_USB_PHY_UNK2_VALUE,
               "PHY UNK2"))
        return 1;
    if (expect(phy(HELIKON_USB_PHY_OPHYUNK3) == HELIKON_USB_PHY_UNK3_VALUE,
               "PHY UNK3"))
        return 1;
    if (expect(phy(HELIKON_USB_PHY_ORSTCON) == 0u, "PHY reset released"))
        return 1;
    if (expect(phy(HELIKON_USB_PHY_OPHYCLK) == HELIKON_USB_PHY_CLOCK_VALUE,
               "PHY clock selected"))
        return 1;
    if (expect(bus.delay_us_total >= 420u, "PHY delays ran"))
        return 1;

    /* Core configuration. */
    uint32_t gusbcfg = otg(HELIKON_USB_REG_GUSBCFG);
    if (expect((gusbcfg & HELIKON_USB_GUSBCFG_PHYIF16) != 0u, "UTMI 16-bit"))
        return 1;
    if (expect(((gusbcfg >> 10) & 0xfu) == 5u, "TRDT 5"))
        return 1;
    if (expect(otg(HELIKON_USB_REG_GRXFSIZ) == HELIKON_USB_RX_FIFO_WORDS,
               "RX FIFO 0x360 words"))
        return 1;
    uint32_t nptx = otg(HELIKON_USB_REG_GNPTXFSIZ);
    if (expect((nptx >> 16) == HELIKON_USB_EP0_TX_FIFO_WORDS,
               "EP0 TX FIFO 0x40 words"))
        return 1;
    if (expect((nptx & 0xffffu) == HELIKON_USB_TX0_START,
               "EP0 TX FIFO starts after RX"))
        return 1;
    uint32_t ep1_fifo_reg = otg(HELIKON_USB_REG_DIEPTXF(0u));
    if (expect((ep1_fifo_reg >> 16) == HELIKON_USB_EP1_TX_FIFO_WORDS,
               "EP1 TX FIFO 0x180 words"))
        return 1;
    if (expect((ep1_fifo_reg & 0xffffu) == HELIKON_USB_EP1_TX_START,
               "EP1 TX FIFO starts after EP0"))
        return 1;
    if (expect((otg(HELIKON_USB_REG_GAHBCFG) & HELIKON_USB_GAHBCFG_GINT) != 0u,
               "global interrupts enabled in GAHBCFG"))
        return 1;
    if (expect(bus.irq_enabled == 1, "VIC line enabled"))
        return 1;
    if (expect(otg(HELIKON_USB_REG_DCFG) ==
                   (HELIKON_USB_DCFG_NZLSOHSK | HELIKON_USB_DCFG_DSPD_HIGH),
               "DCFG: high speed, NZLSOHSK, address 0"))
        return 1;
    if (expect((otg(HELIKON_USB_REG_DCTL) & HELIKON_USB_DCTL_SDIS) == 0u,
               "soft disconnect released"))
        return 1;
    uint32_t mask = otg(HELIKON_USB_REG_GINTMSK);
    if (expect((mask & (HELIKON_USB_GINT_USBRST | HELIKON_USB_GINT_ENUMDNE |
                        HELIKON_USB_GINT_RXFLVL | HELIKON_USB_GINT_DISCINT)) ==
                   (HELIKON_USB_GINT_USBRST | HELIKON_USB_GINT_ENUMDNE |
                    HELIKON_USB_GINT_RXFLVL | HELIKON_USB_GINT_DISCINT),
               "GINTMSK: reset, enum, RX, disconnect"))
        return 1;
    uint32_t doep0 = otg(HELIKON_USB_REG_DOEPCTL(0u));
    if (expect((doep0 & HELIKON_USB_EPCTL_EPENA) != 0u, "EP0 OUT armed"))
        return 1;
    if (expect((doep0 & HELIKON_USB_EPCTL_MPS(0x7ffu)) ==
                   HELIKON_USB_EP0_MAX_PACKET,
               "EP0 max packet 64"))
        return 1;

    /* --- a SETUP packet arrives ------------------------------------------ */
    const uint8_t get_descriptor[8] = { 0x80u, 0x06u, 0x00u, 0x01u,
                                        0x00u, 0x00u, 0x40u, 0x00u };
    inject_rx(0u, HELIKON_USB_PKTSTS_SETUP, get_descriptor, 8u);
    if (expect(helikon_usb_irq() > 0, "SETUP raises an IRQ"))
        return 1;
    enum helikon_usb_event event = HELIKON_USB_EVENT_NONE;
    uint8_t setup[8];
    if (expect(helikon_usb_pop(&event, setup) == 1, "one event queued"))
        return 1;
    if (expect(event == HELIKON_USB_EVENT_SETUP, "the event is SETUP"))
        return 1;
    if (expect(memcmp(setup, get_descriptor, 8u) == 0, "setup bytes copied"))
        return 1;

    /* --- EP0 IN answers it ------------------------------------------------ */
    const uint8_t device_descriptor[18] = {
        0x12u, 0x01u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x40u, 0x09u,
        0x12u, 0x01u, 0x00u, 0x00u, 0x01u, 0x01u, 0x02u, 0x03u, 0x01u
    };
    struct mock_fifo *ep0fifo = find_fifo(
        HELIKON_USB_OTG_BASE + HELIKON_USB_REG_FIFO(0u), 1);
    expect(ep0fifo != NULL, "EP0 FIFO exists");
    if (ep0fifo != NULL)
        ep0fifo->length = 0u;
    if (expect(helikon_usb_ep0_send(device_descriptor, 18u) == HELIKON_OK,
               "ep0 send accepted"))
        return 1;
    uint32_t size = otg(HELIKON_USB_REG_DIEPTSIZ(0u));
    if (expect((size & 0x7ffffu) == 18u, "EP0 transfer size 18"))
        return 1;
    if (expect(((size >> 19) & 0x3ffu) == 1u, "EP0 one packet"))
        return 1;
    if (expect(ep0fifo != NULL && ep0fifo->length == 18u,
               "18 bytes reached the EP0 FIFO"))
        return 1;
    if (expect(ep0fifo != NULL &&
                   memcmp(ep0fifo->data, device_descriptor, 18u) == 0,
               "EP0 FIFO holds the descriptor"))
        return 1;

    /* Completion of the IN stage. */
    mock_raise_endpoint(0u, 1, HELIKON_USB_EPINT_XFRC);
    expect(helikon_usb_irq() > 0, "IEPINT raises an IRQ");
    if (expect(helikon_usb_pop(&event, NULL) == 1, "IN completion event"))
        return 1;
    if (expect(event == HELIKON_USB_EVENT_EP0_IN_DONE,
               "the event is EP0 IN done"))
        return 1;

    /* --- enumeration and configuration --------------------------------- */
    mock_set_gint(HELIKON_USB_GINT_ENUMDNE);
    expect(helikon_usb_irq() > 0, "ENUMDNE raises an IRQ");
    if (expect(helikon_usb_high_speed() == 1, "high speed negotiated"))
        return 1;
    if (expect(helikon_usb_set_address(9u) == HELIKON_OK, "set address 9"))
        return 1;
    if (expect(((otg(HELIKON_USB_REG_DCFG) >> 4) & 0x7fu) == 9u,
               "DCFG carries the address"))
        return 1;
    if (expect(helikon_usb_configure(1) == HELIKON_OK, "configure accepted"))
        return 1;
    if (expect(helikon_usb_is_configured() == 1, "device is configured"))
        return 1;

    uint32_t diep1 = otg(HELIKON_USB_REG_DIEPCTL(1u));
    if (expect((diep1 & HELIKON_USB_EPCTL_USBACTEP) != 0u,
               "EP1 IN active"))
        return 1;
    if (expect((diep1 & HELIKON_USB_EPCTL_EPTYP_BULK) != 0u, "EP1 is bulk"))
        return 1;
    if (expect((diep1 & HELIKON_USB_EPCTL_MPS(0x7ffu)) == 512u,
               "EP1 max packet 512 at high speed"))
        return 1;
    uint32_t doep2 = otg(HELIKON_USB_REG_DOEPCTL(2u));
    if (expect((doep2 & HELIKON_USB_EPCTL_USBACTEP) != 0u, "EP2 OUT active"))
        return 1;
    if (expect((doep2 & HELIKON_USB_EPCTL_MPS(0x7ffu)) == 512u,
               "EP2 max packet 512"))
        return 1;

    /* --- bulk IN -------------------------------------------------------- */
    static uint8_t bulk[1024];
    for (size_t index = 0u; index < sizeof(bulk); ++index)
        bulk[index] = (uint8_t)(index * 3u);
    struct mock_fifo *ep1fifo = find_fifo(
        HELIKON_USB_OTG_BASE + HELIKON_USB_REG_FIFO(1u), 1);
    expect(ep1fifo != NULL, "EP1 FIFO exists");
    if (ep1fifo != NULL)
        ep1fifo->length = 0u;
    if (expect(helikon_usb_ep1_send(bulk, sizeof(bulk)) == HELIKON_OK,
               "bulk send accepted"))
        return 1;
    uint32_t bulk_size = otg(HELIKON_USB_REG_DIEPTSIZ(1u));
    if (expect((bulk_size & 0x7ffffu) == sizeof(bulk), "bulk transfer size"))
        return 1;
    if (expect(((bulk_size >> 19) & 0x3ffu) == 2u, "bulk two packets"))
        return 1;
    if (expect(ep1fifo != NULL && ep1fifo->length == sizeof(bulk),
               "1024 bytes reached the EP1 FIFO"))
        return 1;

    /* --- bulk OUT ------------------------------------------------------- */
    static uint8_t cbw[31];
    for (size_t index = 0u; index < sizeof(cbw); ++index)
        cbw[index] = (uint8_t)(0x40u + index);
    static uint8_t received[64];
    memset(received, 0, sizeof(received));
    if (expect(helikon_usb_ep2_receive(received, sizeof(received)) ==
                   HELIKON_OK,
               "bulk receive armed"))
        return 1;
    inject_rx(2u, HELIKON_USB_PKTSTS_OUT_DATA, cbw, sizeof(cbw));
    expect(helikon_usb_irq() > 0, "OUT data raises an IRQ");
    uint32_t got = 0u;
    if (expect(helikon_usb_out_received(2u, &got) == HELIKON_OK &&
                   got == sizeof(cbw),
               "the OUT buffer holds the CBW"))
        return 1;
    if (expect(memcmp(received, cbw, sizeof(cbw)) == 0,
               "OUT bytes match"))
        return 1;
    mock_raise_endpoint(2u, 0, HELIKON_USB_EPINT_XFRC);
    expect(helikon_usb_irq() > 0, "OEPINT raises an IRQ");
    if (expect(helikon_usb_pop(&event, NULL) == 1, "OUT completion event"))
        return 1;
    if (expect(event == HELIKON_USB_EVENT_EP2_OUT_DONE, "the event is OUT done"))
        return 1;

    /* --- bus reset clears host state ------------------------------------ */
    mock_set_gint(HELIKON_USB_GINT_USBRST);
    expect(helikon_usb_irq() > 0, "USBRST raises an IRQ");
    if (expect(helikon_usb_pop(&event, NULL) == 1, "reset event"))
        return 1;
    if (expect(event == HELIKON_USB_EVENT_BUS_RESET, "the event is BUS_RESET"))
        return 1;
    if (expect(helikon_usb_address() == 0u, "address cleared"))
        return 1;
    if (expect(helikon_usb_is_configured() == 0, "configuration cleared"))
        return 1;
    if (expect((otg(HELIKON_USB_REG_DOEPCTL(0u)) & HELIKON_USB_EPCTL_EPENA) !=
                   0u,
               "EP0 re-armed after reset"))
        return 1;

    /* --- disconnect and shutdown ---------------------------------------- */
    mock_set_gint(HELIKON_USB_GINT_DISCINT);
    expect(helikon_usb_irq() > 0, "DISCINT raises an IRQ");
    if (expect(helikon_usb_pop(&event, NULL) == 1, "disconnect event"))
        return 1;
    if (expect(event == HELIKON_USB_EVENT_DISCONNECT, "the event is DISCONNECT"))
        return 1;

    expect(helikon_usb_irq() == 0, "no pending status handles nothing");
    if (expect(helikon_usb_spurious_count() == 1u, "the call counted spurious"))
        return 1;

    helikon_usb_shutdown();
    if (expect(helikon_usb_is_configured() == 0, "shutdown releases config"))
        return 1;
    if (expect((otg(HELIKON_USB_REG_DCTL) & HELIKON_USB_DCTL_SDIS) != 0u,
               "shutdown disconnects"))
        return 1;

    if (failures == 0u)
        printf("PASS usb_hal\n");
    return failures == 0u ? 0 : 1;
}
