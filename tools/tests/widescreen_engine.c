/* Small fixture for the unchanged production helper bodies extracted by the runner. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
typedef int16_t s16;
typedef int32_t s32;
typedef uint16_t u16;
typedef uint32_t u32;
#define MODE1_GBA_BG_CLIP_X 240
#define MODE1_WS_SHADOW_COLS 32
#define MODE1_WS_SHADOW_ROWS 32
typedef struct { int action; struct { s32 WORD; } x, y; } Entity;
typedef struct { Entity base; } PlayerEntity;
static PlayerEntity gPlayerEntity;
static unsigned char gUpdateVisibleTiles;
#define Q_16_16(n) ((s32)((n) * 65536))
typedef struct { int scroll_x, scroll_y, origin_x, origin_y, width, height, scrollAction, area, room;
                 int scroll_flags, scroll_direction, unk_18; Entity* camera_target; } RoomControls;
static RoomControls gRoomControls;
static struct { unsigned active, xPos, yPos, width, height; } gCurrentWindow;
static int virtuappu_mode1_ws_shadow_base_tile[4];
static u16* virtuappu_mode1_ws_shadow[4];
static u32 sWsContentKey;
static int sWsContentPx;
static int sWsLatchedFrameWidth = 240;
static int enabled = 1;
static int Port_Config_WidescreenEnabled(void) { return enabled; }
static int Port_Widescreen_TargetViewWidth(void) { return 384; }
static int Port_WidescreenEffectiveTarget(void) { return gRoomControls.width < 384 ? gRoomControls.width : 384; }
static u32 Port_WidescreenRoomKey(void) { return 1; }
int Port_Widescreen_FallbackNative(void);
/* Only unrelated task/HUD dependencies are stubbed; shadow publication runs
 * the production UpdateShadows body, including its area and BG gates. */
enum { TASK_GAME = 2, TASK_GAMEOVER = 3, AREA_MINISH_WOODS = 0x0b, AREA_HYRULE_FIELD = 0x0a,
       AREA_CAVE_OF_FLAMES = 0x60, MANAGER = 9, ENTER_ROOM_TEXTBOX_MANAGER = 1 };
#define MODE1_GBA_BG_COUNT 4
#define DISPCNT_BG0_ON 0x100
#define DISPCNT_BG1_ON 0x200
#define DISPCNT_BG2_ON 0x400
#define DISPCNT_BG3_ON 0x800
#define DISPCNT_FORCED_BLANK 0x80
static struct { int active; } gFadeControl;
static struct { int task; int state; } gMain;
typedef struct { u32 control; int yOffset; void* subTileMap; } TestBg;
static struct { TestBg bg0, bg1, bg2, bg3; struct { int displayControl; } lcd;
                struct { u16 window0HorizontalDimensions, window1HorizontalDimensions; } controls; } gScreen;
