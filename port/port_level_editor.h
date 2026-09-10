#ifndef PORT_LEVEL_EDITOR_H
#define PORT_LEVEL_EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool Port_LevelEditor_IsOpen(void);
void Port_LevelEditor_Toggle(void);
bool Port_LevelEditor_HandleKey(int sdlKey, int sdlScancode);
void Port_LevelEditor_HandleMouseButton(int button, int state, float x, float y);
void Port_LevelEditor_HandleMouseMotion(float x, float y);
void Port_LevelEditor_Render(void* renderer, int winW, int winH);
void Port_LevelEditor_OnRoomLoad(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_LEVEL_EDITOR_H */
