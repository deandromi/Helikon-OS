/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "lcd.h"

#include "error.h"
#include "s5l8702_regs.h"

/* 8-bit command-set sequence actions. */
#define LCD8_CMD 0x00u
#define LCD8_SLEEP 0x01u
#define LCD8_END 0xffu

/* 16-bit command-set sequence actions and packing. */
#define LCD16_CMD 0x00u
#define LCD16_SLEEP 0x01u
#define LCD16_MREG 0x02u
#define LCD16_DELAY 0x03u
#define LCD16_END 0xffu
#define LCD16_PACK(action, parameter) \
    ((uint16_t)((action) | ((uint16_t)(parameter) << 8)))

/* Donor lcd-s5l8702.c modes: P8 command, P18 command, P16 RGB565 frame. */
#define LCD_MODE_P8 0x80000c20u
#define LCD_MODE_P18 0x80000da8u
#define LCD_MODE_P16 0x80100db0u

/* Donor lcd-6g.c: panel type 0/1 uses an 8-bit command set. */
static const uint8_t lcd_init_seq_0[] = {
    LCD8_CMD, 0x11, 0,       /* Sleep Out */
    LCD8_SLEEP, 3,           /* 30 ms */
    LCD8_CMD, 0x35, 1, 0x00, /* Tearing Effect Line On */
    LCD8_CMD, 0x3a, 1, 0x06, /* Pixel Format Set */
    LCD8_CMD, 0x36, 1, 0x00, /* Memory Access Control */
    LCD8_CMD, 0x13, 0,       /* Normal Mode On */
    LCD8_CMD, 0x29, 0,       /* Display On */
    LCD8_END
};

static const uint8_t lcd_init_seq_1[] = {
    LCD8_CMD, 0xb0, 21,
    0x3a, 0x3a, 0x80, 0x80, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x0a, 0x3c, 0x30, 0x0f, 0x00,
    0x01, 0x54, 0x06, 0x66, 0x66,
    LCD8_CMD, 0xb8, 1, 0xd8,
    LCD8_CMD, 0xb1, 30,
    0x14, 0x59, 0x00, 0x15, 0x57, 0x27, 0x04, 0x85,
    0x14, 0x59, 0x00, 0x15, 0x57, 0x27, 0x04, 0x85,
    0x14, 0x09, 0x15, 0x57, 0x27, 0x04, 0x05,
    0x14, 0x09, 0x15, 0x57, 0x27, 0x04, 0x05,
    LCD8_CMD, 0xd2, 1, 0x01,
    LCD8_CMD, 0xe0, 13,
    0x00, 0x00, 0x00, 0x05, 0x0b, 0x12, 0x16, 0x1f,
    0x25, 0x22, 0x24, 0x29, 0x1c,
    LCD8_CMD, 0xe1, 13,
    0x08, 0x01, 0x01, 0x06, 0x0b, 0x11, 0x15, 0x1f,
    0x27, 0x26, 0x29, 0x2f, 0x1e,
    LCD8_CMD, 0xe2, 13,
    0x07, 0x01, 0x01, 0x05, 0x09, 0x0f, 0x13, 0x1e,
    0x26, 0x25, 0x28, 0x2e, 0x1e,
    LCD8_CMD, 0xe3, 13,
    0x00, 0x00, 0x00, 0x05, 0x0b, 0x12, 0x16, 0x1f,
    0x25, 0x22, 0x24, 0x29, 0x1c,
    LCD8_CMD, 0xe4, 13,
    0x08, 0x01, 0x01, 0x06, 0x0b, 0x11, 0x15, 0x1f,
    0x27, 0x26, 0x29, 0x2f, 0x1e,
    LCD8_CMD, 0xe5, 13,
    0x07, 0x01, 0x01, 0x05, 0x09, 0x0f, 0x13, 0x1e,
    0x26, 0x25, 0x28, 0x2e, 0x1e,
    LCD8_CMD, 0x3a, 1, 0x06, /* Pixel Format Set */
    LCD8_CMD, 0xc2, 1, 0x00, /* Power Control */
    LCD8_CMD, 0x35, 1, 0x00, /* Tearing Effect Line On */
    LCD8_CMD, 0x11, 0,       /* Sleep Out */
    LCD8_SLEEP, 6,           /* 60 ms */
    LCD8_CMD, 0x13, 0,       /* Normal Mode On */
    LCD8_CMD, 0x29, 0,       /* Display On */
    LCD8_END
};