static void port_ws_window_latch(u16 w0, u16 w1) { (void)w0; (void)w1; }
static struct { TestBg* bgSettings; } gMapBottom, gMapTop;
static u16 gMapDataBottomSpecial[0x4000], gMapDataTopSpecial[0x4000];
static u16 sWsShadowBG1[32*32], sWsShadowBG2[32*32], sWsShadowOverlay[32*32];
static u16 sWsShadowSource[4][32*32];
static u16 vram[0x18000/2];
#define gVram ((unsigned char*)vram)
static int virtuappu_mode1_ws_hud_right_anchor, virtuappu_mode1_ws_msg_shift;
static int virtuappu_mode1_ws_msg_x0, virtuappu_mode1_ws_msg_x1;
static int virtuappu_mode1_ws_msg_y0, virtuappu_mode1_ws_msg_y1;
static __attribute__((unused)) int Port_WidescreenScanContentPx(void) { return 1024; }
static int Port_Widescreen_IsActive(void) { return gMain.task == TASK_GAME && !Port_Widescreen_FallbackNative(); }
static int Port_Widescreen_HudRightAnchor(void) { return 0; }
static void Port_Widescreen_RescanRoomContent(void) {}
static Entity* FindEntityByID(int kind, int id, int list) { (void)kind; (void)id; (void)list; return NULL; }
int Port_Widescreen_RollWidthAt(int fromW, int rollW, int toW, int k, int frames);
int Port_Widescreen_RollCameraK(int k, int frames, int hold);
#include "widescreen_helpers.h"
static int errors;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); ++errors; } } while (0)
int main(void) {
    gMain.task = TASK_GAME;
    gRoomControls.width = 1024;
    gRoomControls.height = 1024;
    static u16 map[128*128], shadow[MODE1_WS_SHADOW_COLS*32];
    for (int i=0; i<128*128; ++i) map[i] = (u16)(i+1);
    gRoomControls.scroll_y = 16;
    for (int phase=0; phase<16; ++phase) {
        gRoomControls.scroll_x = 64 + phase;
        Port_WidescreenShadow_Populate(1, map, NULL, shadow);
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
    /* At the top edge the native fill duplicates map row zero into its
     * leading padding row. Upward shake exposes that row at the seam. */
    gRoomControls.scroll_x = 0;
    for (int y = 0; y < 8; ++y) {
        gRoomControls.scroll_y = y;
        Port_WidescreenShadow_Populate(1, map, NULL, shadow);
        for (int shake = -3; shake < 0; ++shake) {
            int row = (8 + y + shake) / 8;
            CHECK(shadow[row * 32 + 1] == map[30]);
        }
    }
    gRoomControls.scroll_y = 16;
    /* Room-edge padding stays transparent even when the backing map is stale. */
    gRoomControls.scroll_x = 0;
    gRoomControls.width = 256;
    Port_WidescreenShadow_Populate(1, map, NULL, shadow);
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
    Port_WidescreenShadow_PopulateOverlay(3, screen, shadow);
    CHECK(virtuappu_mode1_ws_shadow[3] == shadow);
    CHECK(virtuappu_mode1_ws_shadow_base_tile[3] == 0);
    for (int row = 0; row < 32; ++row)
        for (int col = 0; col < MODE1_WS_SHADOW_COLS; ++col)
            CHECK(shadow[row * MODE1_WS_SHADOW_COLS + col] == screen[row * 32 + (col & 31)]);
    /* Any 256x256 BG3 canvas beside a live map publishes its own screenblock;
     * disabled, wide-sized, mapless and native cases must not. */
    memcpy(gVram + 0xf000, screen, sizeof(screen));
    memcpy(gVram + 0xe800, screen, sizeof(screen));
    gMain.task = TASK_GAME;
    gRoomControls.width = 1024;
    static TestBg overlayMap = { 0x1c00, 0, NULL };
    gScreen.bg1.control = 0x1c00;
    const struct { int area, control, enabled, wide, map, expected; } overlays[] = {
        { AREA_MINISH_WOODS, 0x1e04, 1, 1, 1, 1 },
        { AREA_HYRULE_FIELD, 0x1e05, 1, 1, 1, 1 },
        { 0x1a, 0x1e07, 1, 1, 1, 1 },      /* Veil Springs sky */
        { 0x35, 0x1d0c, 1, 1, 1, 1 },      /* darkness overlay, other screenblock */
        { 0x0b, 0x1e44, 1, 1, 1, 1 },      /* mosaic bit during a fade */
        { AREA_HYRULE_FIELD, 0x1e05, 0, 1, 1, 0 },
        { AREA_HYRULE_FIELD, 0x1e05, 1, 0, 1, 0 },
        { AREA_HYRULE_FIELD, 0x5e05, 1, 1, 1, 0 }, /* 512 wide renders natively */
        { AREA_HYRULE_FIELD, 0x9e05, 1, 1, 1, 0 }, /* 512 tall: no second block */
        { AREA_HYRULE_FIELD, 0x1e05, 1, 1, 0, 0 }, /* overlay screen, no map */
    };
    for (unsigned i = 0; i < sizeof(overlays)/sizeof(overlays[0]); ++i) {
        gRoomControls.area = overlays[i].area;
        gScreen.bg3.control = overlays[i].control;
        gScreen.lcd.displayControl = overlays[i].enabled ? DISPCNT_BG3_ON : 0;
        gMapBottom.bgSettings = overlays[i].map ? &overlayMap : NULL;
        enabled = overlays[i].wide;
        Port_Widescreen_UpdateShadows();
        CHECK((virtuappu_mode1_ws_shadow[3] != NULL) == overlays[i].expected);
        if (overlays[i].expected && virtuappu_mode1_ws_shadow[3])
            CHECK(memcmp(virtuappu_mode1_ws_shadow[3], screen, sizeof(screen)) == 0);
        if (!overlays[i].map)
            CHECK(Port_Widescreen_LatchedFrameWidth() == 240);
    }
    /* A BG1 texture (Mount Crenel rain) wraps only for the frame it opts in. */
    gRoomControls.area = AREA_HYRULE_FIELD;
    gScreen.bg3.control = 0x1e05;
    gMapBottom.bgSettings = &overlayMap;
    gScreen.bg1.control = 0x1d01;
    gScreen.bg2.control = 0x1c00; /* the live map, so shadows are on */
    gScreen.lcd.displayControl = (DISPCNT_BG0_ON << 1) | (DISPCNT_BG0_ON << 2);
    enabled = 1;
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] == NULL);
    Port_Widescreen_WrapBg(1);
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] != NULL);
    CHECK(virtuappu_mode1_ws_shadow_base_tile[1] == 0);
    if (virtuappu_mode1_ws_shadow[1])
        CHECK(memcmp(virtuappu_mode1_ws_shadow[1], screen, sizeof(screen)) == 0);
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] == NULL);
    gScreen.bg1.control = 0x1c00;
    gScreen.bg2.control = 0;
    /* A parallax BG copied out of a wider image continues that image from
     * tile column 30 of the copied window; past the buffer end it is empty. */
    static u16 image[32 * 128], window[32 * 32];
    for (int i = 0; i < 32 * 128; ++i) image[i] = (u16)(0x100 + i);
    const int start = 6;
    /* As the managers do: register, then copy the 32x32 window. */
