#ifndef PORT_RA_H
#define PORT_RA_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_RA_OFF, PORT_RA_IDLE, PORT_RA_LOGGING_IN, PORT_RA_LOADING_GAME,
    PORT_RA_ACTIVE, PORT_RA_UNIDENTIFIED, PORT_RA_ERROR
} Port_RA_State;

/* Lifecycle. Init after the ROM is loaded (gRomData/gRomSize valid), before AgbMain. */
void Port_RA_Init(void);
void Port_RA_Shutdown(void);
/* Once per emulated frame, at VBlank, after the engine has updated state. */
void Port_RA_FrameTick(void);

void Port_RA_Login(const char* username, const char* password); /* async; stores token on success */
void Port_RA_LoginWithToken(const char* username, const char* token);
void Port_RA_Logout(void);

Port_RA_State Port_RA_GetState(void);
const char* Port_RA_GetStatusText(void);      /* human-readable, never NULL */
const char* Port_RA_GetUsername(void);        /* "" when not logged in */
const char* Port_RA_GetGameTitle(void);       /* "" when no game loaded */
const char* Port_RA_GetRichPresence(void);    /* "" when unavailable */
void Port_RA_GetPoints(int* earned, int* total);
void Port_RA_GetProgress(int* unlocked, int* total);

/* Achievement list snapshot for the UI. Valid until the next FrameTick. */
typedef struct {
    const char* title;
    const char* description;
    uint32_t id;
    int points;
    bool unlocked;
    float measured_percent;   /* 0..100, 0 when not measured */
} Port_RA_Achievement;
int Port_RA_GetAchievementCount(void);
const Port_RA_Achievement* Port_RA_GetAchievement(int index);

/* Toast queue drained by the UI: returns false when empty. */
typedef struct {
    char title[128];
    char subtitle[192];
    uint32_t id;
    int points;
} Port_RA_Toast;
bool Port_RA_PopToast(Port_RA_Toast* out);

/* Distinct addresses the loaded achievement set read that the shadow does not
 * serve — i.e. exactly the state a new port_gba_shadow.c row would have to
 * cover. Empty means the set is fully served. */
int Port_RA_GetUnmappedCount(void);
uint32_t Port_RA_GetUnmappedAddress(int index, uint32_t* gba_addr);
uint32_t Port_RA_GetUnmappedDropped(void);

#ifdef __cplusplus
}
#endif
#endif
