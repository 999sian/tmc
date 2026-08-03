#include "port_runtime_config.h"
#include "platform_3ds.h"

#include <stdio.h>
#include <string.h>

static bool sShowFps;
static bool sFollow = true;
static bool sCrests = true;
static bool sFloorReturn = true;
static bool sHideHud;
static bool sHoldText;
static bool sColorCorrection;
static bool sAutosave;
static bool sConsoleParity;
/* The desktop file-select overlay is rendered on the gameplay screen and
 * has no useful 3DS interaction path. Keep the native second-screen UI
 * separate and leave this desktop-only overlay disabled. */
static bool sPortSettings = false;
static bool sVsync = true;
static float sVolume = 1.0f;
static int sBackdrop;

void Port_Config_Load(const char* path) { (void)path; }
void Port_Config_SetActiveSaveProfile(const char* path) { (void)path; }
u8 Port_Config_WindowScale(void) { return 1; }
const char* Port_Config_UpscaleMethod(void) { return "nearest"; }
u64 Port_Config_FrameTimeNs(void) { return 16666667ULL; }
u32 Port_Config_TargetFps(void) { return 60; }
u64 Port_Config_TickTimeNs(void) { return 16666667ULL; }
bool Port_Config_GetDecoupleRender(void) { return false; }
void Port_Config_SetDecoupleRender(bool on) { (void)on; }
bool Port_Config_GetShowFps(void) { return sShowFps; }
void Port_Config_SetShowFps(bool on) { sShowFps = on; }
bool Port_Config_GetTouchControls(void) { return false; }
void Port_Config_SetTouchControls(bool on) { (void)on; }
bool Port_Config_PortSettingsMenuEnabled(void) { return sPortSettings; }
void Port_Config_SetWindowScale(u8 scale) { (void)scale; }
void Port_Config_SetUpscaleMethod(const char* method) { (void)method; }
void Port_Config_SetTargetFps(u32 fps) { (void)fps; }
void Port_Config_CycleTargetFps(int direction) { (void)direction; }
u8 Port_Config_InternalScale(void) { return 1; }
void Port_Config_SetInternalScale(u8 scale) { (void)scale; }
void Port_Config_CycleInternalScale(int direction) { (void)direction; }
PortTouchScheme Port_Config_TouchScheme(void) { return PORT_TOUCH_SCHEME_DPAD; }
void Port_Config_SetTouchScheme(PortTouchScheme scheme) { (void)scheme; }
void Port_Config_CycleTouchScheme(int direction) { (void)direction; }
float Port_Config_TouchScale(void) { return 1.0f; }
void Port_Config_SetTouchScale(float scale) { (void)scale; }
float Port_Config_TouchOpacity(void) { return 1.0f; }
void Port_Config_SetTouchOpacity(float opacity) { (void)opacity; }
bool Port_Config_WidescreenEnabled(void) { return false; }
void Port_Config_SetWidescreenEnabled(bool enabled) { (void)enabled; }
void Port_Config_ToggleWidescreen(void) {}
bool Port_Config_GetConsoleParity(void) { return sConsoleParity; }
void Port_Config_SetConsoleParity(bool on) { sConsoleParity = on; }
void Port_Config_ToggleConsoleParity(void) { sConsoleParity = !sConsoleParity; }
PortAspectMode Port_Config_AspectMode(void) { return PORT_ASPECT_NATIVE_3_2; }
const char* Port_Config_AspectModeName(PortAspectMode mode) { (void)mode; return "Native"; }
void Port_Config_SetAspectMode(PortAspectMode mode) { (void)mode; }
void Port_Config_CycleAspectMode(int direction) { (void)direction; }
PortBgFill Port_Config_BgFill(void) { return PORT_BG_FILL_BLACK; }
const char* Port_Config_BgFillName(PortBgFill fill) { (void)fill; return "Black"; }
void Port_Config_SetBgFill(PortBgFill fill) { (void)fill; }
void Port_Config_CycleBgFill(int direction) { (void)direction; }
void Port_Config_BgFillColor(u8* r, u8* g, u8* b) { *r = *g = *b = 0; }
void Port_Config_SetBgFillColor(u8 r, u8 g, u8 b) { (void)r; (void)g; (void)b; }
PortRenderBackend Port_Config_RenderBackend(void) { return PORT_RENDER_BACKEND_SOFTWARE; }
const char* Port_Config_RenderBackendName(PortRenderBackend b) { (void)b; return "3DS CPU"; }
void Port_Config_SetRenderBackend(PortRenderBackend b) { (void)b; }
void Port_Config_CycleRenderBackend(int direction) { (void)direction; }

