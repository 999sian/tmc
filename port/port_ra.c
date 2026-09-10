/* RetroAchievements client glue (rc_client from libs/rcheevos).
 *
 * This is a decompilation port, not an emulator: there is no live GBA memory
 * image to hand rcheevos. Achievement conditions are served from
 * port_gba_shadow.c, which mirrors the engine's native C globals back into the
 * GBA addresses recorded in linker.ld. RA's GBA memory map is flat
 * (libs/rcheevos/src/rcheevos/consoleinfo.c), so Port_RA_ReadMemory only has
 * to undo that flattening before asking the shadow.
 *
 * The whole translation unit is inert without TMC_RA so an --ra=n build still
 * links, and inert at runtime unless config.json's ra_enabled is true.
 */

#include "port_ra.h"

#include <stddef.h>
#ifndef TMC_RA

/* --ra=n build: every entry point is a no-op and the state is permanently OFF.
 * Keeps every caller (port_main.c, port_bios.c, port_ra_ui.cpp) unconditional. */
void Port_RA_Init(void) {
}
void Port_RA_Shutdown(void) {
}
void Port_RA_FrameTick(void) {
}
void Port_RA_Login(const char* username, const char* password) {
    (void)username;
    (void)password;
}
void Port_RA_LoginWithToken(const char* username, const char* token) {
    (void)username;
    (void)token;
}
void Port_RA_Logout(void) {
}
Port_RA_State Port_RA_GetState(void) {
    return PORT_RA_OFF;
}
const char* Port_RA_GetStatusText(void) {
    return "RetroAchievements support is not compiled into this build";
}
const char* Port_RA_GetUsername(void) {
    return "";
}
const char* Port_RA_GetGameTitle(void) {
    return "";
}
const char* Port_RA_GetRichPresence(void) {
    return "";
}
void Port_RA_GetPoints(int* earned, int* total) {
    if (earned)
        *earned = 0;
    if (total)
        *total = 0;
}
void Port_RA_GetProgress(int* unlocked, int* total) {
    if (unlocked)
        *unlocked = 0;
    if (total)
        *total = 0;
}
int Port_RA_GetAchievementCount(void) {
    return 0;
}
const Port_RA_Achievement* Port_RA_GetAchievement(int index) {
    (void)index;
    return NULL;
}
bool Port_RA_PopToast(Port_RA_Toast* out) {
    (void)out;
    return false;
}
int Port_RA_GetUnmappedCount(void) {
    return 0;
}
uint32_t Port_RA_GetUnmappedAddress(int index, uint32_t* gba_addr) {
    (void)index;
    if (gba_addr)
        *gba_addr = 0;
    return 0;
}
uint32_t Port_RA_GetUnmappedDropped(void) {
    return 0;
}

#else /* TMC_RA */

#include "port_gba_shadow.h"
#include "port_ra_net.h"
#include "port_rom.h"
#include "port_runtime_config.h"

#include "rc_api_request.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Toast ring. Small on purpose: the UI drains it every frame, so a backlog
 * this deep only happens if the UI is not running at all, in which case the
 * oldest notifications are the least interesting. */
#define PORT_RA_TOAST_CAP 8
/* Rich presence is re-evaluated at ~1 Hz; the RA server only accepts a ping
 * every two minutes, and the script walks the whole condition set. */
#define PORT_RA_RP_PERIOD_FRAMES 60

static rc_client_t* sClient;
static Port_RA_State sState = PORT_RA_OFF;
static char sStatus[256] = "RetroAchievements disabled";
static char sUsername[64];
static char sGameTitle[192];
static char sRichPresence[256];
static unsigned sRpCountdown;

/* Achievement snapshot. sList owns the strings sSnapshot points at, so the
 * two are freed/rebuilt together — hence the "valid until the next FrameTick"
 * contract in port_ra.h. */
static rc_client_achievement_list_t* sList;
static Port_RA_Achievement* sSnapshot;
static int sSnapshotCount;
static bool sSnapshotDirty;