static const uint16_t lcd_init_seq_23[] = {
    LCD16_PACK(LCD16_CMD, 0), 0x000,
    LCD16_PACK(LCD16_DELAY, 20),
    LCD16_PACK(LCD16_CMD, 0), 0x000,
    LCD16_PACK(LCD16_DELAY, 20),
    LCD16_PACK(LCD16_CMD, 0), 0x000,
    LCD16_PACK(LCD16_DELAY, 20),
    LCD16_PACK(LCD16_CMD, 0), 0x000,
    LCD16_PACK(LCD16_DELAY, 20),
    LCD16_PACK(LCD16_CMD, 0), 0x000,
    LCD16_PACK(LCD16_DELAY, 20),
    LCD16_PACK(LCD16_CMD, 0), 0x000,
    LCD16_PACK(LCD16_DELAY, 20),

    LCD16_PACK(LCD16_MREG, 1), 0x008, 0x0808,
    LCD16_PACK(LCD16_MREG, 7), 0x010,
    0x0013, 0x0300, 0x0101, 0x0a03, 0x0a0e, 0x0a19, 0x2402,
    LCD16_PACK(LCD16_MREG, 1), 0x018, 0x0001,
    LCD16_PACK(LCD16_MREG, 1), 0x090, 0x0021,

    LCD16_PACK(LCD16_MREG, 14), 0x300,
    0x0307, 0x0003, 0x0402, 0x0303, 0x0300, 0x0407, 0x1c04,
    0x0307, 0x0003, 0x0402, 0x0303, 0x0300, 0x0407, 0x1c04,
    LCD16_PACK(LCD16_MREG, 14), 0x310,
    0x0707, 0x0407, 0x0306, 0x0303, 0x0300, 0x0407, 0x1c01,
    0x0707, 0x0407, 0x0306, 0x0303, 0x0300, 0x0407, 0x1c01,
    LCD16_PACK(LCD16_MREG, 14), 0x320,
    0x0206, 0x0102, 0x0404, 0x0303, 0x0300, 0x0407, 0x1c1f,
    0x0206, 0x0102, 0x0404, 0x0303, 0x0300, 0x0407, 0x1c1f,

    LCD16_PACK(LCD16_MREG, 2), 0x400, 0x001d, 0x0001,
    LCD16_PACK(LCD16_MREG, 1), 0x205, 0x0060,

    LCD16_PACK(LCD16_MREG, 1), 0x007, 0x0001,
    LCD16_PACK(LCD16_MREG, 1), 0x031, 0x0071,
    LCD16_PACK(LCD16_MREG, 1), 0x110, 0x0001,
    LCD16_PACK(LCD16_MREG, 6), 0x100,
    0x17b0, 0x0220, 0x00bd, 0x1500, 0x0103, 0x0105,

    LCD16_PACK(LCD16_MREG, 1), 0x007, 0x0021,
    LCD16_PACK(LCD16_MREG, 1), 0x001, 0x0110,
    LCD16_PACK(LCD16_MREG, 1), 0x003, 0x0230,
    LCD16_PACK(LCD16_MREG, 1), 0x002, 0x0500,
    LCD16_PACK(LCD16_MREG, 1), 0x007, 0x0031,
    LCD16_PACK(LCD16_MREG, 1), 0x030, 0x0007,
    LCD16_PACK(LCD16_SLEEP, 3),
    LCD16_PACK(LCD16_MREG, 1), 0x030, 0x03ff,
    LCD16_PACK(LCD16_SLEEP, 6),
    LCD16_PACK(LCD16_MREG, 1), 0x007, 0x0072,
    LCD16_PACK(LCD16_SLEEP, 15),
    LCD16_PACK(LCD16_MREG, 1), 0x007, 0x0173,
    LCD16_END
};

