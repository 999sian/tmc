/*
 * port/port_discord_rpc.c — Discord Rich Presence client.
 *
 * Direct IPC implementation: we open the Discord socket and speak the
 * documented JSON-RPC protocol ourselves. No external dependency.
 *
 * Protocol (https://discord.com/developers/docs/topics/rpc):
 *   1. Connect to local socket. On Linux it lives at one of:
 *        $XDG_RUNTIME_DIR/discord-ipc-N
 *        $TMPDIR/discord-ipc-N
 *        /tmp/discord-ipc-N      (N = 0..9, try in order)
 *      On Windows it's a named pipe \\?\pipe\discord-ipc-N (TODO).
 *   2. Send a HANDSHAKE op (opcode 0) with our v=1, client_id=<app>.
 *   3. Send SET_ACTIVITY frames (opcode 1, "cmd":"SET_ACTIVITY") with
 *      the activity payload nested inside.
 *
 * Frame layout (little-endian):
 *   uint32 opcode
 *   uint32 length        (length of `payload`)
 *   uint8  payload[length]
 *
 * Rate limit: 1 SET_ACTIVITY per 15s — we coalesce calls with a
 * timer so the per-frame Update hook is harmless.
 *
 * Security model
 * --------------
 * IPC only — no network sockets, no inbound traffic accepted. The only
 * data we send is the activity JSON (area name + integers), and we
 * never read the responses. No filesystem writes, no process spawning,
 * no code paths that turn received bytes into actions.
 *
 * Discord application ID is *not* shipped in source. Resolution order:
 *   1. TMC_DISCORD_APP_ID env var (highest priority — useful for testing
 *      with an alternate dev app without rebuilding).
 *   2. TMC_DISCORD_APP_ID_DEFAULT compile-time macro, injected by xmake
 *      when discord_app_id.txt exists at repo root (gitignored).
 *   3. Nothing — Rich Presence stays a no-op, one-line stderr warning.
 *
 * Why we don't hard-code the ID even though Discord App IDs are public-
 * by-design (Discord clients broadcast them in plaintext):
 *   - Forks shouldn't inherit our app — their users would see "Playing
 *     The Minish Cap PC Port" under the *original* app name.
 *   - We can't observe who uses an ID we registered; if a fork misuses
 *     it, the brand hit lands on the original registrant.
 *
 * To enable Rich Presence locally (dev): drop your Discord application
 * ID into discord_app_id.txt at repo root and rebuild. The file is in
 * .gitignore so the public repo never contains it.
 * To enable Rich Presence in a release artifact: same — keep the file
 * in the release-builder checkout, build, ship the binary. The ID is
 * baked into the binary, never into source.
 */

#include "port_discord_rpc.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>   /* getenv() inside TryConnectUnix */
#include <string.h>
#include <time.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#define HAS_UNIX_IPC 1
#else
#define HAS_UNIX_IPC 0
#endif

#define DISCORD_RPC_OPCODE_HANDSHAKE   0
#define DISCORD_RPC_OPCODE_FRAME       1
#define DISCORD_RPC_OPCODE_CLOSE       2

/* Discord application ID is *not* hard-coded — see the security-model
 * note in the file header. Provide it at runtime via TMC_DISCORD_APP_ID
 * or Rich Presence stays a no-op. */

#define UPDATE_THROTTLE_MS 15000U   /* Discord rate-limit window */

static struct {
    bool   enabled;
    bool   connected;
    int    sock;
    uint64_t last_send_ms;
    /* Last reported state — we suppress identical re-sends inside the
     * throttle window. */
    char    last_payload[512];
    uint64_t pid;
    /* Unix epoch (seconds) anchoring Discord's "elapsed" counter. Set
     * the first time the user turns Rich Presence on, so the counter
     * shows session-since-enable rather than time-since-1970. */
    uint64_t epoch_start_seconds;
} sState = {
    .enabled = false,
    .connected = false,
    .sock = -1,
    .last_send_ms = 0,
    .last_payload = {0},
    .pid = 0,
    .epoch_start_seconds = 0,
};