static Port_RA_Toast sToasts[PORT_RA_TOAST_CAP];
static int sToastHead;  /* next slot to pop */
static int sToastCount;

/* ---- small helpers --------------------------------------------------- */

static void Port_RA_SetStatus(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sStatus, sizeof(sStatus), fmt, ap);
    va_end(ap);
}

static void Port_RA_CopyStr(char* dst, size_t cap, const char* src) {
    if (!src)
        src = "";
    snprintf(dst, cap, "%s", src);
}

static void Port_RA_PushToast(uint32_t id, int points, const char* title, const char* subtitle) {
    Port_RA_Toast* t;

    if (!Port_Config_GetRaNotifications())
        return;

    if (sToastCount == PORT_RA_TOAST_CAP) {
        /* Drop the oldest so a stalled UI can't wedge new notifications out. */
        sToastHead = (sToastHead + 1) % PORT_RA_TOAST_CAP;
        sToastCount--;
    }
    t = &sToasts[(sToastHead + sToastCount) % PORT_RA_TOAST_CAP];
    sToastCount++;

    t->id = id;
    t->points = points;
    Port_RA_CopyStr(t->title, sizeof(t->title), title);
    Port_RA_CopyStr(t->subtitle, sizeof(t->subtitle), subtitle);
}

/* ---- rcheevos callbacks ---------------------------------------------- */

/* Which addresses does the loaded achievement set actually read? The shadow
 * only covers the pointer-free engine globals it can copy byte-accurately, so
 * a set authored against a real GBA dump may reach for state we do not serve.
 * Record the distinct misses (bounded, first-seen order) so the F8 tab can
 * report exactly which addresses need a new shadow row instead of leaving the
 * achievements silently unsupported. */
#define PORT_RA_UNMAPPED_MAX 64
static uint32_t sUnmappedFlat[PORT_RA_UNMAPPED_MAX];
static uint32_t sUnmappedGba[PORT_RA_UNMAPPED_MAX];
static int sUnmappedCount;
static uint32_t sUnmappedDropped;

static void Port_RA_NoteUnmappedRead(uint32_t flat, uint32_t gba_addr, uint32_t num_bytes) {
    int i;

    for (i = 0; i < sUnmappedCount; i++) {
        if (sUnmappedFlat[i] == flat)
            return;
    }
    if (sUnmappedCount == PORT_RA_UNMAPPED_MAX) {
        sUnmappedDropped++;
        return;
    }
    sUnmappedFlat[sUnmappedCount] = flat;
    sUnmappedGba[sUnmappedCount] = gba_addr;
    sUnmappedCount++;
    fprintf(stderr, "[RA] unmapped read: RA 0x%06X -> GBA 0x%08X (%u bytes) — no shadow row\n", flat, gba_addr,
            num_bytes);
}

int Port_RA_GetUnmappedCount(void) {
    return sUnmappedCount;
}

uint32_t Port_RA_GetUnmappedAddress(int index, uint32_t* gba_addr) {
    if (index < 0 || index >= sUnmappedCount) {
        if (gba_addr != NULL)
            *gba_addr = 0;
        return 0;
    }
    if (gba_addr != NULL)
        *gba_addr = sUnmappedGba[index];
    return sUnmappedFlat[index];
}

uint32_t Port_RA_GetUnmappedDropped(void) {
    return sUnmappedDropped;
}

/* RA addresses GBA memory as one flat span; undo that, then serve from the
 * native-globals shadow. Cart SRAM (0x048000+) has no shadow: this port keeps
 * saves in a native SaveFile, never in a GBA SRAM image, so those reads report
 * zero bytes served and rcheevos marks the affected achievements unsupported
 * instead of evaluating them against garbage. */
