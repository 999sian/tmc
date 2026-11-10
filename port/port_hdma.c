#include "port_hdma.h"

#include <stddef.h>
#include <string.h>

#define HDMA_CHANNELS 4

#define DMA_CNT_DEST_FIXED 0x0040
#define DMA_CNT_DEST_RELOAD 0x0060
#define DMA_CNT_DEST_MASK 0x0060
#define DMA_CNT_SRC_FIXED 0x0100
#define DMA_CNT_32BIT 0x0400

/* Three GBA destination-increment modes per scanline:
 *   FIXED:  no increment during transfer, no reload between transfers
 *   INC:    increment during transfer, no reload between transfers
 *   RELOAD: increment during transfer, *do* reload between transfers
 * The previous implementation conflated FIXED and RELOAD, which made the
 * 8-u16 affine-matrix HBlank-DMA used by the rolling barrel and similar
 * scenes write all 8 values to BG2PA, leaving BG2PB..Y_H untouched. */
typedef enum {
    DEST_INC = 0,
    DEST_FIXED,
    DEST_RELOAD,
} HdmaDestMode;

typedef struct {
    int active;
    const uint8_t* src_orig;
    const uint8_t* src;
    uint8_t* dest_orig;
    uint8_t* dest;
    uint16_t count; // units per HBlank transfer
    uint8_t unit;   // 2 or 4 bytes
    uint8_t src_fixed;
    uint8_t dest_mode; // HdmaDestMode
} HdmaChannel;

static HdmaChannel s_channels[HDMA_CHANNELS];

extern uint8_t gIoMem[0x400];

typedef struct {
    const uint8_t* table;
    uint32_t stamp;
    uint16_t packed[PORT_WS_WIN_LINES];
    uint16_t left[PORT_WS_WIN_LINES];
    uint16_t right[PORT_WS_WIN_LINES];
    uint8_t set[PORT_WS_WIN_LINES];
} WsWindowTable;

/* SetVBlankDMA double-buffers the window table, so two slots suffice. */
static WsWindowTable s_ws_tables[2];
static uint32_t s_ws_stamp;

static WsWindowTable* ws_window_find(const void* table) {
    for (int i = 0; i < 2; ++i) {
        if (s_ws_tables[i].table == (const uint8_t*)table) {
            return &s_ws_tables[i];
        }
    }
    return NULL;
}

void port_ws_window_table_begin(const void* table) {
    WsWindowTable* t = ws_window_find(table);
    if (t == NULL) {
        t = (s_ws_tables[0].stamp <= s_ws_tables[1].stamp) ? &s_ws_tables[0] : &s_ws_tables[1];
        t->table = (const uint8_t*)table;
    }
    t->stamp = ++s_ws_stamp;
    memset(t->set, 0, sizeof(t->set));
}

static uint16_t ws_clamp_edge(int v) {
    return (uint16_t)(v < 0 ? 0 : (v > 0x3ff ? 0x3ff : v));
}

void port_ws_window_table_set(const void* table, int entry, int left, int right) {
    WsWindowTable* t = ws_window_find(table);
    if (t == NULL || entry < 0 || entry >= PORT_WS_WIN_LINES) {
        return;
    }
    /* Remember the packed entry the game wrote; a later rewrite of the
     * table (another effect, MemClear) no longer matches and is ignored. */
    t->packed[entry] = (uint16_t)(t->table[entry * 2] | (t->table[entry * 2 + 1] << 8));
    t->left[entry] = ws_clamp_edge(left);
    t->right[entry] = ws_clamp_edge(right);
    t->set[entry] = 1;
}

static void ws_io_write16(int off, uint16_t v) {
    gIoMem[off] = (uint8_t)v;
    gIoMem[off + 1] = (uint8_t)(v >> 8);
}

typedef struct {
    uint16_t packed, left, right;
    uint8_t valid;
} WsWindowStatic;

static WsWindowStatic s_ws_pending[2], s_ws_latched[2];

void port_ws_window_static(int win, uint16_t packed, int left, int right) {
    if (win < 0 || win > 1) {
        return;
    }
    s_ws_pending[win] = (WsWindowStatic){ packed, ws_clamp_edge(left), ws_clamp_edge(right), 1 };
}

void port_ws_window_latch(uint16_t win0h, uint16_t win1h) {
    const uint16_t live[2] = { win0h, win1h };
    for (int win = 0; win < 2; ++win) {
        if (!s_ws_pending[win].valid || s_ws_pending[win].packed != live[win]) {
            s_ws_pending[win].valid = 0;
        }
        s_ws_latched[win] = s_ws_pending[win];
    }
}

/* Start each presented frame from the latched static windows; HBlank DMA
 * writes to WINxH then republish or withdraw per line. */
