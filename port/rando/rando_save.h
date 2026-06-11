#ifndef PORT_RANDO_SAVE_H
#define PORT_RANDO_SAVE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Port_RandoSave_SaveActiveSlot(int slot);
bool Port_RandoSave_LoadSlot(int slot);
void Port_RandoSave_ClearSlot(int slot);
void Port_RandoSave_CopySlot(int src, int dst);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_SAVE_H */
