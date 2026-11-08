/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "audio.h"

#include <stddef.h>

#include "audio_dma.h"
#include "cpu.h"
#include "error.h"
#include "s5l8702_regs.h"

#define CS42L55_ADDRESS 0x94u
#define CS42L55_I2C_BUS 0u
#define AUDIO_MCLK_HZ 12000000u
#define AUDIO_DMA_CHANNEL 0u
#define AUDIO_DMA_FRAMES 2047u
#define AUDIO_VOLUME_MINUS_18DB 0x6eu

/* CS42L55 registers used by the playback path. */
#define CS42L55_HIDDENCTL 0x00u
#define CS42L55_HIDDENCTL_UNLOCK 0x99u
#define CS42L55_HIDDENCTL_LOCK 0x00u
#define CS42L55_PWRCTL1 0x02u
#define CS42L55_PWRCTL2 0x03u
#define CS42L55_CLKCTL1 0x04u
#define CS42L55_CLKCTL2 0x05u
#define CS42L55_CLSHCTL 0x06u
#define CS42L55_MISCCTL 0x07u
#define CS42L55_PGAACTL 0x0bu
#define CS42L55_PGABCTL 0x0cu
#define CS42L55_PLAYCTL 0x0fu
#define CS42L55_HPACTL 0x1au
#define CS42L55_HPBCTL 0x1bu
#define CS42L55_LINEACTL 0x1cu
#define CS42L55_LINEBCTL 0x1du

static int16_t dma_buffer[AUDIO_DMA_FRAMES * 2u]
    __attribute__((aligned(32)));

static uint32_t dma_timeout_stage(uint32_t channel_config);

static int dmac_clock_on(struct helikon_audio *audio)
{
    S5L_PWRCON(S5L_CLOCKGATE_DMAC0 >> 5) &=
        ~(1u << (S5L_CLOCKGATE_DMAC0 & 0x1fu));
    if ((S5L_PWRCON(S5L_CLOCKGATE_DMAC0 >> 5) &
         (1u << (S5L_CLOCKGATE_DMAC0 & 0x1fu))) != 0u) {
        audio->stage = HELIKON_AUDIO_STAGE_DMA_CLOCK;
        return HELIKON_ERR_IO;
    }
    return HELIKON_OK;
}

