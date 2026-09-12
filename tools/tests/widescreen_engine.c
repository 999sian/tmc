/* Small fixture for the unchanged production helper bodies extracted by the runner. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
typedef int16_t s16;
typedef int32_t s32;
typedef uint16_t u16;
typedef uint32_t u32;
#define MODE1_GBA_BG_CLIP_X 240
#define MODE1_WS_SHADOW_COLS 32
#define MODE1_WS_SHADOW_ROWS 32
static struct { int scroll_x, scroll_y, origin_x, origin_y, width, height, scrollAction; } gRoomControls;
static struct { unsigned active, xPos, yPos, width, height; } gCurrentWindow;
static int virtuappu_mode1_ws_shadow_base_tile[4];
static u16* virtuappu_mode1_ws_shadow[4];
static u32 sWsContentKey;
static int sWsContentPx;
static int enabled = 1;
static int Port_Config_WidescreenEnabled(void) { return enabled; }
static int Port_Widescreen_TargetViewWidth(void) { return 384; }
static int Port_WidescreenEffectiveTarget(void) { return gRoomControls.width < 384 ? gRoomControls.width : 384; }
static u32 Port_WidescreenRoomKey(void) { return 1; }
int Port_Widescreen_FallbackNative(void);
static int Port_Widescreen_EffectiveViewWidth(void) { return Port_Widescreen_FallbackNative() ? 240 : Port_WidescreenEffectiveTarget(); }
#include "widescreen_helpers.h"
static int errors;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); ++errors; } } while (0)
int main(void) {
    gRoomControls.width = 1024;
    gRoomControls.height = 1024;
    static u16 map[128*128], shadow[MODE1_WS_SHADOW_COLS*32];
    for (int i=0; i<128*128; ++i) map[i] = (u16)(i+1);
    gRoomControls.scroll_y = 16;
    for (int phase=0; phase<16; ++phase) {
        gRoomControls.scroll_x = 64 + phase;
        Port_WidescreenShadow_Populate(1, map, shadow);
        for (int shake=-3; shake<=3; ++shake) {
            for (int x=240; x<384; ++x) {
                int col = ((x+phase+shake)&255)/8;
                int index = (col-virtuappu_mode1_ws_shadow_base_tile[1]+32)%32;
                int expectedCol = (64+phase+shake+x)/8;
                if (index >= MODE1_WS_SHADOW_COLS || shadow[index] != map[128+expectedCol]) {
                    fprintf(stderr,"FAIL shadow phase=%d shake=%d x=%d index=%d\n",phase,shake,x,index);
                    ++errors; break;
                }
            }
        }
    }
    for (int action=0; action<=5; ++action) {
        gRoomControls.scrollAction = action;
        CHECK(Port_Widescreen_EffectiveViewWidth() == ((action==2 || action==4 || action==5) ? 240 : 384));
        CHECK(Port_Widescreen_CameraRestX(1000) == ((action==2 || action==4 || action==5) ? 784 : 640));
    }
    /* Room-edge padding stays transparent even when the backing map is stale. */
    gRoomControls.scroll_x = 0;
    gRoomControls.width = 256;
    Port_WidescreenShadow_Populate(1, map, shadow);
    CHECK(shadow[2] == map[128+31]);
    CHECK(shadow[3] == 0);
    gRoomControls.scrollAction = 1;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 256);
    enabled = 0;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 240);
    enabled = 1;
    gRoomControls.width = 240;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 240);
    /* Overlay is a complete wrapping screenblock, independent of camera. */
    static u16 screen[32 * 32];
    for (int i = 0; i < 32 * 32; ++i) screen[i] = (u16)(i + 7);
    Port_WidescreenShadow_PopulateOverlay(screen, shadow);
    CHECK(virtuappu_mode1_ws_shadow[3] == shadow);
    CHECK(virtuappu_mode1_ws_shadow_base_tile[3] == 0);
    for (int row = 0; row < 32; ++row)
        for (int col = 0; col < MODE1_WS_SHADOW_COLS; ++col)
            CHECK(shadow[row * MODE1_WS_SHADOW_COLS + col] == screen[row * 32 + (col & 31)]);
    int x,y,w,h;
    gCurrentWindow.active=0;
    CHECK(!Message_GetWindowRect(&x,&y,&w,&h));
    gCurrentWindow.active=1; gCurrentWindow.xPos=5; gCurrentWindow.yPos=1;
    gCurrentWindow.width=18; gCurrentWindow.height=2;
    CHECK(Message_GetWindowRect(&x,&y,&w,&h));
    CHECK(x==40 && y==8 && w==160 && h==32);
    /* Closing animation uses the smaller currently drawn frame. */
    gCurrentWindow.xPos=13; gCurrentWindow.yPos=3;
    gCurrentWindow.width=2; gCurrentWindow.height=0;
    CHECK(Message_GetWindowRect(&x,&y,&w,&h));
    CHECK(x==104 && y==24 && w==32 && h==16);
    gCurrentWindow.active=0;
    CHECK(!Message_GetWindowRect(&x,&y,&w,&h));
    if (!errors) puts("widescreen engine: shake continuity, iris width/camera, live message window passed");
    return errors != 0;
}
