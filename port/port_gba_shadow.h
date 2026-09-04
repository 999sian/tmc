#ifndef PORT_GBA_SHADOW_H
#define PORT_GBA_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GBA-address shadow of the engine's native state.
 *
 * This is a decompilation port, not an emulator: there is no live GBA memory
 * image. The engine's state lives in native C globals (`SaveFile gSave;` and
 * friends), so reading 0x02002A40 out of gEwram returns nothing.
 *
 * RetroAchievements sets for AGB are authored against a real GBA dump and
 * RA's GBA memory map is flat (0x000000-0x007FFF -> 0x03000000 IWRAM,
 * 0x008000-0x047FFF -> 0x02000000 EWRAM). So achievement conditions have to
 * be served from a shadow buffer built out of the native globals, placed at
 * the GBA addresses recorded in linker.ld. That is what makes a condition
 * like "0x02002A90 & 0x04" evaluate correctly against a native port.
 *
 * Only byte ranges whose native layout is provably identical to retail are
 * shadowed; see the table and the rejected list in port_gba_shadow.c.
 * Everything else reads as zero.
 */

/* Refresh the shadow of the engine's native state, addressed by GBA address.
 * Call once per frame before consumers read. */
void Port_GbaShadow_Refresh(void);

/* Read n bytes at a GBA address (0x02xxxxxx / 0x03xxxxxx). Returns the number
 * of bytes actually served (0 when the range is not shadowed). */
uint32_t Port_GbaShadow_Read(uint32_t gba_addr, uint8_t* out, uint32_t n);

/* Diagnostics for the UI/F8: how many symbols are shadowed and how many were
 * rejected by the size gate. `bytes` is the per-frame copied byte count. */
void Port_GbaShadow_GetStats(int* shadowed, int* rejected, uint32_t* bytes);

/* Rejected symbols, for diagnostics. index < the `rejected` count above. */
const char* Port_GbaShadow_GetRejected(int index, const char** reason);

/* Round-trips a sentinel through gSave to prove the address arithmetic, and
 * checks the region-boundary behaviour of Port_GbaShadow_Read. Restores the
 * bytes it touched. Returns true on success, logs to stderr on failure.
 * Runs automatically on the first Refresh when PORT_GBA_SHADOW_SELFTEST is
 * defined; otherwise call it yourself. */
bool Port_GbaShadow_SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GBA_SHADOW_H */
