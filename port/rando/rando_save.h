#ifndef PORT_RANDO_SAVE_H
#define PORT_RANDO_SAVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortRandoSaveLoadStatus {
    PORT_RANDO_SAVE_NONE = 0,
    PORT_RANDO_SAVE_LOADED,
    PORT_RANDO_SAVE_INCOMPATIBLE,
} PortRandoSaveLoadStatus;

bool Port_RandoSave_SaveActiveSlot(int slot);
bool Port_RandoSave_LoadSlot(int slot);
PortRandoSaveLoadStatus Port_RandoSave_LastLoadStatus(void);
bool Port_RandoSave_LoadedLegacySlot(void);
uint64_t Port_RandoSave_ActiveBindingHash(void);
void Port_RandoSave_ClearSlot(int slot);
void Port_RandoSave_CopySlot(int src, int dst);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_SAVE_H */
