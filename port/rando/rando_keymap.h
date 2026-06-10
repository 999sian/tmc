#ifndef PORT_RANDO_KEYMAP_H
#define PORT_RANDO_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bind curated native runtime keys (area<<16 | room<<8 | groundItemFlag) onto
 * the .logic ground-item locations that only carry MinishMaker EU-ROM patch
 * addresses. Idempotent per parse: RandoLogic_BindRuntimeKey fills empty keys
 * only, and a reparse clears them, so this runs after every successful
 * parse+activate. Locations not in the table keep the global-bijection
 * fallback. */
void Rando_Keymap_Apply(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_KEYMAP_H */
