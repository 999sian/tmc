/*
 * port_softslots.c — Extra item-equip buttons (X / Y / L2 / R2).
 *
 * Engine dispatch path (src/playerUtils.c:UpdateActiveItems) reads
 *   gSave.stats.equipped[SLOT_B] and INPUT_USE_ITEM2 each frame.
 * To add four extra equip buttons without touching the save format we:
 *   1. Each frame, scan the four soft-slot inputs. The most recently
 *      pressed slot with an assigned item wins (sticky while held).
 *   2. While a slot is active, port_bios.c forces B_BUTTON pressed in the
 *      GBA KEYINPUT register so the engine produces an INPUT_USE_ITEM2
 *      bit on its abstract input.
 *   3. Two functions in src/playerUtils.c (the B-dispatch site and the
 *      held-item active check) consult Port_SoftSlots_GetEffectiveBItem /
 *      Port_SoftSlots_IsBHeld to override the effective equipped[SLOT_B]
 *      ONLY at those points. The save data is never mutated.
 *
 * Charged items (Gust Jar, Bow): the active state persists for as long
 * as the soft-slot button is held, so the engine's "hold to charge,
 * release to fire" works automatically. When the button releases, B is
 * no longer forced and the engine sees the release.
 *
 * Multi-slot: last-pressed wins. Pressing R2 while still holding L2
 * switches the active slot mid-action; releasing the newer one with the
 * older still held picks any remaining held slot (highest index) so an
 * item is always firing rather than a flicker between two states.
 *
 * Persistence: tmc.softslots, sidecar to tmc.sav. 6-byte magic + 4 bytes.
 * Survives across runs but is unrelated to the EEPROM save.
 */

#include "port_softslots.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* Engine-side queries used by the assignment UI. Declared as plain externs
 * so this TU doesn't drag in the full game headers (and the type collisions
 * that come with them). The numeric ITEM_* values match include/item.h —
 * see comments next to the table below. */
extern unsigned int GetInventoryValue(unsigned int item);

/* Mirror of the relevant prefix of ItemMetaData. We only need menuSlot and
 * the existence of the entry; the real struct (include/itemMetaData.h) has
 * eight u8 fields starting with menuSlot. */
struct PortSoftSlotItemMeta { unsigned char menuSlot; unsigned char rest[7]; };
extern const struct PortSoftSlotItemMeta gItemMetaData[];

#define SOFTSLOT_FILENAME "tmc.softslots"
static const char SOFTSLOT_MAGIC[6] = { 'T', 'M', 'C', 'S', 'S', '1' };

static uint8_t sAssignments[PORT_SOFTSLOT_COUNT];
static int sActiveSlot = -1;
static bool sLoaded = false;

/* Defined in port_runtime_config.cpp. Returns true if the soft-slot's
 * bound input (keyboard or gamepad button/trigger) is currently held. */
extern bool Port_Config_SoftSlotPressed(int slot);

const char* Port_SoftSlots_SlotName(int slot) {
    switch (slot) {
        case 0: return "X";
        case 1: return "Y";
        case 2: return "L2";
        case 3: return "R2";
        default: return "?";
    }
}

void Port_SoftSlots_Init(void) {
    if (sLoaded) return;
    Port_SoftSlots_Load();
    sLoaded = true;
}

void Port_SoftSlots_Update(void) {
    Port_SoftSlots_Init();

    static bool sPrevHeld[PORT_SOFTSLOT_COUNT] = { false, false, false, false };
    bool nowHeld[PORT_SOFTSLOT_COUNT];
    int newlyPressed = -1;

    for (int i = 0; i < PORT_SOFTSLOT_COUNT; i++) {
        nowHeld[i] = Port_Config_SoftSlotPressed(i) && sAssignments[i] != 0;
        if (nowHeld[i] && !sPrevHeld[i]) {
            /* Later iterations overwrite, giving last-iterated == highest-
             * index newly-pressed slot priority. */
            newlyPressed = i;
        }
    }

    int chosen = -1;
    if (newlyPressed >= 0) {
        chosen = newlyPressed;
    } else if (sActiveSlot >= 0 && nowHeld[sActiveSlot]) {
        chosen = sActiveSlot;
    } else {
        for (int i = PORT_SOFTSLOT_COUNT - 1; i >= 0; i--) {
            if (nowHeld[i]) {
                chosen = i;
                break;
            }
        }
    }

    sActiveSlot = chosen;
    memcpy(sPrevHeld, nowHeld, sizeof(sPrevHeld));
}