#define COPY_WINDOW(from)                                                            \
    for (int at = 0; at < 32 * 32; ++at)                                             \
        window[at] = (from) + (at / 32) * 128 + at % 32 < image + 32 * 128           \
                         ? (from)[(at / 32) * 128 + at % 32] : 0
    Port_Widescreen_BgWindowSource(window, image + start, image + 32 * 128);
    COPY_WINDOW(image + start);
    gScreen.bg1.subTileMap = window;
    gScreen.bg1.control = 0x1d47;
    gScreen.bg3.control = 0;
    gScreen.lcd.displayControl = DISPCNT_BG1_ON;
    static TestBg rafterMap = { 0x1c42, 0, NULL };
    gScreen.bg2.control = 0x1c42;
    gMapBottom.bgSettings = &rafterMap;
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] == sWsShadowSource[1]);
    CHECK(virtuappu_mode1_ws_shadow_base_tile[1] == 30);
    if (virtuappu_mode1_ws_shadow[1] == sWsShadowSource[1]) {
        for (int row = 0; row < 32; ++row)
            for (int col = 0; col < MODE1_WS_SHADOW_COLS; ++col)
                CHECK(sWsShadowSource[1][row * MODE1_WS_SHADOW_COLS + col] == image[row * 128 + start + 30 + col]);
    }
    Port_Widescreen_BgWindowSource(window, image + 32 * 128 - 40, image + 32 * 128);
    COPY_WINDOW(image + 32 * 128 - 40);
    Port_Widescreen_UpdateShadows();
    CHECK(sWsShadowSource[1][9] == image[32 * 128 - 1]);
    CHECK(sWsShadowSource[1][10] == 0);