static int i2c_wait_ready(struct helikon_audio *audio, uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(audio->clock);
    while (S5L_IICUNK10(CS42L55_I2C_BUS) != 0u) {
        if (helikon_clock_expired(audio->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
    return HELIKON_OK;
}

static int i2c_wait_io(struct helikon_audio *audio, uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(audio->clock);
    while ((S5L_IICSTAT(CS42L55_I2C_BUS) & (1u << 5)) != 0u &&
           (S5L_IICSTA2(CS42L55_I2C_BUS) & ((1u << 8) | (1u << 13))) == 0u) {
        int rc = i2c_wait_ready(audio, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if (helikon_clock_expired(audio->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
    S5L_IICSTA2(CS42L55_I2C_BUS) |= (1u << 8) | (1u << 13);
    return HELIKON_OK;
}

static void i2c_clock_on(void)
{
    S5L_PWRCON(S5L_CLOCKGATE_I2C0 >> 5) &=
        ~(1u << (S5L_CLOCKGATE_I2C0 & 0x1fu));
}

static void i2c_clock_off(void)
{
    S5L_PWRCON(S5L_CLOCKGATE_I2C0 >> 5) |=
        (1u << (S5L_CLOCKGATE_I2C0 & 0x1fu));
}

static int i2c_start(struct helikon_audio *audio, uint32_t timeout_ms)
{
    int rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    S5L_IICCON(CS42L55_I2C_BUS) = 1u << 7;
    S5L_IICSTAT(CS42L55_I2C_BUS) = 0xc0u;
    rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    S5L_IICDS(CS42L55_I2C_BUS) = CS42L55_ADDRESS;
    rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    S5L_IICSTAT(CS42L55_I2C_BUS) = 0xf0u;
    rc = i2c_wait_io(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return (S5L_IICSTAT(CS42L55_I2C_BUS) & 1u) != 0u ? HELIKON_ERR_IO
                                                    : HELIKON_OK;
}

static int i2c_stop(struct helikon_audio *audio, uint32_t timeout_ms)
{
    int rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_IICSTAT(CS42L55_I2C_BUS) &= ~0x20u;
    rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_IICCON(CS42L55_I2C_BUS) = 0x10u;
    return i2c_wait_io(audio, timeout_ms);
}

static int i2c_send(struct helikon_audio *audio, uint8_t value,
                    uint32_t timeout_ms)
{
    int rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_IICDS(CS42L55_I2C_BUS) = value;
    helikon_clock_delay_us(audio->clock, 5u);
    rc = i2c_wait_ready(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_IICCON(CS42L55_I2C_BUS) = S5L_IICCON(CS42L55_I2C_BUS);
    rc = i2c_wait_io(audio, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return (S5L_IICSTAT(CS42L55_I2C_BUS) & 1u) != 0u ? HELIKON_ERR_IO
                                                    : HELIKON_OK;
}

static int codec_write(struct helikon_audio *audio, uint8_t reg, uint8_t value)
{
    i2c_clock_on();
    int rc = i2c_start(audio, audio->timeout_ms);
    if (rc == HELIKON_OK)
        rc = i2c_send(audio, reg, audio->timeout_ms);
    if (rc == HELIKON_OK)
        rc = i2c_send(audio, value, audio->timeout_ms);
    int stop = i2c_stop(audio, audio->timeout_ms);
    i2c_clock_off();
    return rc != HELIKON_OK ? rc : stop;
}

static int codec_mute(struct helikon_audio *audio, int mute)
{
    return codec_write(audio, CS42L55_PLAYCTL, mute ? 0x83u : 0x80u);
}

static int codec_write_sequence(struct helikon_audio *audio,
                                const uint8_t *sequence, size_t count)
{
    for (size_t index = 0u; index < count; index += 2u) {
        int rc = codec_write(audio, sequence[index], sequence[index + 1u]);
        if (rc != HELIKON_OK)
            return rc;
    }
    return HELIKON_OK;
}

static void codec_reset(void)
{
    /* GPIO3.3 is the CS42L55 reset line; the loader configured the pin. */
    S5L_PDAT(3) |= 8u;
    S5L_PDAT(3) &= ~8u;
}

static int codec_init(struct helikon_audio *audio)
{
    audio->stage = HELIKON_AUDIO_STAGE_CODEC;
    static const uint8_t sequence[] = {
        CS42L55_HIDDENCTL, CS42L55_HIDDENCTL_UNLOCK,
        0x2e, 0x30,
        0x32, 0x07,
        0x33, 0xff,
        0x34, 0xf8,
        0x35, 0xdc,
        0x36, 0xfc,
        0x37, 0xac,
        0x3a, 0xf8,
        0x3c, 0xd3,
        0x3d, 0x23,
        0x3e, 0x81,
        0x3f, 0x46,
        CS42L55_HIDDENCTL, CS42L55_HIDDENCTL_LOCK,
        CS42L55_PWRCTL2, 0xaa, /* line and headphone always on */
        CS42L55_CLKCTL1, 0x2e, /* master, SCLK before MCLK, MCLK/2 */
        CS42L55_CLKCTL2, 0x0b, /* 44.1 kHz */
        CS42L55_MISCCTL, 0x1c,
        CS42L55_PWRCTL1, 0x0f, /* codec, ADCs and charge pump off */
        CS42L55_PLAYCTL, 0x83, /* DSP down, master mute */
        CS42L55_PGAACTL, 0x00,
        CS42L55_PGABCTL, 0x00,
        CS42L55_HPACTL, 0x80,
        CS42L55_HPBCTL, 0x80,
        CS42L55_LINEACTL, 0x80,
        CS42L55_LINEBCTL, 0x80,
        CS42L55_PWRCTL1, 0x0e  /* codec on, ADCs and charge pump off */
    };

    int rc = helikon_pmu_write(audio->pmu, HELIKON_PMU_REG_LDO4ENA, 1u,
                               audio->timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    helikon_clock_delay_us(audio->clock, 20000u);

    S5L_CLKCON3 &= ~0x0000ffffu;
    helikon_clock_delay_us(audio->clock, 100u);

    codec_reset();
    helikon_clock_delay_us(audio->clock, 10000u);
    S5L_PDAT(3) |= 8u;
    helikon_clock_delay_us(audio->clock, 10000u);

    rc = codec_write_sequence(audio, sequence, sizeof(sequence));
    if (rc != HELIKON_OK)
        return rc;

    /* 0x6e is -18 dB in the CS42L55 7-bit volume field, unmuted. */
    rc = codec_write(audio, CS42L55_HPACTL, AUDIO_VOLUME_MINUS_18DB);
    if (rc == HELIKON_OK)
        rc = codec_write(audio, CS42L55_HPBCTL, AUDIO_VOLUME_MINUS_18DB);
    if (rc == HELIKON_OK)
        rc = codec_write(audio, CS42L55_LINEACTL, AUDIO_VOLUME_MINUS_18DB);
    if (rc == HELIKON_OK)
        rc = codec_write(audio, CS42L55_LINEBCTL, AUDIO_VOLUME_MINUS_18DB);
    if (rc == HELIKON_OK)
        rc = codec_write(audio, CS42L55_CLSHCTL, 0x30);
    if (rc == HELIKON_OK)
        rc = codec_write(audio, CS42L55_PLAYCTL, 0x80);
    return rc;
}

static int dma_chunk(struct helikon_audio *audio, const int16_t *pcm,
                     size_t frames)
{
    if (frames == 0u || frames > AUDIO_DMA_FRAMES) {
        audio->stage = HELIKON_AUDIO_STAGE_BUFFER;
        return HELIKON_ERR_BUFFER;
    }

    audio->stage = HELIKON_AUDIO_STAGE_BUFFER;
    size_t samples = frames * 2u;
    for (size_t index = 0u; index < samples; ++index)
        dma_buffer[index] = pcm[index];

    audio->stage = HELIKON_AUDIO_STAGE_CACHE;
    helikon_cpu_clean_dcache_range(dma_buffer, samples * sizeof(int16_t));

    audio->stage = HELIKON_AUDIO_STAGE_DMA_SETUP;
    S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) = 0u;
    uint64_t start = helikon_clock_now_us(audio->clock);
    while ((S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) & (1u << 17)) != 0u) {
        if (helikon_clock_expired(audio->clock, start, audio->timeout_ms)) {
            audio->stage = HELIKON_AUDIO_STAGE_DMA_CONFIG;
            return HELIKON_ERR_BUSY;
        }
    }
    S5L_DMAC_INTTCCLR = 1u << AUDIO_DMA_CHANNEL;
    S5L_DMAC_INTERRCLR = 1u << AUDIO_DMA_CHANNEL;
    S5L_DMAC_CONFIG |= 1u;

    S5L_DMAC_CH_SRC(AUDIO_DMA_CHANNEL) = (uint32_t)(uintptr_t)dma_buffer;
    S5L_DMAC_CH_DST(AUDIO_DMA_CHANNEL) = S5L_I2S_BASE + 0x10u;
    S5L_DMAC_CH_LLI(AUDIO_DMA_CHANNEL) = 0u;
    S5L_DMAC_CH_CONTROL(AUDIO_DMA_CHANNEL) =
        helikon_audio_dma_control((uint32_t)samples);
    S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) = helikon_audio_dma_config();

    audio->stage = HELIKON_AUDIO_STAGE_DMA_WAIT;
    start = helikon_clock_now_us(audio->clock);
    while ((S5L_DMAC_RAWINTTCSTS & (1u << AUDIO_DMA_CHANNEL)) == 0u) {
        if ((S5L_DMAC_RAWINTERRSTS & (1u << AUDIO_DMA_CHANNEL)) != 0u) {
            S5L_DMAC_INTERRCLR = 1u << AUDIO_DMA_CHANNEL;
            S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) = 0u;
            return HELIKON_ERR_IO;
        }
        if (helikon_clock_expired(audio->clock, start, audio->timeout_ms)) {
            audio->stage =
                dma_timeout_stage(S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL));
            S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) = 0u;
            return HELIKON_ERR_BUSY;
        }
    }
    S5L_DMAC_INTTCCLR = 1u << AUDIO_DMA_CHANNEL;
    audio->stage = HELIKON_AUDIO_STAGE_DMA_ACTIVE;
    start = helikon_clock_now_us(audio->clock);
    while ((S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) & (1u << 17)) != 0u) {
        if (helikon_clock_expired(audio->clock, start, audio->timeout_ms))
            return HELIKON_ERR_BUSY;
    }
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

static void i2s_stop(void)
{
    S5L_I2STXCOM = 0x0au;
    S5L_I2SCLKCON = 0u;
    S5L_PWRCON(1) |= 1u << 7;
}

static int i2s_start(void)
{
    S5L_PWRCON(1) &= ~(1u << 7);
    S5L_I2SCLKCON = 1u;
    S5L_I2STXCOM = 0x0eu;

    /*
     * Read the gates back before queuing DMA. If AUD0 (CLKCON3) or the I2S
     * gate is still off, the FIFO can never request data and the DMA wait
     * would otherwise be reported as a generic timeout.
     */
    if ((S5L_PWRCON(1) & (1u << 7)) != 0u ||
        (S5L_I2SCLKCON & 1u) == 0u ||
        (S5L_CLKCON3 & 0x8000u) != 0u)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

static uint32_t dma_timeout_stage(uint32_t channel_config)
{
    uint32_t dmac_gate = S5L_PWRCON(S5L_CLOCKGATE_DMAC0 >> 5) &
                         (1u << (S5L_CLOCKGATE_DMAC0 & 0x1fu));
    if (dmac_gate != 0u)
        return HELIKON_AUDIO_STAGE_DMA_CLOCK;
    if ((channel_config & 1u) == 0u ||
        ((channel_config >> 6) & 0x0fu) != 0x0au ||
        ((channel_config >> 11) & 7u) != 1u)
        return HELIKON_AUDIO_STAGE_DMA_CONFIG;
    if ((S5L_PWRCON(1) & (1u << 7)) != 0u ||
        (S5L_I2SCLKCON & 1u) == 0u ||
        (S5L_CLKCON3 & 0x8000u) != 0u)
        return HELIKON_AUDIO_STAGE_I2S;
    if ((channel_config & (1u << 17)) != 0u)
        return HELIKON_AUDIO_STAGE_I2S_FIFO;
    return HELIKON_AUDIO_STAGE_DMA_CONFIG;
}

int helikon_audio_set_rate(struct helikon_audio *audio, uint32_t sample_rate)
{
    if (audio == NULL || !audio->initialized)
        return HELIKON_ERR_ARGUMENT;
    if (sample_rate != 44100u && sample_rate != 48000u)
        return HELIKON_ERR_UNSUPPORTED;

    audio->stage = HELIKON_AUDIO_STAGE_RATE;
    int rc = codec_write(audio, CS42L55_CLKCTL2,
                         sample_rate == 44100u ? 0x0bu : 0x09u);
    if (rc != HELIKON_OK)
        return rc;
    S5L_I2SCLKDIV = AUDIO_MCLK_HZ / sample_rate;
    audio->sample_rate = sample_rate;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_init(struct helikon_audio *audio, struct helikon_pmu *pmu,
                       struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (audio == NULL || pmu == NULL || clock == NULL || !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    audio->pmu = pmu;
    audio->clock = clock;
    audio->sample_rate = 44100u;
    audio->timeout_ms = timeout_ms;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    audio->initialized = 0;
    audio->running = 0;

    int rc = dmac_clock_on(audio);
    if (rc != HELIKON_OK)
        return rc;

    rc = codec_init(audio);
    if (rc != HELIKON_OK)
        return rc;

    audio->stage = HELIKON_AUDIO_STAGE_I2S;
    audio->initialized = 1;
    S5L_PWRCON(1) &= ~(1u << 7);
    S5L_I2STXCON = 0xb100019u;
    S5L_I2SCLKCON = 1u;
    rc = helikon_audio_set_rate(audio, 44100u);
    if (rc != HELIKON_OK) {
        audio->initialized = 0;
        return rc;
    }
    S5L_I2STXCOM = 0x0au;
    S5L_I2SCLKCON = 0u;
    S5L_PWRCON(1) |= 1u << 7;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_play(struct helikon_audio *audio, const int16_t *pcm,
                       size_t frames)
{
    if (audio == NULL || !audio->initialized || pcm == NULL) {
        if (audio != NULL)
            audio->stage = HELIKON_AUDIO_STAGE_BUFFER;
        return HELIKON_ERR_ARGUMENT;
    }

    if (!audio->running) {
        audio->stage = HELIKON_AUDIO_STAGE_CODEC;
        int rc = codec_mute(audio, 0);
        if (rc != HELIKON_OK)
            return rc;
        audio->stage = HELIKON_AUDIO_STAGE_I2S;
        rc = i2s_start();
        if (rc != HELIKON_OK)
            return rc;
        audio->running = 1;
    }

    while (frames != 0u) {
        size_t chunk = frames > AUDIO_DMA_FRAMES ? AUDIO_DMA_FRAMES : frames;
        int rc = dma_chunk(audio, pcm, chunk);
        if (rc != HELIKON_OK)
            return rc;
        pcm += chunk * 2u;
        frames -= chunk;
    }
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_pause(struct helikon_audio *audio)
{
    if (audio == NULL || !audio->initialized)
        return HELIKON_ERR_ARGUMENT;
    if (audio->running) {
        audio->stage = HELIKON_AUDIO_STAGE_CODEC;
        int rc = codec_mute(audio, 1);
        S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) = 0u;
        audio->stage = HELIKON_AUDIO_STAGE_I2S;
        i2s_stop();
        audio->running = 0;
        if (rc != HELIKON_OK) {
            audio->stage = HELIKON_AUDIO_STAGE_CODEC;
            return rc;
        }
    }
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_resume(struct helikon_audio *audio)
{
    if (audio == NULL || !audio->initialized)
        return HELIKON_ERR_ARGUMENT;
    if (!audio->running) {
        audio->stage = HELIKON_AUDIO_STAGE_CODEC;
        int rc = codec_mute(audio, 0);
        if (rc != HELIKON_OK)
            return rc;
        audio->stage = HELIKON_AUDIO_STAGE_I2S;
        rc = i2s_start();
        if (rc != HELIKON_OK)
            return rc;
        audio->running = 1;
    }
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_stop(struct helikon_audio *audio)
{
    if (audio == NULL || !audio->initialized)
        return HELIKON_ERR_ARGUMENT;
    audio->stage = HELIKON_AUDIO_STAGE_CODEC;
    int rc = codec_mute(audio, 1);
    S5L_DMAC_CH_CONFIG(AUDIO_DMA_CHANNEL) = 0u;
    audio->stage = HELIKON_AUDIO_STAGE_I2S;
    i2s_stop();
    audio->running = 0;
    if (rc != HELIKON_OK) {
        audio->stage = HELIKON_AUDIO_STAGE_CODEC;
        return rc;
    }
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}