bool Port_SoftSlots_IsBHeld(void) {
    return sActiveSlot >= 0;
}

uint8_t Port_SoftSlots_GetEffectiveBItem(uint8_t saved) {
    if (sActiveSlot < 0) return saved;
    uint8_t a = sAssignments[sActiveSlot];
    return a ? a : saved;
}

uint8_t Port_SoftSlots_GetAssignment(int slot) {
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return 0;
    return sAssignments[slot];
}

void Port_SoftSlots_SetAssignment(int slot, uint8_t itemId) {
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return;
    sAssignments[slot] = itemId;
    Port_SoftSlots_Save();
}

void Port_SoftSlots_Save(void) {
    FILE* f = fopen(SOFTSLOT_FILENAME, "wb");
    if (!f) return;
    fwrite(SOFTSLOT_MAGIC, 1, sizeof(SOFTSLOT_MAGIC), f);
    fwrite(sAssignments, 1, sizeof(sAssignments), f);
    fclose(f);
}

void Port_SoftSlots_Load(void) {
    memset(sAssignments, 0, sizeof(sAssignments));
    FILE* f = fopen(SOFTSLOT_FILENAME, "rb");
    if (!f) return;
    char magic[sizeof(SOFTSLOT_MAGIC)];
    if (fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
        memcmp(magic, SOFTSLOT_MAGIC, sizeof(magic)) == 0) {
        if (fread(sAssignments, 1, sizeof(sAssignments), f) != sizeof(sAssignments)) {
            memset(sAssignments, 0, sizeof(sAssignments));
        }
    }
    fclose(f);
}

/* ---- Assignment UI helpers (cycling through owned items) -------------- */

/* Equippable items in cycle order. Mirrors the ITEM_* enum values from
 * include/item.h that have a menu slot — i.e. the things the pause menu
 * lets the player put on A or B. ITEM_LANTERN_OFF is preferred over
 * ITEM_LANTERN_ON because that's the cold-start id; the engine swaps to
 * _ON at runtime. ITEM_BOMBS / _BOW / _BOOMERANG / _SHIELD / _LANTERN
 * pair up with their upgraded variants — owning the upgraded variant
 * implies ownership of the base one for our purposes. */
static const uint8_t kEquippable[] = {
    /* values mirror include/item.h enum positions 1..0x21 */
    1,  /* ITEM_SMITH_SWORD       */
    2,  /* ITEM_GREEN_SWORD       */
    3,  /* ITEM_RED_SWORD         */
    4,  /* ITEM_BLUE_SWORD        */
    7,  /* ITEM_BOMBS             */
    8,  /* ITEM_REMOTE_BOMBS      */
    9,  /* ITEM_BOW               */
    10, /* ITEM_LIGHT_ARROW       */
    11, /* ITEM_BOOMERANG         */
    12, /* ITEM_MAGIC_BOOMERANG   */
    13, /* ITEM_SHIELD            */
    14, /* ITEM_MIRROR_SHIELD     */
    15, /* ITEM_LANTERN_OFF       */
    17, /* ITEM_GUST_JAR          */
    18, /* ITEM_PACCI_CANE        */
    19, /* ITEM_MOLE_MITTS        */
    20, /* ITEM_ROCS_CAPE         */
    21, /* ITEM_PEGASUS_BOOTS     */
    23, /* ITEM_OCARINA           */
    28, /* ITEM_BOTTLE1           */
    29, /* ITEM_BOTTLE2           */
    30, /* ITEM_BOTTLE3           */
    31, /* ITEM_BOTTLE4           */
};

static const char* ItemDisplayName(uint8_t id) {
    switch (id) {
        case 0:  return "(unassigned)";
        case 1:  return "Sword (Smith)";
        case 2:  return "Sword (Green)";
        case 3:  return "Sword (Red)";
        case 4:  return "Sword (Blue)";
        case 7:  return "Bombs";
        case 8:  return "Remote Bombs";
        case 9:  return "Bow";
        case 10: return "Light Arrow";
        case 11: return "Boomerang";
        case 12: return "Magic Boomerang";
        case 13: return "Shield";
        case 14: return "Mirror Shield";
        case 15: return "Lantern";
        case 17: return "Gust Jar";
        case 18: return "Pacci Cane";
        case 19: return "Mole Mitts";
        case 20: return "Roc's Cape";
        case 21: return "Pegasus Boots";
        case 23: return "Ocarina";
        case 28: return "Bottle 1";
        case 29: return "Bottle 2";
        case 30: return "Bottle 3";
        case 31: return "Bottle 4";
        default: return "?";
    }
}