void port_ws_window_frame_begin(void) {
    uint16_t flags = 0;
    memset(&gIoMem[PORT_WS_WIN_IO], 0, 10);
    for (int win = 0; win < 2; ++win) {
        if (s_ws_latched[win].valid) {
            ws_io_write16(PORT_WS_WIN_IO + win * 4, s_ws_latched[win].left);
            ws_io_write16(PORT_WS_WIN_IO + win * 4 + 2, s_ws_latched[win].right);
            flags |= (uint16_t)(1u << win);
        }
    }
    ws_io_write16(PORT_WS_WIN_IO + 8, flags);
}

/* Publish (or withdraw) the full edges for the WINxH value just written. */
static void ws_window_on_write(const HdmaChannel* c, const uint8_t* unit_src) {
    int win;
    if (c->dest_orig == &gIoMem[0x40]) {
        win = 0;
    } else if (c->dest_orig == &gIoMem[0x42]) {
        win = 1;
    } else {
        return;
    }
    uint16_t flags = (uint16_t)(gIoMem[PORT_WS_WIN_IO + 8] | (gIoMem[PORT_WS_WIN_IO + 9] << 8));
    const WsWindowTable* t = ws_window_find(c->src_orig);
    ptrdiff_t entry = (unit_src - c->src_orig) / 2;
    uint16_t written = (uint16_t)(unit_src[0] | (unit_src[1] << 8));
    if (t != NULL && entry >= 0 && entry < PORT_WS_WIN_LINES && t->set[entry] && t->packed[entry] == written) {
        ws_io_write16(PORT_WS_WIN_IO + win * 4, t->left[entry]);
        ws_io_write16(PORT_WS_WIN_IO + win * 4 + 2, t->right[entry]);
        flags |= (uint16_t)(1u << win);
    } else {
        flags &= (uint16_t)~(1u << win);
    }
    ws_io_write16(PORT_WS_WIN_IO + 8, flags);
}

void port_hdma_register(int channel, const void* src, void* dest, uint16_t cnt_h, uint16_t count) {
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
    if (dm == DMA_CNT_DEST_FIXED) {
        c->dest_mode = DEST_FIXED;
    } else if (dm == DMA_CNT_DEST_RELOAD) {
        c->dest_mode = DEST_RELOAD;
    } else {
        c->dest_mode = DEST_INC;
    }
}

void port_hdma_unregister(int channel) {
    if (channel < 0 || channel >= HDMA_CHANNELS) {
        return;
    }
    s_channels[channel].active = 0;
}

int port_hdma_has_active_channels(void) {
    int ch;

    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        if (s_channels[ch].active) {
            return 1;
        }
    }
    return 0;
}

int port_hdma_dest_overlaps(const void* lo, const void* hi) {
    int ch;

    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        const HdmaChannel* c = &s_channels[ch];
        const uint8_t* d_lo;
        const uint8_t* d_hi;
        size_t span;

        if (!c->active) {
            continue;
        }
        /* Per-line write footprint starting at dest_orig. FIXED writes one
         * unit in place; INC/RELOAD advance across `count` units. INC also
         * drifts across lines, but TMC's affine users are RELOAD/FIXED —
         * treat the first-line footprint as the footprint. */
        span = (size_t)(c->dest_mode == DEST_FIXED ? c->unit : c->unit * c->count);
        d_lo = c->dest_orig;
        d_hi = c->dest_orig + span;
        if (d_lo < (const uint8_t*)hi && d_hi > (const uint8_t*)lo) {
            return 1;
        }
    }
    return 0;
}

/*
 * Hardware fires HBlank DMA at the *end* of a scanline, so transfer k (made
 * during line k's HBlank) takes effect on line k+1: line N renders table
 * entry N-1. Line 0 renders entry 0 too, because PerformVBlankDMA
 * (src/interrupts.c, GBA path) copies the first transfer's units into the
 * registers by hand right after arming the DMA -- the "why is it copied
 * again?" loop is that seed. This callback runs before line N is drawn, so
 * line 0 performs the seed (transfer entry 0, then rewind) and lines 1..159
 * perform transfers 0..158; transfer 159 lands in VBlank and is invisible.
 */
void port_hdma_step_line(int line) {
    int ch;

    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        HdmaChannel* c = &s_channels[ch];
        uint8_t* d;
        uint16_t i;

        if (!c->active) {
            continue;
        }
        d = c->dest;
        const uint8_t* first_src = c->src;
        for (i = 0; i < c->count; ++i) {
            memcpy(d, c->src, c->unit);
            if (!c->src_fixed) {
                c->src += c->unit;
            }
            /* DEST_FIXED never advances within a transfer; INC and RELOAD do. */
            if (c->dest_mode != DEST_FIXED) {
                d += c->unit;
            }
        }
        /* Between scanlines: RELOAD rewinds to dest_orig; INC keeps the
         * advanced pointer; FIXED stayed put anyway. */
        c->dest = (c->dest_mode == DEST_RELOAD) ? c->dest_orig : d;
        if (c->unit == 2 && c->count == 1) {
            ws_window_on_write(c, first_src);
        }
        if (line == 0) {
            c->src = c->src_orig;
            c->dest = c->dest_orig;
        }
    }
}

void port_hdma_vblank_reset(void) {
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
