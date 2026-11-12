#ifndef PORT_HDMA_H
#define PORT_HDMA_H

/*
 * Software simulation of GBA HBlank-triggered DMA channels.
 *
 * On hardware, a DMA configured with DMA_START_HBLANK | DMA_REPEAT transfers
 * `count` units from src to dest at every HBlank. TMC uses this for the iris
 * circle / window effects (per-scanline WIN0H).
 *
 * The host PPU renders frames as a single batch, so we drive HDMA from the
 * VirtuaPPU mode-1 pre-line callback: one transfer per scanline.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void port_hdma_register(int channel, const void* src, void* dest, uint16_t cnt_h, uint16_t count);
int port_hdma_has_active_channels(void);
/* 1 if any ACTIVE channel's per-line destination range overlaps [lo, hi).
 * Used by the PPU glue to strobe the affine BG2X/BG2Y reference latch: a
 * write EVENT must reload the internal reference even when the written
 * value is unchanged (constant-value HDMA pins the layer on hardware). */
int port_hdma_dest_overlaps(const void* lo, const void* hi);
void port_hdma_unregister(int channel);
void port_hdma_step_line(int line);
void port_hdma_vblank_reset(void);

/* Widescreen window side channel. WINxH holds 8-bit edges, so a window table
 * built for a frame wider than 240 records each entry's full edges here too.
 * When HBlank DMA writes an entry into WIN0H/WIN1H unchanged, the full edges
 * land in unused I/O so both PPU rasterizers see them per line:
 *   0x56 WIN0 left, 0x58 WIN0 right, 0x5A WIN1 left, 0x5C WIN1 right,
 *   0x5E flags (bit 0 WIN0 valid, bit 1 WIN1 valid). */
#define PORT_WS_WIN_IO 0x56
#define PORT_WS_WIN_LINES 160
void port_ws_window_table_begin(const void* table);
void port_ws_window_table_set(const void* table, int entry, int left, int right);
void port_ws_window_frame_begin(void);
/* Static windows: record the full edges for the WINxH value the game just
 * stored; port_ws_window_latch (each vblank) keeps the record while that
 * value is still the live one. */
void port_ws_window_static(int win, uint16_t packed, int left, int right);
void port_ws_window_latch(uint16_t win0h, uint16_t win1h);

#ifdef __cplusplus
}
#endif

#endif