const char* Port_SoftSlots_GetSlotLabel(int slot) {
    static char buf[64];
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return "?";
    uint8_t id = sAssignments[slot];
    snprintf(buf, sizeof(buf), "%-3s : %s",
             Port_SoftSlots_SlotName(slot), ItemDisplayName(id));
    return buf;
}

/* Returns the table index of `id`, or -1 if not in the table. */
static int CycleIndexOf(uint8_t id) {
    if (id == 0) return -1; /* "unassigned" lives outside the table */
    for (size_t i = 0; i < sizeof(kEquippable) / sizeof(kEquippable[0]); i++) {
        if (kEquippable[i] == id) return (int)i;
    }
    return -1;
}

void Port_SoftSlots_CycleAssignment(int slot, int direction) {
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return;
    if (direction == 0) return;
    int step = direction > 0 ? 1 : -1;
    int n = (int)(sizeof(kEquippable) / sizeof(kEquippable[0]));

    /* Walk the circular sequence (unassigned, kEquippable[0..n-1]) until
     * we hit a position the player owns (or wrap back to start). */
    int idx = CycleIndexOf(sAssignments[slot]); /* -1 = unassigned */
    for (int tries = 0; tries < n + 1; tries++) {
        idx += step;
        if (idx >= n) {
            idx = -1; /* wrap to "unassigned" */
        } else if (idx < -1) {
            idx = n - 1;
        }
        uint8_t cand = idx < 0 ? 0 : kEquippable[idx];
        if (cand == 0 || GetInventoryValue(cand) == 1) {
            Port_SoftSlots_SetAssignment(slot, cand);
            return;
        }
    }
    /* Player has no owned items at all; leave unchanged. */
}

/* ---- Pause-menu integration --------------------------------------- */

/* Frames-of-grace counter. Subtask_PauseMenu pumps this each engine frame
 * the menu is open; Port_UpdateInput decays it. We stay "active" for a
 * couple of frames after the engine stops calling NotifyPauseActive so
 * the overlay doesn't flicker during the engine's frame jitter. */
static int sPauseFramesGrace = 0;
static bool sConfigOpen = false;
static int sConfigCursor = 0;

void Port_SoftSlots_NotifyPauseActive(void) {
    sPauseFramesGrace = 4;
}

void Port_SoftSlots_TickPause(void) {
    if (sPauseFramesGrace > 0) {
        sPauseFramesGrace--;
    } else if (sConfigOpen) {
        /* Player closed the start menu while config overlay was open
         * (e.g. Esc'd out of pause via Start). Drop the overlay too. */
        sConfigOpen = false;
    }
}

bool Port_SoftSlots_IsPauseActive(void) {
    return sPauseFramesGrace > 0;
}

bool Port_SoftSlots_ConfigIsOpen(void) {
    return sConfigOpen;
}

void Port_SoftSlots_ConfigOpen(void) {
    Port_SoftSlots_Init();
    sConfigOpen = true;
    sConfigCursor = 0;
}

void Port_SoftSlots_ConfigClose(void) {
    sConfigOpen = false;
}

bool Port_SoftSlots_ConfigHandleKey(int sdlKey) {
    if (!sConfigOpen) return false;
    switch (sdlKey) {
        case SDLK_UP:
            sConfigCursor = (sConfigCursor + PORT_SOFTSLOT_COUNT - 1) % PORT_SOFTSLOT_COUNT;
            return true;
        case SDLK_DOWN:
            sConfigCursor = (sConfigCursor + 1) % PORT_SOFTSLOT_COUNT;
            return true;
        case SDLK_LEFT:
            Port_SoftSlots_CycleAssignment(sConfigCursor, -1);
            return true;
        case SDLK_RIGHT:
            Port_SoftSlots_CycleAssignment(sConfigCursor, +1);
            return true;
        case SDLK_RETURN:
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            sConfigOpen = false;
            return true;
        default:
            return false;
    }
}