static uint32_t Port_RA_ReadMemory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t* client) {
    uint32_t gba_addr;
    uint32_t region_end;

    (void)client;

    if (address <= 0x007FFFu) {
        gba_addr = 0x03000000u + address;
        region_end = 0x007FFFu;
    } else if (address <= 0x047FFFu) {
        gba_addr = 0x02000000u + (address - 0x008000u);
        region_end = 0x047FFFu;
    } else {
        return 0;
    }

    /* Never let a multi-byte read walk out of its RA region into the next
     * one — the two are contiguous for RA but 16 MB apart on the GBA. */
    if (num_bytes > region_end - address + 1u)
        num_bytes = region_end - address + 1u;

    {
        const uint32_t served = Port_GbaShadow_Read(gba_addr, buffer, num_bytes);
        if (served == 0)
            Port_RA_NoteUnmappedRead(address, gba_addr, num_bytes);
        return served;
    }
}

typedef struct {
    rc_client_server_callback_t callback;
    void* callback_data;
} Port_RA_PendingCall;

static void Port_RA_NetDone(const Port_RA_NetResult* result) {
    Port_RA_PendingCall* pending = (Port_RA_PendingCall*)result->user;
    rc_api_server_response_t response;

    response.body = result->body ? result->body : "";
    response.body_length = strlen(response.body);
    /* http_status == 0 means the transfer never completed (DNS, TLS, timeout).
     * rcheevos distinguishes that from an HTTP error via this sentinel and
     * will retry the request itself. */
    response.http_status_code = result->http_status ? result->http_status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;

    pending->callback(&response, pending->callback_data);
    free(pending);
}

static void Port_RA_ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback,
                               void* callback_data, rc_client_t* client) {
    Port_RA_PendingCall* pending;

    (void)client;

    pending = (Port_RA_PendingCall*)malloc(sizeof(*pending));
    if (!pending) {
        rc_api_server_response_t response;
        response.body = "";
        response.body_length = 0;
        response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        callback(&response, callback_data);
        return;
    }
    pending->callback = callback;
    pending->callback_data = callback_data;

    Port_RA_Net_Request(request->url, request->post_data, request->content_type, Port_RA_NetDone, pending);
}

static void Port_RA_LogMessage(const char* message, const rc_client_t* client) {
    (void)client;
    fprintf(stderr, "[RA] %s\n", message ? message : "");
}

/* ---- achievement snapshot ------------------------------------------- */

static void Port_RA_FreeSnapshot(void) {
    if (sList) {
        rc_client_destroy_achievement_list(sList);
        sList = NULL;
    }
    free(sSnapshot);
    sSnapshot = NULL;
    sSnapshotCount = 0;
}

static void Port_RA_RebuildSnapshot(void) {
    uint32_t bucket;
    uint32_t total = 0;
    int out = 0;

    Port_RA_FreeSnapshot();

    if (!rc_client_has_achievements(sClient))
        return;

    sList = rc_client_create_achievement_list(sClient, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                              RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!sList)
        return;

    for (bucket = 0; bucket < sList->num_buckets; bucket++)
        total += sList->buckets[bucket].num_achievements;
    if (total == 0) {
        rc_client_destroy_achievement_list(sList);
        sList = NULL;
        return;
    }

    sSnapshot = (Port_RA_Achievement*)calloc(total, sizeof(*sSnapshot));
    if (!sSnapshot) {
        rc_client_destroy_achievement_list(sList);
        sList = NULL;
        return;
    }

    for (bucket = 0; bucket < sList->num_buckets; bucket++) {
        const rc_client_achievement_bucket_t* b = &sList->buckets[bucket];
        uint32_t i;
        for (i = 0; i < b->num_achievements; i++) {
            const rc_client_achievement_t* a = b->achievements[i];
            if (!a)
                continue;
            sSnapshot[out].title = a->title;
            sSnapshot[out].description = a->description;
            sSnapshot[out].id = a->id;
            sSnapshot[out].points = (int)a->points;
            sSnapshot[out].unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
            sSnapshot[out].measured_percent = a->measured_percent;
            out++;
        }
    }
    sSnapshotCount = out;
}

/* ---- event handler --------------------------------------------------- */

