#ifndef PORT_ROM_PICKER_H
#define PORT_ROM_PICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the native file picker, validates the selected ROM, and installs
 * it as baserom.gba next to the executable (in the data dir for
 * Picori.app). Returns 0 on success. */
int Port_RomPicker_PromptAndInstall(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROM_PICKER_H */