/* ---- Rendering ---------------------------------------------------- */

static const char* SlotValueLabel(int slot) {
    /* sAssignments stored across runs; render "—" for empty so the panel
     * looks intentional rather than crashed. */
    uint8_t id = sAssignments[slot];
    if (id == 0) return "---";
    /* Reuse ItemDisplayName via the same switch the cycle code drives. */
    static char buf[32];
    SDL_strlcpy(buf, ItemDisplayName(id), sizeof(buf));
    return buf;
}

/* The framebuffer info bar and the OAM letter-badge sprite injection
 * both lived here at one point. They're gone — the framebuffer bar
 * was occluding the engine's item-name display and the OAM injection
 * was leaking into pause-menu sprite tiles. The remaining UI is the
 * SDL `\` configuration overlay (RenderConfigOverlay, below) and the
 * F8 → "Extra equip slots" page in the debug menu. */

#if 0  /* disabled — kept for reference */
static const uint8_t kGlyph_dot[5]   = { 0b000, 0b000, 0b000, 0b000, 0b010 };
static const uint8_t kGlyph_dash[5]  = { 0b000, 0b000, 0b111, 0b000, 0b000 };
static const uint8_t kGlyph_colon[5] = { 0b000, 0b010, 0b000, 0b010, 0b000 };
static const uint8_t kGlyph_lb[5]    = { 0b110, 0b100, 0b100, 0b100, 0b110 };
static const uint8_t kGlyph_rb[5]    = { 0b011, 0b001, 0b001, 0b001, 0b011 };
static const uint8_t kGlyph_space[5] = { 0, 0, 0, 0, 0 };
static const uint8_t kGlyph_qmark[5] = { 0b110, 0b001, 0b010, 0b000, 0b010 };
static const uint8_t kGlyph_apos[5]  = { 0b010, 0b010, 0b000, 0b000, 0b000 };

static const uint8_t kGlyph_0[5] = { 0b111, 0b101, 0b101, 0b101, 0b111 };
static const uint8_t kGlyph_1[5] = { 0b010, 0b110, 0b010, 0b010, 0b111 };
static const uint8_t kGlyph_2[5] = { 0b110, 0b001, 0b010, 0b100, 0b111 };
static const uint8_t kGlyph_3[5] = { 0b110, 0b001, 0b110, 0b001, 0b110 };
static const uint8_t kGlyph_4[5] = { 0b101, 0b101, 0b111, 0b001, 0b001 };

static const uint8_t kGlyph_A[5] = { 0b010, 0b101, 0b111, 0b101, 0b101 };
static const uint8_t kGlyph_B[5] = { 0b110, 0b101, 0b110, 0b101, 0b110 };
static const uint8_t kGlyph_C[5] = { 0b011, 0b100, 0b100, 0b100, 0b011 };
static const uint8_t kGlyph_D[5] = { 0b110, 0b101, 0b101, 0b101, 0b110 };
static const uint8_t kGlyph_E[5] = { 0b111, 0b100, 0b110, 0b100, 0b111 };
static const uint8_t kGlyph_F[5] = { 0b111, 0b100, 0b110, 0b100, 0b100 };
static const uint8_t kGlyph_G[5] = { 0b011, 0b100, 0b101, 0b101, 0b011 };
static const uint8_t kGlyph_H[5] = { 0b101, 0b101, 0b111, 0b101, 0b101 };
static const uint8_t kGlyph_I[5] = { 0b111, 0b010, 0b010, 0b010, 0b111 };
static const uint8_t kGlyph_J[5] = { 0b001, 0b001, 0b001, 0b101, 0b010 };
static const uint8_t kGlyph_K[5] = { 0b101, 0b101, 0b110, 0b101, 0b101 };
static const uint8_t kGlyph_L[5] = { 0b100, 0b100, 0b100, 0b100, 0b111 };
static const uint8_t kGlyph_M[5] = { 0b101, 0b111, 0b111, 0b101, 0b101 };
static const uint8_t kGlyph_N[5] = { 0b101, 0b111, 0b111, 0b111, 0b101 };
static const uint8_t kGlyph_O[5] = { 0b010, 0b101, 0b101, 0b101, 0b010 };
static const uint8_t kGlyph_P[5] = { 0b110, 0b101, 0b110, 0b100, 0b100 };
static const uint8_t kGlyph_Q[5] = { 0b010, 0b101, 0b101, 0b111, 0b011 };
static const uint8_t kGlyph_R[5] = { 0b110, 0b101, 0b110, 0b101, 0b101 };
static const uint8_t kGlyph_S[5] = { 0b011, 0b100, 0b010, 0b001, 0b110 };
static const uint8_t kGlyph_T[5] = { 0b111, 0b010, 0b010, 0b010, 0b010 };
static const uint8_t kGlyph_U[5] = { 0b101, 0b101, 0b101, 0b101, 0b011 };
static const uint8_t kGlyph_V[5] = { 0b101, 0b101, 0b101, 0b101, 0b010 };
static const uint8_t kGlyph_W[5] = { 0b101, 0b101, 0b111, 0b111, 0b101 };
static const uint8_t kGlyph_X[5] = { 0b101, 0b101, 0b010, 0b101, 0b101 };
static const uint8_t kGlyph_Y[5] = { 0b101, 0b101, 0b010, 0b010, 0b010 };
static const uint8_t kGlyph_Z[5] = { 0b111, 0b001, 0b010, 0b100, 0b111 };