#if HAS_UNIX_IPC
static int TryConnectUnix(void) {
    const char* roots[3] = { NULL, NULL, "/tmp" };
    roots[0] = getenv("XDG_RUNTIME_DIR");
    roots[1] = getenv("TMPDIR");

    for (int r = 0; r < 3; ++r) {
        if (!roots[r] || roots[r][0] == '\0') continue;
        for (int n = 0; n < 10; ++n) {
            char path[256];
            const int written = snprintf(path, sizeof(path),
                                         "%s/discord-ipc-%d", roots[r], n);
            if (written <= 0 || written >= (int)sizeof(path)) continue;

            int s = socket(AF_UNIX, SOCK_STREAM, 0);
            if (s < 0) return -1;
            struct sockaddr_un addr = {0};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
            if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                /* Non-blocking so SET_ACTIVITY writes don't stall the
                 * frame loop if Discord's socket is busy. */
                int flags = fcntl(s, F_GETFL, 0);
                if (flags >= 0) fcntl(s, F_SETFL, flags | O_NONBLOCK);
                return s;
            }
            close(s);
        }
    }
    return -1;
}
#endif

static bool SendFrame(uint32_t opcode, const char* payload, uint32_t len) {
    if (sState.sock < 0) return false;
#if HAS_UNIX_IPC
    uint8_t header[8];
    /* little-endian, matching Discord's documented frame format */
    header[0] = (uint8_t)(opcode & 0xff);
    header[1] = (uint8_t)((opcode >> 8) & 0xff);
    header[2] = (uint8_t)((opcode >> 16) & 0xff);
    header[3] = (uint8_t)((opcode >> 24) & 0xff);
    header[4] = (uint8_t)(len & 0xff);
    header[5] = (uint8_t)((len >> 8) & 0xff);
    header[6] = (uint8_t)((len >> 16) & 0xff);
    header[7] = (uint8_t)((len >> 24) & 0xff);
    /* MSG_NOSIGNAL prevents SIGPIPE when Discord exits mid-send. */
    if (send(sState.sock, header, 8, MSG_NOSIGNAL) != 8) return false;
    if (len > 0 && send(sState.sock, payload, len, MSG_NOSIGNAL) != (ssize_t)len) return false;
    return true;
#else
    (void)opcode; (void)payload; (void)len;
    return false;
#endif
}

static const char* ResolveClientId(void) {
    const char* env = getenv("TMC_DISCORD_APP_ID");
    if (env && env[0]) return env;
#ifdef TMC_DISCORD_APP_ID_DEFAULT
    /* Compile-time fallback injected by xmake from a gitignored
     * discord_app_id.txt at repo root. Empty in the public-source build;
     * a developer or release pipeline populates the file locally. */
    return TMC_DISCORD_APP_ID_DEFAULT;
#else
    return NULL;
#endif
}

/* Defensive JSON string escape — area names are currently static ASCII
 * with no special characters, but `gRoomControls.area`-driven labels may
 * grow over time (mods, custom areas). Escaping here keeps the activity
 * payload syntactically valid no matter what shows up upstream. Only
 * handles the chars the JSON spec strictly requires; non-ASCII bytes
 * pass through (Discord accepts UTF-8). Caller-supplied `cap` is the
 * full buffer size including the NUL terminator. */
static void JsonEscape(const char* in, char* out, size_t cap) {
    if (cap == 0) return;
    size_t w = 0;
    for (size_t r = 0; in && in[r] && w + 2 < cap; ++r) {
        unsigned char c = (unsigned char)in[r];
        const char* esc = NULL;
        char esc_buf[8];
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", c);
                    esc = esc_buf;
                }
                break;
        }
        if (esc) {
            size_t elen = strlen(esc);
            if (w + elen + 1 >= cap) break;
            memcpy(out + w, esc, elen);
            w += elen;
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = '\0';
}

