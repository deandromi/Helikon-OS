/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S5L8702 register definitions for the 6G HAL implementation.
 *
 * This header is included only by the firmware/hal/6g implementation. No core
 * module, block driver, HFS+ code, diagnostics or UI may include it: register
 * access is a HAL responsibility. Addresses and bit meanings are the
 * donor/paper-derived sequences referenced in each driver, not a board
 * measurement.
 */
#ifndef HELIKON_HAL_6G_S5L8702_REGS_H
#define HELIKON_HAL_6G_S5L8702_REGS_H

#include <stdint.h>

#define HELIKON_REG32(address) \
    (*(volatile uint32_t *)(uintptr_t)(address))
#define HELIKON_REG16(address) \
    (*(volatile uint16_t *)(uintptr_t)(address))

/* Clock and power management (donor system-s5l8702.c). */
#define S5L_CLK_BASE 0x3c500000u
#define S5L_PLLMODE (HELIKON_REG32(S5L_CLK_BASE + 0x44u))
#define S5L_PLLMODE_OSCSEL (1u << 8)
#define S5L_PWRCON(index) \
    HELIKON_REG32(S5L_CLK_BASE + ((index) == 0u ? 0x48u : 0x4cu))
#define S5L_CLKCON3 (HELIKON_REG32(S5L_CLK_BASE + 0x10u))
#define S5L_CG16_RTIME (HELIKON_REG16(S5L_CLK_BASE + 0x12u))

#define S5L_CLOCKGATE_TIMER 37u
#define S5L_CLOCKGATE_I2C0 36u
#define S5L_CLOCKGATE_LCD 1u
#define S5L_CLOCKGATE_ATA 5u
#define S5L_CLOCKGATE_CWHEEL 33u

/* 16/32-bit timer; timer E is the free-running microsecond counter. */
#define S5L_TIMER_BASE 0x3c700000u
#define S5L_TECON (HELIKON_REG32(S5L_TIMER_BASE + 0xa0u))
#define S5L_TECMD (HELIKON_REG32(S5L_TIMER_BASE + 0xa4u))
#define S5L_TEDATA0 (HELIKON_REG32(S5L_TIMER_BASE + 0xa8u))
#define S5L_TEPRE (HELIKON_REG32(S5L_TIMER_BASE + 0xb0u))
#define S5L_TECNT (HELIKON_REG32(S5L_TIMER_BASE + 0xb4u))

/* GPIO (donor gpio-s5l8702.c and app.lds-derived pin map). */
#define S5L_GPIO_BASE 0x3cf00000u
#define S5L_PCON(index) \
    HELIKON_REG32(S5L_GPIO_BASE + ((uint32_t)(index) << 5))
#define S5L_PDAT(index) \
    HELIKON_REG32(S5L_GPIO_BASE + 0x04u + ((uint32_t)(index) << 5))
#define S5L_PUNB(index) \
    HELIKON_REG32(S5L_GPIO_BASE + 0x0cu + ((uint32_t)(index) << 5))
#define S5L_PUNC(index) \
    HELIKON_REG32(S5L_GPIO_BASE + 0x10u + ((uint32_t)(index) << 5))
#define S5L_GPIOCMD (HELIKON_REG32(S5L_GPIO_BASE + 0x200u))

/* I2C bus 0 (donor i2c-s5l8702.c). */
#define S5L_IIC_BASE 0x3c600000u
#define S5L_IICCON(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x300000u * (uint32_t)(bus))
#define S5L_IICSTAT(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x04u + 0x300000u * (uint32_t)(bus))
#define S5L_IICADD(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x08u + 0x300000u * (uint32_t)(bus))
#define S5L_IICDS(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x0cu + 0x300000u * (uint32_t)(bus))
#define S5L_IICUNK10(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x10u + 0x300000u * (uint32_t)(bus))
#define S5L_IICUNK14(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x14u + 0x300000u * (uint32_t)(bus))
#define S5L_IICUNK18(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x18u + 0x300000u * (uint32_t)(bus))
#define S5L_IICSTA2(bus) \
    HELIKON_REG32(S5L_IIC_BASE + 0x20u + 0x300000u * (uint32_t)(bus))

/* I2S controller and PL080 DMA (donor pcm-s5l8702.c / pl080.c). */
#define S5L_I2S_BASE 0x3ca00000u
#define S5L_I2SCLKCON (HELIKON_REG32(S5L_I2S_BASE + 0x00u))
#define S5L_I2STXCON (HELIKON_REG32(S5L_I2S_BASE + 0x04u))
#define S5L_I2STXCOM (HELIKON_REG32(S5L_I2S_BASE + 0x08u))
#define S5L_I2STXDB (HELIKON_REG32(S5L_I2S_BASE + 0x10u))
#define S5L_I2SCLKDIV (HELIKON_REG32(S5L_I2S_BASE + 0x40u))

#define S5L_DMA0_BASE 0x38200000u
#define S5L_DMAC_CHANNEL(channel) \
    (S5L_DMA0_BASE + 0x100u + ((uint32_t)(channel) << 5))
#define S5L_DMAC_INTTCSTS (HELIKON_REG32(S5L_DMA0_BASE + 0x04u))
#define S5L_DMAC_INTTCCLR (HELIKON_REG32(S5L_DMA0_BASE + 0x08u))
#define S5L_DMAC_INTERRCLR (HELIKON_REG32(S5L_DMA0_BASE + 0x10u))
#define S5L_DMAC_RAWINTTCSTS (HELIKON_REG32(S5L_DMA0_BASE + 0x14u))
#define S5L_DMAC_CONFIG (HELIKON_REG32(S5L_DMA0_BASE + 0x30u))
#define S5L_DMAC_CH_SRC(channel) \
    HELIKON_REG32(S5L_DMAC_CHANNEL(channel) + 0x00u)