static const uint8_t* PickGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case 'A': return kGlyph_A; case 'B': return kGlyph_B; case 'C': return kGlyph_C;
        case 'D': return kGlyph_D; case 'E': return kGlyph_E; case 'F': return kGlyph_F;
        case 'G': return kGlyph_G; case 'H': return kGlyph_H; case 'I': return kGlyph_I;
        case 'J': return kGlyph_J; case 'K': return kGlyph_K; case 'L': return kGlyph_L;
        case 'M': return kGlyph_M; case 'N': return kGlyph_N; case 'O': return kGlyph_O;
        case 'P': return kGlyph_P; case 'Q': return kGlyph_Q; case 'R': return kGlyph_R;
        case 'S': return kGlyph_S; case 'T': return kGlyph_T; case 'U': return kGlyph_U;
        case 'V': return kGlyph_V; case 'W': return kGlyph_W; case 'X': return kGlyph_X;
        case 'Y': return kGlyph_Y; case 'Z': return kGlyph_Z;
        case '0': return kGlyph_0; case '1': return kGlyph_1; case '2': return kGlyph_2;
        case '3': return kGlyph_3; case '4': return kGlyph_4;
        case '.': return kGlyph_dot;
        case '-': return kGlyph_dash;
        case ':': return kGlyph_colon;
        case '[': return kGlyph_lb;
        case ']': return kGlyph_rb;
        case ' ': return kGlyph_space;
        case '\'': return kGlyph_apos;
        default:  return kGlyph_qmark;
    }
}

#define FB_W 240
#define FB_H 160
#define GLYPH_W 3
#define GLYPH_H 5
#define CHAR_PITCH 4   /* 3px glyph + 1px gap */
#define LINE_PITCH 6   /* 5px glyph + 1px gap */

static void DrawGlyph(uint32_t* fb, int x, int y, char c, uint32_t color) {
    const uint8_t* glyph = PickGlyph(c);
    for (int row = 0; row < GLYPH_H; row++) {
        int py = y + row;
        if (py < 0 || py >= FB_H) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < GLYPH_W; col++) {
            int px = x + col;
            if (px < 0 || px >= FB_W) continue;
            /* MSB of the 3-bit row is the leftmost pixel. */
            if (bits & (1u << (GLYPH_W - 1 - col))) {
                fb[py * FB_W + px] = color;
            }
        }
    }
}

static int DrawString(uint32_t* fb, int x, int y, const char* s, uint32_t color) {
    int dx = 0;
    while (*s) {
        DrawGlyph(fb, x + dx, y, *s, color);
        dx += CHAR_PITCH;
        s++;
    }
    return dx;
}

static void FillRect(uint32_t* fb, int x, int y, int w, int h, uint32_t color) {
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= FB_H) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= FB_W) continue;
            fb[py * FB_W + px] = color;
        }
    }
}