static void Port_RA_EventHandler(const rc_client_event_t* event, rc_client_t* client) {
    (void)client;

    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if (event->achievement) {
                Port_RA_PushToast(event->achievement->id, (int)event->achievement->points,
                                  event->achievement->title, event->achievement->description);
            }
            sSnapshotDirty = true;
            break;

        case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
        case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        case RC_CLIENT_EVENT_SUBSET_COMPLETED:
        case RC_CLIENT_EVENT_RESET:
            /* The only "something changed" signals rc_client gives for the
             * list contents; rebuilding on anything else would reallocate
             * every frame. */
            sSnapshotDirty = true;
            break;

        case RC_CLIENT_EVENT_GAME_COMPLETED: {
            const rc_client_game_t* game = rc_client_get_game_info(sClient);
            Port_RA_PushToast(game ? game->id : 0, 0, "Game Complete!",
                              game && game->title ? game->title : "All achievements earned");
            break;
        }

        case RC_CLIENT_EVENT_SERVER_ERROR:
            Port_RA_SetStatus("RetroAchievements server error: %s",
                              event->server_error && event->server_error->error_message
                                  ? event->server_error->error_message
                                  : "unknown");
            Port_RA_PushToast(event->server_error ? event->server_error->related_id : 0, 0,
                              "RetroAchievements error",
                              event->server_error ? event->server_error->error_message : NULL);
            break;

        case RC_CLIENT_EVENT_DISCONNECTED:
            Port_RA_PushToast(0, 0, "RetroAchievements offline", "Unlocks will be sent once reconnected");
            break;

        case RC_CLIENT_EVENT_RECONNECTED:
            Port_RA_PushToast(0, 0, "RetroAchievements reconnected", "Pending unlocks submitted");
            break;

        default:
            break;
    }
}

/* ---- game identification -------------------------------------------- */

static void Port_RA_LoadGameCallback(int result, const char* error_message, rc_client_t* client, void* userdata) {
    (void)client;
    (void)userdata;

    if (result == RC_OK) {
        const rc_client_game_t* game = rc_client_get_game_info(sClient);
        Port_RA_CopyStr(sGameTitle, sizeof(sGameTitle), game ? game->title : "");
        sState = PORT_RA_ACTIVE;
        sSnapshotDirty = true;
        sRpCountdown = 0; /* show rich presence on the next frame, not up to a second later */
        Port_RA_SetStatus("%s", sGameTitle[0] ? sGameTitle : "Game loaded");
    } else if (result == RC_NO_GAME_LOADED) {
        sState = PORT_RA_UNIDENTIFIED;
        Port_RA_SetStatus("ROM not recognised by RetroAchievements");
    } else if (result == RC_ABORTED) {
        sState = PORT_RA_IDLE;
        Port_RA_SetStatus("Game load cancelled");
    } else {
        sState = PORT_RA_ERROR;
        Port_RA_SetStatus("Could not load game: %s", error_message ? error_message : rc_error_str(result));
    }
}

static void Port_RA_BeginLoadGame(void) {
    if (!gRomData || gRomSize == 0) {
        sState = PORT_RA_ERROR;
        Port_RA_SetStatus("No ROM image available to identify");
        return;
    }
    sState = PORT_RA_LOADING_GAME;
    Port_RA_SetStatus("Identifying game...");
    /* rc_client hashes the buffer itself (rc_hash_generate for
     * RC_CONSOLE_GAMEBOY_ADVANCE) and resolves it against the server. */
    rc_client_begin_identify_and_load_game(sClient, RC_CONSOLE_GAMEBOY_ADVANCE, NULL, gRomData, gRomSize,
                                           Port_RA_LoadGameCallback, NULL);
}

/* ---- login ----------------------------------------------------------- */

