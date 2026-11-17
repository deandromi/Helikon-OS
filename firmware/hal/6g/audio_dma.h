/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pure PL080 DMA control words for the 6G audio path.
 *
 * This header deliberately contains no register access, so the host player
 * tests can check the exact control word without a device. The values port
 * the Rockbox pcm-s5l8702.c configuration: memory-to-IIS0_TX, 16-bit
 * samples, 8-beat source burst, 4-beat destination burst, source increment
 * and a fixed destination.
 */
#ifndef HELIKON_HAL_6G_AUDIO_DMA_H
#define HELIKON_HAL_6G_AUDIO_DMA_H

#include <stdint.h>

/* 2047 frames * two 16-bit samples per frame. */
#define HELIKON_AUDIO_DMA_MAX_SAMPLES 4094u
#define HELIKON_AUDIO_DMA_COUNT_MASK 0x0fffu

/*
 * DMACCxControl[31] (I) is the terminal count interrupt enable for this
 * LLI. It must be set even though the driver polls DMACRawIntTCStatus
 * instead of taking the channel IRQ: without it the PL080 never raises the
 * terminal count status at all. DMACCxConfiguration[15] (ITC) stays clear
 * so the interrupt remains masked.
 */
#define HELIKON_AUDIO_DMA_CONTROL_I (1u << 31)
#define HELIKON_AUDIO_DMA_CONTROL_PROT (7u << 28)
#define HELIKON_AUDIO_DMA_CONTROL_SI (1u << 26)
#define HELIKON_AUDIO_DMA_CONTROL_SWIDTH_16 (1u << 18)
#define HELIKON_AUDIO_DMA_CONTROL_DWIDTH_16 (1u << 21)
#define HELIKON_AUDIO_DMA_CONTROL_SBSIZE_8 (2u << 12)
#define HELIKON_AUDIO_DMA_CONTROL_DBSIZE_4 (1u << 15)

static inline uint32_t helikon_audio_dma_control(uint32_t samples)
{
    return HELIKON_AUDIO_DMA_CONTROL_I |
           HELIKON_AUDIO_DMA_CONTROL_PROT |
           HELIKON_AUDIO_DMA_CONTROL_SI |
           HELIKON_AUDIO_DMA_CONTROL_SWIDTH_16 |
           HELIKON_AUDIO_DMA_CONTROL_DWIDTH_16 |
           HELIKON_AUDIO_DMA_CONTROL_SBSIZE_8 |
           HELIKON_AUDIO_DMA_CONTROL_DBSIZE_4 |
           (samples & HELIKON_AUDIO_DMA_COUNT_MASK);
}

/*
 * DMACCxConfiguration: channel enable, destination peripheral IIS0_TX
 * (0xa), memory-to-peripheral flow control (1). IE and ITC stay clear so
 * the channel error and terminal count interrupts stay masked; the driver
 * polls the raw status registers.
 */
static inline uint32_t helikon_audio_dma_config(void)
{
    return 1u | (0x0au << 6) | (1u << 11);
}

#endif
