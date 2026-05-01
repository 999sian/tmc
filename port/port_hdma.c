#include "port_hdma.h"

#include <string.h>

#define HDMA_CHANNELS 4

#define DMA_CNT_DEST_FIXED  0x0040
#define DMA_CNT_DEST_RELOAD 0x0060
#define DMA_CNT_DEST_MASK   0x0060
#define DMA_CNT_SRC_FIXED   0x0100
#define DMA_CNT_32BIT       0x0400

typedef struct {
    int active;
    const uint8_t* src_orig;
    const uint8_t* src;
    uint8_t* dest_orig;
    uint8_t* dest;
    uint16_t count;     // units per HBlank transfer
    uint8_t  unit;      // 2 or 4 bytes
    uint8_t  src_fixed;
    uint8_t  dest_reload; // 0 = increment, 1 = reset to dest_orig after each HBlank
} HdmaChannel;

static HdmaChannel s_channels[HDMA_CHANNELS];

void port_hdma_register(int channel, const void* src, void* dest,
                        uint16_t cnt_h, uint16_t count)
{
    HdmaChannel* c;
    uint16_t dm;

    if (channel < 0 || channel >= HDMA_CHANNELS) {
        return;
    }
    c = &s_channels[channel];
    c->active = 1;
    c->src_orig = c->src = (const uint8_t*)src;
    c->dest_orig = c->dest = (uint8_t*)dest;
    c->count = count ? count : 1;
    c->unit = (cnt_h & DMA_CNT_32BIT) ? 4 : 2;
    c->src_fixed = (cnt_h & DMA_CNT_SRC_FIXED) ? 1 : 0;
    dm = cnt_h & DMA_CNT_DEST_MASK;
    c->dest_reload = (dm == DMA_CNT_DEST_FIXED || dm == DMA_CNT_DEST_RELOAD) ? 1 : 0;
}

void port_hdma_unregister(int channel)
{
    if (channel < 0 || channel >= HDMA_CHANNELS) {
        return;
    }
    s_channels[channel].active = 0;
}

void port_hdma_step_line(int line)
{
    int ch;

    (void)line;
    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        HdmaChannel* c = &s_channels[ch];
        uint8_t* d;
        uint16_t i;

        if (!c->active) {
            continue;
        }
        d = c->dest;
        for (i = 0; i < c->count; ++i) {
            memcpy(d, c->src, c->unit);
            if (!c->src_fixed) {
                c->src += c->unit;
            }
            if (!c->dest_reload) {
                d += c->unit;
            }
        }
        c->dest = c->dest_reload ? c->dest_orig : d;
    }
}

void port_hdma_vblank_reset(void)
{
    int ch;

    /*
     * TMC re-arms its HBlank DMA via SetVBlankDMA each frame, so registers
     * are typically refreshed during VBlank. If a channel happens to outlive
     * the frame, rewind src/dest so the same per-scanline table replays.
     */
    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        HdmaChannel* c = &s_channels[ch];
        if (!c->active) {
            continue;
        }
        c->src = c->src_orig;
        c->dest = c->dest_orig;
    }
}
