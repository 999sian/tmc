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

void port_hdma_register(int channel, const void* src, void* dest,
                        uint16_t cnt_h, uint16_t count);
void port_hdma_unregister(int channel);
void port_hdma_step_line(int line);
void port_hdma_vblank_reset(void);

#ifdef __cplusplus
}
#endif

#endif