static void Port_RA_LoginCallback(int result, const char* error_message, rc_client_t* client, void* userdata) {
    const rc_client_user_t* user;
    const bool persist = userdata != NULL; /* password login: capture the token */

    (void)client;

    if (result != RC_OK) {
        sState = PORT_RA_ERROR;
        sUsername[0] = '\0';
        Port_RA_SetStatus("Login failed: %s", error_message ? error_message : rc_error_str(result));
        return;
    }

    user = rc_client_get_user_info(sClient);
    Port_RA_CopyStr(sUsername, sizeof(sUsername), user ? user->username : "");
    Port_RA_SetStatus("Logged in as %s", sUsername[0] ? sUsername : "(unknown)");

    /* Only the token is ever persisted — the password never leaves the stack
     * of Port_RA_Login. */
    if (persist && user) {
        Port_Config_SetRaUsername(user->username ? user->username : "");
        Port_Config_SetRaToken(user->token ? user->token : "");
    }

    Port_RA_BeginLoadGame();
}

/* ---- client lifetime ------------------------------------------------- */

static bool Port_RA_EnsureClient(void) {
    if (sClient)
        return true;

    Port_RA_Net_Init();

    sClient = rc_client_create(Port_RA_ReadMemory, Port_RA_ServerCall);
    if (!sClient) {
        sState = PORT_RA_ERROR;
        Port_RA_SetStatus("Could not create the RetroAchievements client");
        Port_RA_Net_Shutdown();
        return false;
    }

    rc_client_enable_logging(sClient, RC_CLIENT_LOG_LEVEL_ERROR, Port_RA_LogMessage);
    rc_client_set_event_handler(sClient, Port_RA_EventHandler);
    /* Hardcore is permanently OFF. This port ships save states (F1-F6), speed
     * control / fast-forward and a practice mode, and it is not an
     * RA-approved emulator — offering hardcore would submit leaderboard-grade
     * unlocks from a client that cannot honour hardcore's rules. Softcore
     * unlocks are the honest thing to send. */
    rc_client_set_hardcore_enabled(sClient, 0);
    /* All memory reads must come from the game thread's fresh shadow, never
     * from rc_client's background bookkeeping. */
    rc_client_set_allow_background_memory_reads(sClient, 0);

    sState = PORT_RA_IDLE;
    Port_RA_SetStatus("Not logged in");
    return true;
}

void Port_RA_Init(void) {
    const char* user;
    const char* token;

    if (sClient)
        return;
    if (!Port_Config_GetRaEnabled()) {
        sState = PORT_RA_OFF;
        Port_RA_SetStatus("RetroAchievements disabled");
        return;
    }
    if (!Port_RA_EnsureClient())
        return;

    user = Port_Config_GetRaUsername();
    token = Port_Config_GetRaToken();
    if (user && user[0] && token && token[0])
        Port_RA_LoginWithToken(user, token);
}

void Port_RA_Shutdown(void) {
    Port_RA_FreeSnapshot();
    if (sClient) {
        /* Net first: Port_RA_Net_Shutdown() drains the queue by invoking every
         * outstanding callback, and those land back in rc_client — so the
         * client has to still be alive when it runs. */
        Port_RA_Net_Shutdown();
        rc_client_destroy(sClient);
        sClient = NULL;
    }
    sState = PORT_RA_OFF;
    sToastHead = sToastCount = 0;
    sGameTitle[0] = sUsername[0] = sRichPresence[0] = '\0';
    Port_RA_SetStatus("RetroAchievements disabled");
}

/* ---- per-frame ------------------------------------------------------- */

void Port_RA_FrameTick(void) {
    if (!sClient || !Port_Config_GetRaEnabled())
        return;

    /* 1. Deliver completed HTTP work on this thread (may fire rc_client
     *    callbacks, which may queue events).
     * 2. Re-snapshot the engine's native globals into GBA address space.
     * 3. Only then evaluate conditions against that fresh shadow. */
    Port_RA_Net_Pump();

    if (rc_client_is_game_loaded(sClient)) {
        Port_GbaShadow_Refresh();
        rc_client_do_frame(sClient);
    } else {
        rc_client_idle(sClient);
    }

    if (sSnapshotDirty) {
        sSnapshotDirty = false;
        Port_RA_RebuildSnapshot();
    }

    if (sRpCountdown == 0) {
        sRpCountdown = PORT_RA_RP_PERIOD_FRAMES;
        if (rc_client_has_rich_presence(sClient))
            rc_client_get_rich_presence_message(sClient, sRichPresence, sizeof(sRichPresence));
        else
            sRichPresence[0] = '\0';
    } else {
        sRpCountdown--;
    }
}

