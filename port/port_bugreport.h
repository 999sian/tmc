#ifndef PORT_BUGREPORT_H
#define PORT_BUGREPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Captures a bug-report bundle next to the binary:
 *   ./bugreport_<timestamp>/screenshot.bmp   (240x160 GBA framebuffer)
 *   ./bugreport_<timestamp>/save.bin         (current save EEPROM dump)
 *   ./bugreport_<timestamp>/state.txt        (area/room/coords/version)
 * Returns the directory name on success (caller must free), NULL on failure.
 * Triggered by F9 in port_bios.c. */
char* Port_BugReport_Capture(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BUGREPORT_H */