void Port_SoftSlots_DrawInfoIntoFramebuffer(uint32_t* fb) {
    if (!fb) return;
    if (!Port_SoftSlots_IsPauseActive()) return;
    if (sConfigOpen) return; /* config overlay is the modal focus */

    /* Layout: a row of four "[X]Lantern" cells along the bottom of the
     * 240x160 frame, plus a one-line hint underneath. We sit in y=146..159
     * which on the TMC pause menu is below the active panel art.
     *
     * ABGR8888 little-endian: 0xAABBGGRR. The TMC pause menu uses a dark
     * blue background for its panel, so we use a translucent-feeling
     * dark grey backdrop and TMC-yellow for the slot labels. */
    const uint32_t kBgColor      = 0xFF101018u; /* near-black, slight blue */
    const uint32_t kBorderColor  = 0xFF6080A0u; /* slate */
    const uint32_t kLabelColor   = 0xFF40D0FFu; /* cyan-ish for slot tags */
    const uint32_t kValueColor   = 0xFFFFFFFFu;
    const uint32_t kEmptyColor   = 0xFF606060u;
    const uint32_t kHintColor    = 0xFFA0A0A0u;

    const int barY = 146;
    const int barH = 14; /* 5px glyph + 1px padding * 2 + 2px frame */
    FillRect(fb, 1, barY, FB_W - 2, barH, kBgColor);
    /* 1-pixel border top + bottom */
    FillRect(fb, 1, barY, FB_W - 2, 1, kBorderColor);
    FillRect(fb, 1, barY + barH - 1, FB_W - 2, 1, kBorderColor);

    /* Four cells, ~58 px each, evenly spaced. */
    const int cellW = (FB_W - 2) / 4;
    for (int i = 0; i < PORT_SOFTSLOT_COUNT; i++) {
        int cellX = 1 + i * cellW;
        /* Vertical separator between cells */
        if (i > 0) FillRect(fb, cellX, barY + 1, 1, barH - 2, kBorderColor);

        /* Slot tag at left side of cell. */
        char tag[8];
        snprintf(tag, sizeof(tag), "[%s]", Port_SoftSlots_SlotName(i));
        int textY = barY + 4;
        int tagWidth = DrawString(fb, cellX + 3, textY, tag, kLabelColor);

        /* Item name (uppercased by the font, truncated to fit). */
        const char* full = SlotValueLabel(i);
        int avail = cellW - tagWidth - 6; /* 3 left, 3 right pad */
        int maxChars = avail / CHAR_PITCH;
        if (maxChars < 1) maxChars = 1;
        char buf[16];
        int n = 0;
        for (; full[n] && n < (int)sizeof(buf) - 1 && n < maxChars; n++) {
            buf[n] = full[n];
        }
        buf[n] = '\0';
        bool empty = (full[0] == '-' || full[0] == 0);
        DrawString(fb, cellX + 3 + tagWidth + 1, textY, buf,
                   empty ? kEmptyColor : kValueColor);
    }

    /* No persistent hint — first run users see the F8 menu and the config
     * key (\\) is also documented in the F8 page. */
    (void)kHintColor;
}

/* ---- Pause-menu sprite badges (X / Y / L / R) ---------------------- *
 *
 * Hand-rolled 8x8 4bpp tile data for the four letter glyphs, designed
 * to mimic the engine's existing A indicator: red filled square with a
 * yellow letter inside. Palette indices used:
 *   0 = transparent
 *   1 = red (background fill)
 *   2 = yellow (letter)
 *
 * Each tile is encoded as 8 rows of 4 bytes; each byte holds two
 * pixels (low nibble = leftmost pixel). Palette colour is picked from
 * the engine's existing OBJ palette 0, which the pause-menu sprites
 * already use — entries 1 and 2 there happen to be red/orange and
 * yellow on TMC's UI palette, giving us native-matching colours
 * without loading custom palette data. */

/* Pretty-print convention: each row is two bytes plus two bytes ==
 * 8 pixels. Read R/Y left-to-right and the tile is the obvious shape. */
#define R 1u  /* red    */
#define Y 2u  /* yellow */
#define _ 0u  /* transparent — currently unused; full 8x8 fill */

#define ROW(p0,p1,p2,p3,p4,p5,p6,p7) \
    (uint8_t)(((p1) << 4) | (p0)), \
    (uint8_t)(((p3) << 4) | (p2)), \
    (uint8_t)(((p5) << 4) | (p4)), \
    (uint8_t)(((p7) << 4) | (p6))