/* ---- public control -------------------------------------------------- */

void Port_RA_Login(const char* username, const char* password) {
    if (!username || !username[0] || !password || !password[0]) {
        Port_RA_SetStatus("Username and password are both required");
        return;
    }
    if (!Port_RA_EnsureClient())
        return;

    sState = PORT_RA_LOGGING_IN;
    Port_RA_SetStatus("Logging in as %s...", username);
    /* Non-NULL userdata = "persist the token when this succeeds". */
    rc_client_begin_login_with_password(sClient, username, password, Port_RA_LoginCallback, (void*)&sClient);
}

void Port_RA_LoginWithToken(const char* username, const char* token) {
    if (!username || !username[0] || !token || !token[0]) {
        Port_RA_SetStatus("Not logged in");
        return;
    }
    if (!Port_RA_EnsureClient())
        return;

    sState = PORT_RA_LOGGING_IN;
    Port_RA_SetStatus("Restoring session for %s...", username);
    rc_client_begin_login_with_token(sClient, username, token, Port_RA_LoginCallback, NULL);
}

void Port_RA_Logout(void) {
    Port_RA_FreeSnapshot();
    if (sClient) {
        rc_client_unload_game(sClient);
        rc_client_logout(sClient);
        sState = PORT_RA_IDLE;
    } else {
        sState = PORT_RA_OFF;
    }
    /* Drop the stored token so the next launch does not silently re-login. */
    Port_Config_SetRaToken("");
    sUsername[0] = sGameTitle[0] = sRichPresence[0] = '\0';
    sToastHead = sToastCount = 0;
    Port_RA_SetStatus("Not logged in");
}

/* ---- getters --------------------------------------------------------- */

Port_RA_State Port_RA_GetState(void) {
    return sState;
}

const char* Port_RA_GetStatusText(void) {
    return sStatus;
}

const char* Port_RA_GetUsername(void) {
    return sUsername;
}

const char* Port_RA_GetGameTitle(void) {
    return sGameTitle;
}

const char* Port_RA_GetRichPresence(void) {
    return sRichPresence;
}

void Port_RA_GetPoints(int* earned, int* total) {
    rc_client_user_game_summary_t summary;

    if (!sClient || !rc_client_is_game_loaded(sClient)) {
        if (earned)
            *earned = 0;
        if (total)
            *total = 0;
        return;
    }
    rc_client_get_user_game_summary(sClient, &summary);
    if (earned)
        *earned = (int)summary.points_unlocked;
    if (total)
        *total = (int)summary.points_core;
}

void Port_RA_GetProgress(int* unlocked, int* total) {
    rc_client_user_game_summary_t summary;

    if (!sClient || !rc_client_is_game_loaded(sClient)) {
        if (unlocked)
            *unlocked = 0;
        if (total)
            *total = 0;
        return;
    }
    rc_client_get_user_game_summary(sClient, &summary);
    if (unlocked)
        *unlocked = (int)summary.num_unlocked_achievements;
    if (total)
        *total = (int)summary.num_core_achievements;
}

int Port_RA_GetAchievementCount(void) {
    return sSnapshotCount;
}

const Port_RA_Achievement* Port_RA_GetAchievement(int index) {
    if (!sSnapshot || index < 0 || index >= sSnapshotCount)
        return NULL;
    return &sSnapshot[index];
}

bool Port_RA_PopToast(Port_RA_Toast* out) {
    if (!out || sToastCount == 0)
        return false;
    *out = sToasts[sToastHead];
    sToastHead = (sToastHead + 1) % PORT_RA_TOAST_CAP;
    sToastCount--;
    return true;
}

#endif /* TMC_RA */