static bool EnsureConnected(void) {
    if (sState.connected) return true;
#if HAS_UNIX_IPC
    const char* client_id = ResolveClientId();
    if (!client_id) {
        /* No app ID supplied — Rich Presence is a no-op. Logged once so
         * the user sees why the F8 toggle has no visible effect. */
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[discord-rpc] disabled: set TMC_DISCORD_APP_ID env "
                            "var to a Discord application ID to enable Rich "
                            "Presence. Register an app at "
                            "https://discord.com/developers/applications.\n");
            warned = true;
        }
        return false;
    }
    sState.sock = TryConnectUnix();
    if (sState.sock < 0) return false;

    char handshake[256];
    int n = snprintf(handshake, sizeof(handshake),
                     "{\"v\":1,\"client_id\":\"%s\"}", client_id);
    if (n <= 0 || n >= (int)sizeof(handshake)) {
        close(sState.sock); sState.sock = -1;
        return false;
    }
    if (!SendFrame(DISCORD_RPC_OPCODE_HANDSHAKE, handshake, (uint32_t)n)) {
        close(sState.sock); sState.sock = -1;
        return false;
    }
    sState.connected = true;
    sState.pid = (uint64_t)getpid();
    fprintf(stderr, "[discord-rpc] connected (client_id=%s)\n", client_id);
    return true;
#else
    return false;
#endif
}

void Port_DiscordRpc_SetEnabled(bool on) {
    if (on && !sState.enabled) {
        /* Anchor the elapsed-time counter to the moment of opt-in. */
        sState.epoch_start_seconds = (uint64_t)time(NULL);
    }
    sState.enabled = on;
    if (!on && sState.connected) {
        Port_DiscordRpc_Shutdown();
    }
}

bool Port_DiscordRpc_IsEnabled(void) { return sState.enabled; }

void Port_DiscordRpc_Update(const char* area_name,
                            int hearts_now, int hearts_max,
                            int rupees) {
    if (!sState.enabled) return;
    const uint64_t now = SDL_GetTicks();
    if (now < sState.last_send_ms + UPDATE_THROTTLE_MS && sState.last_send_ms != 0) {
        return;
    }
    if (!EnsureConnected()) return;

    if (!area_name || !area_name[0]) area_name = "Adventuring";

    /* JSON-escape the area name in case it ever contains chars that
     * would break the payload (quotes, backslashes, control bytes). */
    char details[256];
    JsonEscape(area_name, details, sizeof(details));

    /* state_line is built from integers via snprintf — no escape needed. */
    char state_line[128];
    snprintf(state_line, sizeof(state_line),
             "%d/%d hearts · %d rupees",
             hearts_now, hearts_max, rupees);

    char activity[768];
    int n = snprintf(activity, sizeof(activity),
        "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%llu\",\"args\":{"
            "\"pid\":%llu,"
            "\"activity\":{"
                "\"details\":\"%s\","
                "\"state\":\"%s\","
                "\"timestamps\":{\"start\":%llu},"
                "\"assets\":{\"large_image\":\"minishcap\",\"large_text\":\"The Minish Cap\"}"
            "}"
        "}}",
        (unsigned long long)now,
        (unsigned long long)sState.pid,
        details, state_line,
        (unsigned long long)sState.epoch_start_seconds);
    if (n <= 0 || n >= (int)sizeof(activity)) return;

    /* Skip if identical content arrived inside the same minute — keeps
     * Discord's update queue clean. */
    if (strncmp(activity, sState.last_payload, sizeof(sState.last_payload)) == 0) {
        return;
    }

    if (!SendFrame(DISCORD_RPC_OPCODE_FRAME, activity, (uint32_t)n)) {
        /* Discord likely closed the socket — drop state so the next
         * call retries from scratch. */
        Port_DiscordRpc_Shutdown();
        return;
    }
    strncpy(sState.last_payload, activity, sizeof(sState.last_payload) - 1);
    sState.last_payload[sizeof(sState.last_payload) - 1] = '\0';
    sState.last_send_ms = now;
}

void Port_DiscordRpc_Shutdown(void) {
#if HAS_UNIX_IPC
    if (sState.sock >= 0) {
        SendFrame(DISCORD_RPC_OPCODE_CLOSE, "{}", 2);
        close(sState.sock);
        sState.sock = -1;
    }
#endif
    sState.connected = false;
    sState.last_payload[0] = '\0';
    sState.last_send_ms = 0;
}