static const uint8_t kGlyphTilesXYLR[4][32] = {
    /* X */ {
        ROW(R,R,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,Y,R),
        ROW(R,R,Y,R,R,Y,R,R),
        ROW(R,R,R,Y,Y,R,R,R),
        ROW(R,R,R,Y,Y,R,R,R),
        ROW(R,R,Y,R,R,Y,R,R),
        ROW(R,Y,R,R,R,R,Y,R),
        ROW(R,R,R,R,R,R,R,R),
    },
    /* Y */ {
        ROW(R,R,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,Y,R),
        ROW(R,R,Y,R,R,Y,R,R),
        ROW(R,R,R,Y,Y,R,R,R),
        ROW(R,R,R,R,Y,R,R,R),
        ROW(R,R,R,R,Y,R,R,R),
        ROW(R,R,R,R,Y,R,R,R),
        ROW(R,R,R,R,R,R,R,R),
    },
    /* L */ {
        ROW(R,R,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,R,R),
        ROW(R,Y,R,R,R,R,R,R),
        ROW(R,Y,Y,Y,Y,Y,Y,R),
        ROW(R,R,R,R,R,R,R,R),
    },
    /* R */ {
        ROW(R,R,R,R,R,R,R,R),
        ROW(R,Y,Y,Y,Y,R,R,R),
        ROW(R,Y,R,R,R,Y,R,R),
        ROW(R,Y,Y,Y,Y,R,R,R),
        ROW(R,Y,R,Y,R,R,R,R),
        ROW(R,Y,R,R,Y,R,R,R),
        ROW(R,Y,R,R,R,Y,R,R),
        ROW(R,R,R,R,R,R,R,R),
    },
};

#undef R
#undef Y
#undef _
#undef ROW

/* Sprite VRAM destination tile index. 1024 tiles available in 4bpp
 * mode; we sit at 0x3FC..0x3FF (the last four) on the assumption that
 * the engine's pause-menu UI sprites don't use the very top of sprite
 * VRAM. We re-inject every frame the badges render so a transient
 * eviction by another sprite loader self-corrects on the next frame. */
#define PORT_SOFTSLOT_GLYPH_BASE_TILE 0x3FC

/* OAM slots — picked from the high end of the 128-slot OAM array on
 * the assumption the engine's pause-menu OAM dispatch fills from
 * slot 0 upward. Same self-correction story as VRAM: we push every
 * frame so transient overlap with other consumers doesn't stick. */
#define PORT_SOFTSLOT_OAM_SLOT 124

/* Forward decls for the GBA video memory the port owns. Declared here
 * to avoid pulling port_gba_mem.h's ABI assumptions into other parts
 * of this file. */
extern uint8_t  gVram[];
extern uint16_t gOamMem[];

static void InjectGlyphTilesToVRAM(void) {
    /* Sprite tile data lives in VRAM 0x06010000+. In the port that's
     * gVram offset 0x10000. Tiles are 32 bytes each in 4bpp mode. */
    uint8_t* sprite_vram = gVram + 0x10000;
    for (int i = 0; i < 4; i++) {
        memcpy(sprite_vram + (PORT_SOFTSLOT_GLYPH_BASE_TILE + i) * 32,
               kGlyphTilesXYLR[i], 32);
    }
}