static int wait_status(struct helikon_lcd *lcd, uint32_t mask, int set,
                       uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(lcd->clock);
    for (;;) {
        uint32_t status = S5L_LCD_STATUS & mask;
        if ((set && status == mask) || (!set && status == 0u))
            return HELIKON_OK;
        if (helikon_clock_expired(lcd->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
}

static int lcd_write_config(struct helikon_lcd *lcd, uint32_t config,
                            uint32_t timeout_ms)
{
    int rc = wait_status(lcd, 0x2u, 1, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    helikon_clock_delay_us(lcd->clock, 1u);
    S5L_LCD_CON = config;
    return HELIKON_OK;
}

static int lcd_write_cmd(struct helikon_lcd *lcd, uint16_t command,
                         uint32_t timeout_ms)
{
    int rc = wait_status(lcd, 0x10u, 0, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_LCD_WCMD = command;
    return HELIKON_OK;
}

static int lcd_write_data(struct helikon_lcd *lcd, uint16_t data,
                          uint32_t timeout_ms)
{
    int rc = wait_status(lcd, 0x10u, 0, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_LCD_WDATA = data;
    return HELIKON_OK;
}

static int lcd_send_cmd8(struct helikon_lcd *lcd, uint8_t command,
                         const uint8_t *data, uint8_t length,
                         uint32_t timeout_ms)
{
    int rc = lcd_write_cmd(lcd, command, timeout_ms);
    for (uint8_t i = 0; rc == HELIKON_OK && i < length; ++i)
        rc = lcd_write_data(lcd, data[i], timeout_ms);
    return rc;
}

static int lcd_write_reg16(struct helikon_lcd *lcd, uint16_t reg,
                           uint16_t value, uint32_t timeout_ms)
{
    int rc = lcd_write_cmd(lcd, reg, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return lcd_write_data(lcd, value, timeout_ms);
}

static int lcd_run_seq8(struct helikon_lcd *lcd, const uint8_t *sequence,
                        uint32_t timeout_ms)
{
    int rc = HELIKON_OK;
    while (rc == HELIKON_OK) {
        uint8_t action = *sequence++;
        if (action == LCD8_END)
            break;
        if (action == LCD8_SLEEP) {
            uint8_t ticks = *sequence++;
            for (uint8_t i = 0; i < ticks; ++i)
                helikon_clock_delay_us(lcd->clock, 10000u);
            continue;
        }
        if (action != LCD8_CMD)
            return HELIKON_ERR_FORMAT;
        uint8_t command = *sequence++;
        uint8_t length = *sequence++;
        rc = lcd_send_cmd8(lcd, command, sequence, length, timeout_ms);
        sequence += length;
    }
    return rc;
}

static int lcd_run_seq16(struct helikon_lcd *lcd, const uint16_t *sequence,
                         uint32_t timeout_ms)
{
    int rc = HELIKON_OK;
    while (rc == HELIKON_OK) {
        uint16_t entry = *sequence++;
        uint8_t action = (uint8_t)(entry & 0xffu);
        uint16_t parameter = (uint16_t)(entry >> 8);
        if (action == LCD16_END)
            break;
        if (action == LCD16_CMD) {
            rc = lcd_write_cmd(lcd, *sequence++, timeout_ms);
        } else if (action == LCD16_MREG) {
            uint16_t reg = *sequence++;
            for (uint16_t i = 0; rc == HELIKON_OK && i < parameter; ++i)
                rc = lcd_write_reg16(lcd, reg++, *sequence++, timeout_ms);
        } else if (action == LCD16_SLEEP) {
            for (uint16_t i = 0; i < parameter; ++i)
                helikon_clock_delay_us(lcd->clock, 10000u);
        } else if (action == LCD16_DELAY) {
            helikon_clock_delay_us(lcd->clock,
                                   (uint32_t)parameter * 64u);
        } else {
            return HELIKON_ERR_FORMAT;
        }
    }
    return rc;
}

static int lcd_set_window(struct helikon_lcd *lcd, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height, uint32_t timeout_ms)
{
    uint32_t xe = x + width - 1u;
    uint32_t ye = y + height - 1u;

    int rc = lcd_write_config(lcd, lcd->cmd_mode, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    if (lcd->cmdset == 16u) {
        rc = lcd_write_reg16(lcd, 0x210u, (uint16_t)x, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_write_reg16(lcd, 0x211u, (uint16_t)xe, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_write_reg16(lcd, 0x212u, (uint16_t)y, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_write_reg16(lcd, 0x213u, (uint16_t)ye, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_write_reg16(lcd, 0x200u, (uint16_t)x, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_write_reg16(lcd, 0x201u, (uint16_t)y, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_write_cmd(lcd, 0x202u, timeout_ms);
        return rc;
    }

    uint8_t column[4] = {
        (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(xe >> 8), (uint8_t)xe
    };
    uint8_t row[4] = {
        (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(ye >> 8), (uint8_t)ye
    };
    rc = lcd_send_cmd8(lcd, 0x2au, column, sizeof(column), timeout_ms);
    if (rc == HELIKON_OK)
        rc = lcd_send_cmd8(lcd, 0x2bu, row, sizeof(row), timeout_ms);
    if (rc == HELIKON_OK)
        rc = lcd_write_cmd(lcd, 0x2cu, timeout_ms);
    return rc;
}

int helikon_lcd_init(struct helikon_lcd *lcd, struct helikon_clock *clock,
                     uint32_t timeout_ms)
{
    if (lcd == NULL || clock == NULL || !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    lcd->clock = clock;
    lcd->initialized = 0;

    S5L_PWRCON(S5L_CLOCKGATE_LCD >> 5) &=
        ~(1u << (S5L_CLOCKGATE_LCD & 0x1fu));
    S5L_LCD_PHTIME = 0x33u;

    /*
     * PDAT6[5:4] is the panel-type strap. Configure those two pins as inputs
     * before reading them; the donor gpio_preinit table does the same.
     */
    S5L_PCON(6) &= ~0x00ff0000u;

    lcd->panel_type = (uint8_t)((S5L_PDAT(S5L_LCD_TYPE_PORT) &
                                 S5L_LCD_TYPE_MASK) >>
                                S5L_LCD_TYPE_SHIFT);
    int rc;
    if (lcd->panel_type <= 1u) {
        lcd->cmdset = 8u;
        lcd->cmd_mode = LCD_MODE_P8;
        lcd->frame_mode = LCD_MODE_P16;
        rc = lcd_write_config(lcd, lcd->cmd_mode, timeout_ms);
        if (rc == HELIKON_OK) {
            rc = lcd_run_seq8(lcd, lcd->panel_type == 0u ? lcd_init_seq_0
                                                         : lcd_init_seq_1,
                              timeout_ms);
        }
    } else {
        lcd->cmdset = 16u;
        lcd->cmd_mode = LCD_MODE_P18;
        lcd->frame_mode = LCD_MODE_P16;
        rc = lcd_write_config(lcd, lcd->cmd_mode, timeout_ms);
        if (rc == HELIKON_OK)
            rc = lcd_run_seq16(lcd, lcd_init_seq_23, timeout_ms);
    }
    if (rc != HELIKON_OK)
        return rc;

    lcd->initialized = 1;
    return HELIKON_OK;
}

int helikon_lcd_attach(struct helikon_lcd *lcd, struct helikon_clock *clock)
{
    if (lcd == NULL || clock == NULL || !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    lcd->clock = clock;
    lcd->initialized = 0;

    S5L_PWRCON(S5L_CLOCKGATE_LCD >> 5) &=
        ~(1u << (S5L_CLOCKGATE_LCD & 0x1fu));
    S5L_LCD_PHTIME = 0x33u;
    S5L_PCON(6) &= ~0x00ff0000u;

    lcd->panel_type = (uint8_t)((S5L_PDAT(S5L_LCD_TYPE_PORT) &
                                 S5L_LCD_TYPE_MASK) >>
                                S5L_LCD_TYPE_SHIFT);
    if (lcd->panel_type <= 1u) {
        lcd->cmdset = 8u;
        lcd->cmd_mode = LCD_MODE_P8;
    } else {
        lcd->cmdset = 16u;
        lcd->cmd_mode = LCD_MODE_P18;
    }
    lcd->frame_mode = LCD_MODE_P16;
    lcd->initialized = 1;
    return HELIKON_OK;
}

int helikon_lcd_present(void *context, const uint16_t *pixels, uint32_t stride,
                        uint32_t x, uint32_t y, uint32_t width,
                        uint32_t height)
{
    struct helikon_lcd *lcd = context;
    if (lcd == NULL || !lcd->initialized || pixels == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (width == 0u || height == 0u || x >= HELIKON_LCD_WIDTH ||
        y >= HELIKON_LCD_HEIGHT || width > HELIKON_LCD_WIDTH - x ||
        height > HELIKON_LCD_HEIGHT - y || stride < x + width)
        return HELIKON_ERR_ARGUMENT;

    /* The panel transfer can take longer than a frame; F1 uses a 1 s bound. */
    const uint32_t timeout_ms = 1000u;
    int rc = lcd_set_window(lcd, x, y, width, height, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    rc = lcd_write_config(lcd, lcd->frame_mode, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    for (uint32_t row = 0; row < height; ++row) {
        const uint16_t *line = pixels + (size_t)(y + row) * stride + x;
        for (uint32_t column = 0; column < width; ++column) {
            rc = lcd_write_data(lcd, line[column], timeout_ms);
            if (rc != HELIKON_OK)
                return rc;
        }
    }
    return HELIKON_OK;
}