#define BOOL_CONFIG(name, initial) \
    bool Port_Config_Get##name(void) { return initial; } \
    void Port_Config_Set##name(bool on) { (void)on; }
BOOL_CONFIG(TtsEnabled, false)
BOOL_CONFIG(A11yCues, false)
BOOL_CONFIG(A11yFootsteps, false)
BOOL_CONFIG(A11yHazards, false)
BOOL_CONFIG(A11yRadar, false)
BOOL_CONFIG(A11yWalls, false)
BOOL_CONFIG(PracticeShowTimer, false)
BOOL_CONFIG(PracticeShowInputs, false)
BOOL_CONFIG(PracticeShowHistory, false)
BOOL_CONFIG(DiscordRpc, false)
BOOL_CONFIG(GpuRaster, false)
BOOL_CONFIG(GpuRasterGles, false)
BOOL_CONFIG(LcdPersistence, false)
BOOL_CONFIG(RibbonEnabled, false)
BOOL_CONFIG(MenuHintSeen, true)
BOOL_CONFIG(RollAttackMacroEnabled, false)
BOOL_CONFIG(Fullscreen, true)
BOOL_CONFIG(FullscreenHideCursor, false)
BOOL_CONFIG(RandoEnabled, false)
BOOL_CONFIG(RandoGlitchless, false)
BOOL_CONFIG(RandoObscure, false)
BOOL_CONFIG(RandoKinstones, false)
BOOL_CONFIG(RandoEntrances, false)
BOOL_CONFIG(RandoDojos, false)
BOOL_CONFIG(RandoOpenWorld, false)
BOOL_CONFIG(RandoHomewarp, false)
BOOL_CONFIG(RandoStartSword, false)
BOOL_CONFIG(RandoEarlyCrests, false)
BOOL_CONFIG(RandoInstantText, false)
BOOL_CONFIG(RandoDungeonItems, false)

