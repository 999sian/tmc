#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Draw inside an active ImGui frame. Frame is native-resolution ABGR8888;
 * viewport uses ImGui logical coordinates and must match this frame exactly. */
void Port_HDText_Draw(const uint32_t* frame, int frameWidth, int frameHeight, int pitchPixels,
                      float viewportX, float viewportY, float viewportWidth, float viewportHeight);
#ifdef __cplusplus
}
#endif