#undef COPY_WINDOW
    /* Another BG reusing the same buffer: the record no longer describes
     * it, so no source shadow, even once the old contents return. */
    window[0] ^= 1;
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] != sWsShadowSource[1]);
    window[0] ^= 1;
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] != sWsShadowSource[1]);
    /* Another subTileMap (a plain map BG swapped in) gets no source shadow. */
    gScreen.bg1.subTileMap = image;
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[1] == NULL);
    gScreen.bg1.subTileMap = NULL;
    gScreen.bg2.control = 0;
    /* A map rendered as BG3 keeps its map shadow. */
    gScreen.bg3.control = 0x1c03;
    gScreen.bg1.control = 0x1b00;
    gScreen.lcd.displayControl = DISPCNT_BG3_ON;
    gMapBottom.bgSettings = &overlayMap;
    Port_Widescreen_UpdateShadows();
    CHECK(virtuappu_mode1_ws_shadow[3] == sWsShadowBG1);
    gMapBottom.bgSettings = NULL;
    enabled = 1;
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
    /* Present width is latched at vblank with the shadows it frames. */
    gMain.task = TASK_GAME;
    gRoomControls.width = 1024;
    gRoomControls.scrollAction = 1;
    gScreen.bg1.control = 0x1d00;
    static TestBg mapBg = { 0x1d00, 0, NULL };
    gMapBottom.bgSettings = &mapBg;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 384);
    enabled = 0;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 240);
    enabled = 1;
    gMapBottom.bgSettings = NULL;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 240); /* overlay screen: no map shadow */

    /* Room reloads blank the map BGs while the width is 0 or a fade runs:
     * the wide width holds across them instead of flashing 240 bars. */
    gMapBottom.bgSettings = &mapBg;
    gScreen.lcd.displayControl = DISPCNT_BG1_ON;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 384);
    gScreen.lcd.displayControl = DISPCNT_BG0_ON;
    gMapBottom.bgSettings = NULL;
    gRoomControls.width = 0;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 384);
    gRoomControls.width = 1024;
    gFadeControl.active = 1;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 384);
    gFadeControl.active = 0;
    gScreen.lcd.displayControl = DISPCNT_BG0_ON | DISPCNT_FORCED_BLANK;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 384);
    /* A steady BG0-only screen is a native overlay, not a reload. */
    gScreen.lcd.displayControl = DISPCNT_BG0_ON;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 240);
    /* Holding never widens a native frame, and a disabled option drops it. */
    gRoomControls.width = 0;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 240);
    gRoomControls.width = 1024;
    gMapBottom.bgSettings = &mapBg;
    gScreen.lcd.displayControl = DISPCNT_BG1_ON;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 384);
    gScreen.lcd.displayControl = DISPCNT_BG0_ON;
    gRoomControls.width = 0;
    enabled = 0;
    Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_LatchedFrameWidth() == 240);
    enabled = 1;
    gRoomControls.width = 1024;
    gMapBottom.bgSettings = NULL;

    /* Wide rolling transition to the right: the outgoing room (x 0..511)
     * stays visible beside the destination (x 512..1023) for the whole
     * scroll, and the camera lands on the destination's rest. */
    for (int i = 0; i < 0x4000; ++i) gMapDataBottomSpecial[i] = (u16)(0x1000 + (i & 0x7ff));
    gRoomControls = (RoomControls){ .scroll_x = 128, .scroll_y = 0, .origin_x = 0, .origin_y = 0,
                                    .width = 512, .height = 256, .scrollAction = 1, .scroll_direction = 1,
                                    .camera_target = &gPlayerEntity.base };
    gPlayerEntity.base.x.WORD = 500 << 16;
    gPlayerEntity.base.y.WORD = 100 << 16;
    Port_Widescreen_RollBegin();
    gRoomControls.scrollAction = 2;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 384);
    /* GameMain swaps in the destination after Scroll2Sub0. */
    for (int i = 0; i < 0x4000; ++i) gMapDataBottomSpecial[i] = (u16)(0x5000 + (i & 0x7ff));
    gRoomControls.origin_x = 512;
    Port_Widescreen_RollStart();
    int prev = gRoomControls.scroll_x;
    for (int k = 1; k < 0x3c; ++k) {
        gRoomControls.unk_18 = k;
        gPlayerEntity.base.x.WORD += Q_16_16(0.25);
        gRoomControls.scroll_x += 4;
        Port_Widescreen_RollStep();
        CHECK(gRoomControls.scroll_x >= prev && gRoomControls.scroll_x <= 512);
        CHECK(Port_Widescreen_EffectiveViewWidth() == 384);
        prev = gRoomControls.scroll_x;
        if (k == 30) {
            CHECK(gRoomControls.scroll_x == 320);
            static u16 buffer[32 * 32];
            Port_WidescreenRollComposeBuffer(gMapDataBottomSpecial, sWsRollBottom, buffer);
            /* Buffer row 1 = map row 0 (row 0 is the padding row above). */
            CHECK(buffer[32 + 0] == sWsRollBottom[40]);   /* x 320: outgoing room */
            CHECK(buffer[32 + 23] == sWsRollBottom[63]);  /* x 504: outgoing edge */
            CHECK(buffer[32 + 24] == gMapDataBottomSpecial[0]); /* x 512: destination */
            Port_WidescreenShadow_Populate(1, gMapDataBottomSpecial, sWsRollBottom, shadow);
            CHECK(shadow[32 + 1] == gMapDataBottomSpecial[6]); /* display x 240 = world 560 */
        }
    }
    CHECK(prev == 128 + 384 * 59 / 60);
    gRoomControls.scrollAction = 0;
    CHECK(Port_Widescreen_RollEnd() == 1);
    CHECK(Port_Widescreen_CameraRestX(gPlayerEntity.base.x.WORD >> 16) == 512);

    /* Downward: the reveal rows above the destination come from the room left. */
    for (int i = 0; i < 0x4000; ++i) gMapDataBottomSpecial[i] = (u16)(0x1000 + (i & 0x7ff));
    gRoomControls = (RoomControls){ .scroll_x = 0, .scroll_y = 96, .origin_x = 0, .origin_y = 0,
                                    .width = 512, .height = 256, .scrollAction = 1, .scroll_direction = 2,
                                    .camera_target = &gPlayerEntity.base };
    Port_Widescreen_RollBegin();
    gRoomControls.scrollAction = 2;
    for (int i = 0; i < 0x4000; ++i) gMapDataBottomSpecial[i] = (u16)(0x5000 + (i & 0x7ff));
    gRoomControls.origin_y = 256;
    gRoomControls.scroll_y = 176; /* 80px into the scroll */
    Port_WidescreenShadow_Populate(1, gMapDataBottomSpecial, sWsRollBottom, shadow);
    /* Shadow row sr = world tile row 2*floor(-80/16) - 1 + sr relative to y=256. */
    CHECK(shadow[1 * 32 + 1] == sWsRollBottom[(32 - 10) * 128 + 30]); /* old room, row 22 */
    CHECK(shadow[11 * 32 + 1] == gMapDataBottomSpecial[0 * 128 + 30]); /* new room, row 0 */
    gRoomControls.scrollAction = 0;
    Port_Widescreen_RollEnd();

    /* A transition that starts native stays native (no composite, no snap). */
    gRoomControls.width = 240;
    gRoomControls.scrollAction = 1;
    Port_Widescreen_RollBegin();
    gRoomControls.scrollAction = 2;
    gRoomControls.width = 1024;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 240);
    CHECK(!Port_WidescreenRollComposite());
    gRoomControls.scrollAction = 0;
    CHECK(Port_Widescreen_RollEnd() == 0);

    /* Castle throne room (272 wide) down into the hall (496 wide, further
     * left): while throne-room rows show, the view keeps to its columns
     * instead of panning toward the hall's rest into world outside both
     * rooms. The handoff keeps the view centre for Scroll1 to pan on. */
    gRoomControls = (RoomControls){ .scroll_x = 384, .scroll_y = 208, .origin_x = 384, .origin_y = 16,
                                    .width = 272, .height = 352, .scrollAction = 1, .scroll_direction = 2,
                                    .camera_target = &gPlayerEntity.base };
    gPlayerEntity.base.x.WORD = 408 << 16;
    gPlayerEntity.base.y.WORD = 362 << 16;
    Port_Widescreen_RollBegin();
    gRoomControls.scrollAction = 2;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 272);
    gRoomControls.origin_x = 272;
    gRoomControls.origin_y = 368;
    gRoomControls.width = 496;
    gRoomControls.height = 528;
    Port_Widescreen_RollStart();
    for (int k = 1; k < 0x28; ++k) {
        gRoomControls.unk_18 = k;
        gPlayerEntity.base.y.WORD += Q_16_16(0.375);
        gRoomControls.scroll_y += 4;
        Port_Widescreen_RollStep();
        /* Throne-room columns while the throne room is on screen: widening
         * early would show void beside it. */
        CHECK(Port_Widescreen_EffectiveViewWidth() == 272);
        CHECK(gRoomControls.scroll_x == 384);
    }
    gRoomControls.scrollAction = 0;
    CHECK(Port_Widescreen_RollEnd() == 1);
    gRoomControls.scroll_x = 272; /* sub_080809D4's rest snap */
    Port_Widescreen_RollHandoff();
    /* Then the hall's 384 over the next frames, not in one pop. */
    {
        int prev = Port_Widescreen_EffectiveViewWidth();
        CHECK(prev > 272 && prev < 290);
        CHECK(gRoomControls.scroll_x + prev / 2 == 384 + 272 / 2);
        /* A map BG on screen, so the frame width latches. */
        gScreen.bg3.control = 0x1c03;
        gScreen.lcd.displayControl = DISPCNT_BG3_ON;
        gMapBottom.bgSettings = &overlayMap;
        for (int k = 0; k < 12; ++k) {
            Port_Widescreen_UpdateShadows();
            /* The frame presents the width its camera was laid out for. */
            CHECK(Port_Widescreen_LatchedFrameWidth() == prev);
            const int w = Port_Widescreen_EffectiveViewWidth();
            CHECK(w >= prev && w - prev <= 12);
            CHECK(gRoomControls.scroll_x >= gRoomControls.origin_x &&
                  gRoomControls.scroll_x + w <= gRoomControls.origin_x + (int)gRoomControls.width);
            prev = w;
        }
        CHECK(prev == 384);
    }

    /* Back up: the hall's 384 view would flank the 272 throne room with void
     * for the whole roll, so the roll eases down to 272 over its first
     * frames, centred and inside both rooms. */
    gRoomControls = (RoomControls){ .scroll_x = 272, .scroll_y = 368, .origin_x = 272, .origin_y = 368,
                                    .width = 496, .height = 528, .scrollAction = 1, .scroll_direction = 0,
                                    .camera_target = &gPlayerEntity.base };
    gPlayerEntity.base.x.WORD = 440 << 16;
    gPlayerEntity.base.y.WORD = 372 << 16;
    Port_Widescreen_RollBegin();
    gRoomControls.scrollAction = 2;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 384);
    gRoomControls.origin_x = 384;
    gRoomControls.origin_y = 16;
    gRoomControls.width = 272;
    gRoomControls.height = 352;
    Port_Widescreen_RollStart();
    CHECK(Port_Widescreen_EffectiveViewWidth() == 384);
    CHECK(gRoomControls.scroll_x == 384 + 272 / 2 - 384 / 2);
    {
        int prevW = 384, prevY = 368;
        for (int k = 1; k < 0x28; ++k) {
            gRoomControls.unk_18 = k;
            gPlayerEntity.base.y.WORD -= Q_16_16(0.375);
            gRoomControls.scroll_y -= 4;
            Port_Widescreen_RollStep();
            const int w = Port_Widescreen_EffectiveViewWidth();
            /* The width eases down about the throne room's centre while the
             * camera holds in the hall (the throne room's rows would bring
             * void beside them), then the camera rolls at 272. */
            CHECK(w <= prevW && prevW - w <= 12);
            CHECK(gRoomControls.scroll_x + w / 2 == 384 + 272 / 2);
            CHECK(k > 12 || gRoomControls.scroll_y == 368);
            CHECK(k < 12 || w == 272);
            CHECK(gRoomControls.scroll_y <= prevY);
            prevW = w;
            prevY = gRoomControls.scroll_y;
        }
    }
    gRoomControls.scrollAction = 0;
    Port_Widescreen_RollEnd();
    for (int k = 0; k < 12; ++k)
        Port_Widescreen_UpdateShadows();
    CHECK(Port_Widescreen_EffectiveViewWidth() == 272);

    /* Width easing: monotone, starts at the start width, reaches the roll
     * width by frame 12 and the destination width by the last frame. */
    {
        /* A narrowing vertical roll holds the camera while its width eases,
         * then covers the whole path and ends where an unheld roll ends. */
        {
            int prevK = 0;
            CHECK(Port_Widescreen_RollCameraK(7, 0x28, 0) == 7);
            for (int k = 0; k <= 12; ++k)
                CHECK(Port_Widescreen_RollCameraK(k, 0x28, 1) == 0);
            CHECK(Port_Widescreen_RollCameraK(0x28, 0x28, 1) == 0x28);
            for (int k = 1; k <= 0x28; ++k) {
                const int kc = Port_Widescreen_RollCameraK(k, 0x28, 1);
                CHECK(kc >= prevK && kc - prevK <= 2);
                prevK = kc;
            }
        }
        int prev = Port_Widescreen_RollWidthAt(384, 240, 336, 0, 0x28);
        CHECK(prev == 384);
        CHECK(Port_Widescreen_RollWidthAt(384, 240, 336, 12, 0x28) == 240);
        CHECK(Port_Widescreen_RollWidthAt(384, 240, 336, 0x28 - 12, 0x28) == 240);
        CHECK(Port_Widescreen_RollWidthAt(384, 240, 336, 0x28, 0x28) == 336);
        for (int k = 1; k <= 0x28; ++k) {
            const int w = Port_Widescreen_RollWidthAt(384, 240, 336, k, 0x28);
            CHECK(k <= 12 ? w <= prev : w >= prev);
            CHECK(w % 2 == 0);
            prev = w;
        }
    }

    /* Rooms offset sideways (Palace of Winds): 720-wide rooms sharing 240
     * columns. The roll runs at the shared 240 so neither room shows void,
     * then widens back to 384 inside the destination. */
    gRoomControls = (RoomControls){ .scroll_x = 1040, .scroll_y = 2992, .origin_x = 1040, .origin_y = 2672,
                                    .width = 720, .height = 480, .scrollAction = 1, .scroll_direction = 2,
                                    .camera_target = &gPlayerEntity.base };
    gPlayerEntity.base.x.WORD = 1160 << 16;
    gPlayerEntity.base.y.WORD = 3146 << 16;
    Port_Widescreen_RollBegin();
    gRoomControls.scrollAction = 2;
    CHECK(Port_Widescreen_EffectiveViewWidth() == 384);
    gRoomControls.origin_x = 560;
    gRoomControls.origin_y = 3152;
    gRoomControls.width = 720;
    gRoomControls.height = 320;
    Port_Widescreen_RollStart();
    for (int k = 1; k < 0x28; ++k) {
        gRoomControls.unk_18 = k;
        gPlayerEntity.base.y.WORD += Q_16_16(0.375);
        gRoomControls.scroll_y += 4;
        Port_Widescreen_RollStep();
        if (k >= 12 && k <= 0x28 - 12) {
            CHECK(Port_Widescreen_EffectiveViewWidth() == 240);
            CHECK(gRoomControls.scroll_x == 1040); /* the shared columns */
        }
    }
    gRoomControls.scrollAction = 0;
    Port_Widescreen_RollEnd();
    Port_Widescreen_RollHandoff();
    /* Widening back to 384 at the room's right edge (x=1280): each wider
     * view keeps the camera inside the room. */
    for (int k = 0; k < 12; ++k) {
        Port_Widescreen_UpdateShadows();
        const int w = Port_Widescreen_EffectiveViewWidth();
        CHECK(gRoomControls.scroll_x >= gRoomControls.origin_x &&
              gRoomControls.scroll_x + w <= gRoomControls.origin_x + (int)gRoomControls.width);
    }

    if (!errors) puts("widescreen engine: shake continuity, iris width/camera, live message window, frame latch, two-room rolling, mismatched-room rolls passed");
    return errors != 0;
}