float Port_Config_GetTtsRate(void) { return 1.0f; }
void Port_Config_SetTtsRate(float v) { (void)v; }
float Port_Config_GetTtsPitch(void) { return 1.0f; }
void Port_Config_SetTtsPitch(float v) { (void)v; }
float Port_Config_GetTtsVolume(void) { return 1.0f; }
void Port_Config_SetTtsVolume(float v) { (void)v; }
const char* Port_Config_GetTtsVoice(void) { return ""; }
void Port_Config_SetTtsVoice(const char* v) { (void)v; }
const char* Port_Config_GetTtsLanguage(void) { return "en"; }
void Port_Config_SetTtsLanguage(const char* v) { (void)v; }
bool Port_Config_GetSecondScreenFollowCam(void) { return sFollow; }
void Port_Config_SetSecondScreenFollowCam(bool on) { sFollow = on; }
bool Port_Config_GetSecondScreenCrestPins(void) { return sCrests; }
void Port_Config_SetSecondScreenCrestPins(bool on) { sCrests = on; }
bool Port_Config_GetSecondScreenFloorReturn(void) { return sFloorReturn; }
void Port_Config_SetSecondScreenFloorReturn(bool on) { sFloorReturn = on; }
int Port_Config_GetSecondScreenBackdrop(void) { return sBackdrop; }
void Port_Config_SetSecondScreenBackdrop(int style) { sBackdrop = style; }
bool Port_Config_GetSecondScreenSwap(void) { return false; }
void Port_Config_SetSecondScreenSwap(bool on) { (void)on; }
bool Port_Config_GetHideTopHud(void) { return sHideHud; }
void Port_Config_SetHideTopHud(bool on) { sHideHud = on; }
float Port_Config_GetPracticeSlowmo(void) { return 1.0f; }
void Port_Config_SetPracticeSlowmo(float v) { (void)v; }
bool Port_Config_GetVSync(void) { return sVsync; }
void Port_Config_SetVSync(bool on) { sVsync = on; }
bool Port_Config_GetColorCorrection(void) { return sColorCorrection; }
void Port_Config_SetColorCorrection(bool on) { sColorCorrection = on; }
float Port_Config_GetLcdPersistenceRho(void) { return 0.0f; }
void Port_Config_SetLcdPersistenceRho(float v) { (void)v; }
bool Port_Config_GetHoldToAdvanceText(void) { return sHoldText; }
void Port_Config_SetHoldToAdvanceText(bool on) { sHoldText = on; }
float Port_Config_GetMasterVolume(void) { return sVolume; }
void Port_Config_SetMasterVolume(float v) { sVolume = v; }
const char* Port_Config_GetShaderPreset(void) { return ""; }
void Port_Config_SetShaderPreset(const char* path) { (void)path; }
int Port_Config_HasRebornMask(void) { return 0; }
unsigned Port_Config_GetRebornMask(void) { return 0; }
void Port_Config_SetRebornMask(unsigned mask) { (void)mask; }
int Port_Config_GetRandoItemPool(void) { return 0; }
int Port_Config_GetRandoTunicColor(void) { return 0; }
int Port_Config_GetRandoHeartColor(void) { return 0; }
void Port_Config_SetRandoSettings(bool a, bool b, bool c, bool d, bool e, bool f, int g, bool h, bool i, bool j, bool k, int l, int m) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l; (void)m;
}
int Port_Config_GetRandoTricks(void) { return 0; }
void Port_Config_SetRandoTricks(int tricks) { (void)tricks; }
int Port_Config_GetRandoAccessibility(void) { return 0; }
void Port_Config_SetRandoAccessibility(int accessibility) { (void)accessibility; }
void Port_Config_OpenGamepads(void) {}
void Port_Config_CloseGamepads(void) {}

bool Port_Config_InputPressed(PortInput input) {
    const u16 keys = (u16)(~Platform3DS_ReadKeyInput()) & 0x03ff;
    static const u16 masks[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return input < 10 && (keys & masks[input]) != 0;
}
bool Port_Config_InputEdgePressed(PortInput input) {
    const u16 keys = (u16)(~Platform3DS_ReadKeyDownInput()) & 0x03ff;
    static const u16 masks[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return input < 10 && (keys & masks[input]) != 0;
}
bool Port_Config_SoftSlotPressed(int slot) {
    const u32 held = (u32)Platform3DS_KeysHeld();
    static const u32 masks[4] = { 1u << 10, 1u << 11, 1u << 14, 1u << 15 };
    return slot >= 0 && slot < 4 && (held & masks[slot]) != 0;
}
bool Port_Config_GetLeftStick(float* outX, float* outY) {
    float x = 0.0f;
    float y = 0.0f;
    Platform3DS_ReadCircle(&x, &y);
    if (outX) *outX = x;
    if (outY) *outY = y;
    return x != 0.0f || y != 0.0f;
}
float Port_Config_GetAnalogDeadzone(void) { return 0.3f; }
void Port_Config_SetAnalogDeadzone(float v) { (void)v; }
void Port_Config_ClearInputEdges(void) {}
void Port_Config_TestForceEdge(PortInput input) { (void)input; }
void Port_Config_StampInputEdge(PortInput input) { (void)input; }
int Port_Config_PreferredRegion(void) { return 0; }
void Port_Config_SetPreferredRegion(int region) { (void)region; }
int Port_Config_PreferredLanguage(void) { return 0; }
void Port_Config_SetPreferredLanguage(int lang) { (void)lang; }
void Port_Config_SetPortSettingsMenuEnabled(bool enabled) { sPortSettings = enabled; }

bool Port_Config_AutosaveEnabled(void) { return sAutosave; }
void Port_Config_SetAutosaveEnabled(bool enabled) { sAutosave = enabled; }
u32 Port_Config_AutosaveIntervalMs(void) { return 300000; }
void Port_Config_SetAutosaveIntervalMs(u32 ms) { (void)ms; }