void Port_SoftSlots_PushBadge(int s, int icon_x, int icon_y) {
    if (s < 0 || s >= 4) return;

    /* Lazily refresh the tiles every time we draw — robust against
     * the engine reloading sprite VRAM between frames. */
    InjectGlyphTilesToVRAM();

    /* Position offsets: place the four badges in distinct corners of
     * the item icon so they don't stack on top of each other or on
     * the engine's existing A/B indicator (which uses the icon's
     * upper-right corner). */
    int dx, dy;
    switch (s) {
        case 0: dx =  0; dy =  0; break; /* X — upper-left */
        case 1: dx =  0; dy =  8; break; /* Y — lower-left */
        case 2: dx =  8; dy =  8; break; /* L — lower-right */
        case 3: dx =  8; dy =  0; break; /* R — upper-right (may overlap A) */
    }

    int slot = PORT_SOFTSLOT_OAM_SLOT + s;
    if (slot >= 128) return;

    int x = icon_x + dx;
    int y = icon_y + dy;

    /* GBA OAM attributes:
     *   attr0 bits  0-7  : y (8 bits)
     *   attr0 bits  8-9  : OBJ mode (00 = normal)
     *   attr0 bits 10-11 : effects (00 = none)
     *   attr0 bit   12   : mosaic
     *   attr0 bit   13   : 16-color (0) / 256-color (1)
     *   attr0 bits 14-15 : shape (00 = square)
     *
     *   attr1 bits  0-8  : x (9 bits)
     *   attr1 bits 14-15 : size (00 = 8x8 with shape=square)
     *
     *   attr2 bits  0-9  : tile number
     *   attr2 bits 10-11 : priority (lower = drawn over higher)
     *   attr2 bits 12-15 : palette
     */
    uint16_t attr0 = (uint16_t)(y & 0xFF);
    uint16_t attr1 = (uint16_t)(x & 0x1FF);
    uint16_t attr2 = (uint16_t)((PORT_SOFTSLOT_GLYPH_BASE_TILE + s) & 0x3FF);
    /* Palette 0 — same the existing A/B indicator sprite uses; gives us
     * the same red/yellow colour scheme. Priority 0 so we draw over
     * the item icon and the cursor brackets. */

    gOamMem[slot * 4 + 0] = attr0;
    gOamMem[slot * 4 + 1] = attr1;
    gOamMem[slot * 4 + 2] = attr2;
    /* attr3 (gOamMem[*4 + 3]) is the affine padding; leave alone. */
}
#endif /* disabled — see comment near top of this section */

static void RenderConfigOverlay(SDL_Renderer* r, int winW, int winH) {
    /* Centered modal: 4 stacked rows, one per slot, with arrow hints. */
    const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    const float rowH = 28.0f;
    const float rowGap = 4.0f;
    const float boxW = 360.0f;
    const float boxH = 60.0f + rowH * 4 + rowGap * 3;
    SDL_FRect box = { (winW - boxW) * 0.5f, (winH - boxH) * 0.5f, boxW, boxH };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 230);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_RenderRect(r, &box);

    /* Title */
    const char* title = "EXTRA EQUIP SLOTS";
    int titleLen = (int)SDL_strlen(title);
    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_RenderDebugText(r, box.x + (box.w - titleLen * charW) * 0.5f,
                        box.y + 8.0f, title);

    float y = box.y + 30.0f;
    for (int i = 0; i < PORT_SOFTSLOT_COUNT; i++) {
        bool sel = (i == sConfigCursor);
        SDL_FRect row = { box.x + 12.0f, y, box.w - 24.0f, rowH };

        SDL_SetRenderDrawColor(r, sel ? 60 : 30, sel ? 60 : 30, sel ? 80 : 40, 220);
        SDL_RenderFillRect(r, &row);
        SDL_SetRenderDrawColor(r, sel ? 255 : 140, sel ? 240 : 140, sel ? 64 : 160, 255);
        SDL_RenderRect(r, &row);

        char left[8];
        SDL_snprintf(left, sizeof(left), "[%s]", Port_SoftSlots_SlotName(i));
        SDL_RenderDebugText(r, row.x + 8.0f, row.y + 9.0f, left);

        const char* lArrow = sel ? "<" : " ";
        const char* rArrow = sel ? ">" : " ";
        SDL_RenderDebugText(r, row.x + 60.0f, row.y + 9.0f, lArrow);
        const char* val = SlotValueLabel(i);
        SDL_RenderDebugText(r, row.x + 80.0f, row.y + 9.0f, val);
        SDL_RenderDebugText(r, row.x + row.w - 16.0f, row.y + 9.0f, rArrow);

        y += rowH + rowGap;
    }

    /* Footer hint */
    SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
    const char* foot = "Up/Down pick  Left/Right cycle  Enter/Esc done";
    int footLen = (int)SDL_strlen(foot);
    SDL_RenderDebugText(r, box.x + (box.w - footLen * charW) * 0.5f,
                        box.y + box.h - 18.0f, foot);
}

void Port_SoftSlots_RenderOverlay(void* sdl_renderer, int winW, int winH) {
    SDL_Renderer* r = (SDL_Renderer*)sdl_renderer;
    if (!r) return;
    if (sConfigOpen) {
        RenderConfigOverlay(r, winW, winH);
    }
}