#define S5L_DMAC_CH_DST(channel) \
    HELIKON_REG32(S5L_DMAC_CHANNEL(channel) + 0x04u)
#define S5L_DMAC_CH_LLI(channel) \
    HELIKON_REG32(S5L_DMAC_CHANNEL(channel) + 0x08u)
#define S5L_DMAC_CH_CONTROL(channel) \
    HELIKON_REG32(S5L_DMAC_CHANNEL(channel) + 0x0cu)
#define S5L_DMAC_CH_CONFIG(channel) \
    HELIKON_REG32(S5L_DMAC_CHANNEL(channel) + 0x10u)

/* LCD interface controller (donor lcd-s5l8702.c / lcd-6g.c). */
#define S5L_LCD_BASE 0x38300000u
#define S5L_LCD_CON (HELIKON_REG32(S5L_LCD_BASE + 0x00u))
#define S5L_LCD_WCMD (HELIKON_REG32(S5L_LCD_BASE + 0x04u))
#define S5L_LCD_RCMD (HELIKON_REG32(S5L_LCD_BASE + 0x0cu))
#define S5L_LCD_RDATA (HELIKON_REG32(S5L_LCD_BASE + 0x10u))
#define S5L_LCD_DBUFF (HELIKON_REG32(S5L_LCD_BASE + 0x14u))
#define S5L_LCD_INTCON (HELIKON_REG32(S5L_LCD_BASE + 0x18u))
#define S5L_LCD_STATUS (HELIKON_REG32(S5L_LCD_BASE + 0x1cu))
#define S5L_LCD_PHTIME (HELIKON_REG32(S5L_LCD_BASE + 0x20u))
#define S5L_LCD_WDATA (HELIKON_REG32(S5L_LCD_BASE + 0x40u))

/* ATA controller (donor storage_ata-6g.c + s5l87xx.h). */
#define S5L_ATA_BASE 0x38700000u
#define S5L_ATA_CONTROL (HELIKON_REG32(S5L_ATA_BASE + 0x00u))
#define S5L_ATA_STATUS (HELIKON_REG32(S5L_ATA_BASE + 0x04u))
#define S5L_ATA_COMMAND (HELIKON_REG32(S5L_ATA_BASE + 0x08u))
#define S5L_ATA_SWRST (HELIKON_REG32(S5L_ATA_BASE + 0x0cu))
#define S5L_ATA_IRQ (HELIKON_REG32(S5L_ATA_BASE + 0x10u))
#define S5L_ATA_IRQ_MASK (HELIKON_REG32(S5L_ATA_BASE + 0x14u))
#define S5L_ATA_CFG (HELIKON_REG32(S5L_ATA_BASE + 0x18u))
#define S5L_ATA_PIO_TIME (HELIKON_REG32(S5L_ATA_BASE + 0x2cu))
#define S5L_ATA_PIO_DTR (HELIKON_REG32(S5L_ATA_BASE + 0x54u))
#define S5L_ATA_PIO_FED (HELIKON_REG32(S5L_ATA_BASE + 0x58u))
#define S5L_ATA_PIO_SCR (HELIKON_REG32(S5L_ATA_BASE + 0x5cu))
#define S5L_ATA_PIO_LLR (HELIKON_REG32(S5L_ATA_BASE + 0x60u))
#define S5L_ATA_PIO_LMR (HELIKON_REG32(S5L_ATA_BASE + 0x64u))
#define S5L_ATA_PIO_LHR (HELIKON_REG32(S5L_ATA_BASE + 0x68u))
#define S5L_ATA_PIO_DVR (HELIKON_REG32(S5L_ATA_BASE + 0x6cu))
#define S5L_ATA_PIO_CSD (HELIKON_REG32(S5L_ATA_BASE + 0x70u))
#define S5L_ATA_PIO_DAD (HELIKON_REG32(S5L_ATA_BASE + 0x74u))
#define S5L_ATA_PIO_READY (HELIKON_REG32(S5L_ATA_BASE + 0x78u))
#define S5L_ATA_PIO_RDATA (HELIKON_REG32(S5L_ATA_BASE + 0x7cu))

/* Click-wheel controller (donor button-clickwheel.c). */
#define S5L_WHEEL_BASE 0x3c200000u
#define S5L_WHEEL00 (HELIKON_REG32(S5L_WHEEL_BASE + 0x00u))
#define S5L_WHEEL04 (HELIKON_REG32(S5L_WHEEL_BASE + 0x04u))
#define S5L_WHEEL08 (HELIKON_REG32(S5L_WHEEL_BASE + 0x08u))
#define S5L_WHEEL10 (HELIKON_REG32(S5L_WHEEL_BASE + 0x10u))
#define S5L_WHEELINT (HELIKON_REG32(S5L_WHEEL_BASE + 0x14u))
#define S5L_WHEELRX (HELIKON_REG32(S5L_WHEEL_BASE + 0x18u))
#define S5L_WHEELTX (HELIKON_REG32(S5L_WHEEL_BASE + 0x1cu))

/* Panel strapping: PDAT6[5:4] selects the LCD type on the 6G. */
#define S5L_LCD_TYPE_PORT 6u
#define S5L_LCD_TYPE_SHIFT 4u
#define S5L_LCD_TYPE_MASK 0x30u

#endif
