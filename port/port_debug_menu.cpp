/*
 * port_debug_menu.cpp — F8 in-game debug menu.
 *
 * Renders an SDL overlay using SDL_RenderDebugText. While open, all game
 * input is masked (Port_UpdateInput consults Port_DebugMenu_IsOpen) and
 * SDL key events are routed to the menu instead of the game.
 *
 * Pages are an array of items; each item is either a submenu pointer or
 * a callable action. Up/Down navigates, Enter activates, B/Esc backs out
 * (and closes the menu when at the top level).
 *
 * Game-state mutations live in port_debug_actions.c so this TU doesn't
 * need to include the game headers (which don't parse as C++ — they use
 * `this` as a parameter name).
 */

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "port_debug_menu.h"

extern "C" {
void Port_DebugAction_GiveAllItems(void);
void Port_DebugAction_MaxHearts(void);
void Port_DebugAction_HealFull(void);
void Port_DebugAction_MaxRupees(void);
void Port_DebugAction_MaxShells(void);
void Port_DebugAction_AllKinstones(void);
int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                          unsigned short x, unsigned short y,
                          unsigned char layer);
int Port_DebugQuery_AreaRoomCount(unsigned char area);
int Port_DebugQuery_RoomDimensions(unsigned char area, unsigned char room,
                                   unsigned short* w, unsigned short* h);
const char* Port_DebugQuery_AreaName(unsigned char area);

/* Display / runtime-config knobs — same set the file-select "L Settings"
 * panel exposes, so the F8 menu can drive them mid-game. */
void          Port_PPU_ToggleFullscreen(void);
bool          Port_PPU_IsFullscreen(void);
void          Port_PPU_CycleWindowScale(int direction);
unsigned char Port_PPU_WindowScale(void);
void          Port_PPU_CyclePresentationMode(int direction);
const char*   Port_PPU_PresentationModeName(void);
unsigned int  Port_Config_TargetFps(void);
void          Port_Config_CycleTargetFps(int direction);
unsigned char Port_Config_InternalScale(void);
void          Port_Config_CycleInternalScale(int direction);
}

namespace {

/* Mirror a few enum values from include/area.h here so the menu doesn't
 * pull in the game headers. Update if these area indices ever change. */
constexpr unsigned char AREA_MINISH_WOODS              = 0x00;
constexpr unsigned char AREA_MINISH_VILLAGE            = 0x01;
constexpr unsigned char AREA_HYRULE_TOWN               = 0x02;
constexpr unsigned char AREA_HYRULE_FIELD              = 0x03;
constexpr unsigned char AREA_MT_CRENEL                 = 0x06;
constexpr unsigned char AREA_MELARIS_MINE              = 0x10;
constexpr unsigned char AREA_DEEPWOOD_SHRINE           = 0x48;
constexpr unsigned char AREA_DEEPWOOD_SHRINE_BOSS      = 0x49;
constexpr unsigned char AREA_DEEPWOOD_SHRINE_ENTRY     = 0x4A;
constexpr unsigned char AREA_CAVE_OF_FLAMES            = 0x50;
constexpr unsigned char AREA_CAVE_OF_FLAMES_BOSS       = 0x51;
constexpr unsigned char AREA_FORTRESS_OF_WINDS         = 0x58;
constexpr unsigned char AREA_TEMPLE_OF_DROPLETS        = 0x60;
constexpr unsigned char AREA_ROYAL_CRYPT               = 0x68;
constexpr unsigned char AREA_PALACE_OF_WINDS           = 0x70;

bool sOpen = false;

struct MenuItem {
    std::string label;
    std::function<void()> action;
    /* Optional cycle handlers for value-toggle items (Display settings page).
     * When set, Left/Right invoke them and the renderer prefers labelFn over
     * the static label so the visible row updates with the current value. */
    std::function<void()> cycleLeft;
    std::function<void()> cycleRight;
    std::function<std::string()> labelFn;
};

struct MenuPage {
    std::string title;
    std::vector<MenuItem> items;
    int cursor = 0;
    /* Viewport: index of the topmost visible item. Renderer + key handler
     * together keep `cursor` inside [viewportTop, viewportTop + visible). */
    int viewportTop = 0;
};

/* Maximum number of items shown at once on a page. Larger pages scroll —
 * cursor still walks every item, but only a window of this many is drawn. */
constexpr int kVisibleItemsMax = 18;

std::vector<MenuPage> sPageStack;
std::string sToast;            /* Temporary message shown at bottom of screen. */
unsigned int sToastUntilTicks = 0;

/* Items in sPageStack store std::function lambdas. Clearing the stack
 * inside one of those lambdas would destroy the std::function whose body
 * is currently executing — even though the executing copy is a local,
 * the implementation is fragile enough that doing it has been blamed for
 * a crash on "Close menu". Defer the actual stack clear/pop to the
 * top-level HandleKey caller via these flags. */
int sPendingPops = 0;
bool sPendingClose = false;

void Toast(const std::string& msg) {
    sToast = msg;
    sToastUntilTicks = SDL_GetTicks() + 1500;
}

/* ------- Page builders (forward-declared so actions can push pages) ------- */
MenuPage BuildItemsPage(void);
MenuPage BuildWarpPage(void);
MenuPage BuildAllAreasPage(void);
MenuPage BuildAreaRoomsPage(unsigned char area);
MenuPage BuildDisplaySettingsPage(void);
MenuPage BuildMainPage(void);

void Push(MenuPage page) {
    sPageStack.push_back(std::move(page));
}

void Pop(void) {
    /* Deferred — see sPendingPops/sPendingClose. The actual stack mutation
     * happens after the calling lambda has returned. */
    if (static_cast<int>(sPageStack.size()) - sPendingPops <= 1) {
        sPendingClose = true;
    } else {
        ++sPendingPops;
    }
}

void ApplyPendingMutations(void) {
    if (sPendingClose) {
        sPendingClose = false;
        sPendingPops = 0;
        sOpen = false;
        sPageStack.clear();
        return;
    }
    while (sPendingPops > 0 && !sPageStack.empty()) {
        sPageStack.pop_back();
        --sPendingPops;
    }
    sPendingPops = 0;
}

void DoWarp(unsigned char area, unsigned char room,
            unsigned short x = 0x80, unsigned short y = 0x80,
            unsigned char layer = 0) {
    if (!Port_DebugAction_Warp(area, room, x, y, layer)) {
        Toast("Warp ignored: not in gameplay");
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Warp -> area 0x%02X room 0x%02X", area, room);
    Toast(buf);
    sOpen = false;
    sPageStack.clear();
}

MenuPage BuildItemsPage(void) {
    MenuPage p;
    p.title = "ITEMS";
    p.items.push_back({ "Unlock all items",      []() { Port_DebugAction_GiveAllItems(); Toast("All items granted"); } });
    p.items.push_back({ "Max heart containers",  []() { Port_DebugAction_MaxHearts();    Toast("Hearts maxed");      } });
    p.items.push_back({ "Heal to full",          []() { Port_DebugAction_HealFull();     Toast("Healed");            } });
    p.items.push_back({ "999 rupees",            []() { Port_DebugAction_MaxRupees();    Toast("999 rupees");        } });
    p.items.push_back({ "999 mysterious shells", []() { Port_DebugAction_MaxShells();    Toast("999 shells");        } });
    p.items.push_back({ "All kinstones fused",   []() { Port_DebugAction_AllKinstones(); Toast("All kinstones");     } });
    p.items.push_back({ "<- Back",               []() { Pop(); } });
    return p;
}

MenuPage BuildWarpPage(void) {
    MenuPage p;
    p.title = "WARP";
    /* Dungeon entries are lifted verbatim from src/data/screenTransitions.c
     * (the Wallmaster screen-transitions table, gWallMasterScreenTransitions)
     * — area, room, endX, endY, layer — so the warp goes through DoExitTransition
     * exactly the way a wallmaster pickup does. Layer=1 across all dungeons. */
    p.items.push_back({ "Hyrule Town",                  []() { DoWarp(AREA_HYRULE_TOWN, 0x00, 0x80, 0xC0, 1); } });
    /* #65 fix: Link's house lives in SOUTH_HYRULE_FIELD (room 0x01),
     * not Western_Woods_South (room 0x00). Local coords come from the
     * exit list in src/data/transitions.c (gExitList_HouseInteriors2_-
     * LinksHouseEntrance: WARP_TYPE_BORDER -> 0x290, 0x19c). */
    p.items.push_back({ "Hyrule Field - Link's house",  []() { DoWarp(AREA_HYRULE_FIELD, 0x01, 0x290, 0x19C, 1); } });
    p.items.push_back({ "Minish Woods",                 []() { DoWarp(AREA_MINISH_WOODS, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Minish Village",               []() { DoWarp(AREA_MINISH_VILLAGE, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Mt Crenel",                    []() { DoWarp(AREA_MT_CRENEL,    0x00, 0x80, 0xC0, 1); } });
    /* Spawn at Mountain Minish 4's coordinates from gUnk_additional_9 — a
     * known-walkable spot near the room's left side (#42/#43 repro). */
    p.items.push_back({ "Melari's Mines",               []() { DoWarp(AREA_MELARIS_MINE, 0x00, 0x80, 0x130, 1); } });
    p.items.push_back({ "Deepwood Shrine",              []() { DoWarp(AREA_DEEPWOOD_SHRINE,    0x0B, 0xa8, 0xb8, 1); } });
    /* Boss-room coords match the canonical entry transitions in
     * src/data/transitions.c / src/manager/holeManager.c rather than the
     * placeholder (0x80, 0x80) that left Link off-camera or invisible.
     * Layer matches what the room map expects (CoF boss is a hole drop
     * onto layer 2). */
    p.items.push_back({ "Deepwood Shrine - boss",       []() { DoWarp(AREA_DEEPWOOD_SHRINE_BOSS, 0x00, 0x88, 0xD8, 1); } });
    p.items.push_back({ "Cave of Flames",               []() { DoWarp(AREA_CAVE_OF_FLAMES,     0x04, 0x98, 0xa8, 1); } });
    /* Room 0x08 = Rollobite lava room (#36 — moving lava platforms).
     * Local coords come from the captured world position (610, 3578) minus the
     * room origin (336, 3200) recorded in area_room_headers.json. */
    p.items.push_back({ "Cave of Flames - Rollobite",   []() { DoWarp(AREA_CAVE_OF_FLAMES,     0x08, 0x112, 0x17A, 1); } });
    p.items.push_back({ "Cave of Flames - boss",        []() { DoWarp(AREA_CAVE_OF_FLAMES_BOSS, 0x00, 0xC0, 0xF8, 2); } });
    p.items.push_back({ "Fortress of Winds",            []() { DoWarp(AREA_FORTRESS_OF_WINDS,  0x21, 0x78, 0xa8, 1); } });
    p.items.push_back({ "Temple of Droplets",           []() { DoWarp(AREA_TEMPLE_OF_DROPLETS, 0x03, 0x108, 0xf8, 1); } });
    p.items.push_back({ "Royal Crypt",                  []() { DoWarp(AREA_ROYAL_CRYPT,        0x08, 0x88, 0x78, 1); } });
    p.items.push_back({ "Palace of Winds",              []() { DoWarp(AREA_PALACE_OF_WINDS,    0x31, 0x238, 0x58, 1); } });
    /* #58 repro: bakery rafters at the reporter's exact spot. World pos
     * (1864, 117); room 3 origin map_x=0x60 << 4 = 0x600 → local (0x148, 0x75).
     * Area + room constants hardcoded — not yet mirrored above. */
    p.items.push_back({ "MinishRafters Bakery (#58 repro)",
                        []() { DoWarp(0x2E, 0x03, 0x148, 0x75, 1); } });
    /* #57 repro: Carlov's figurine shop. Area 0x23 = HouseInteriors3,
     * room 7 = Carlov, room header (0x00, 0x0E, 0xF0, 0xA0) → local centre
     * (0x78, 0x50). Walk into the device + insert shells to draw. */
    p.items.push_back({ "Carlov figurine shop (#57 repro)",
                        []() { DoWarp(0x23, 0x07, 0x78, 0x50, 1); } });
    p.items.push_back({ "All areas (raw, by index) ->", []() { Push(BuildAllAreasPage()); } });
    p.items.push_back({ "<- Back",                      []() { Pop(); } });
    return p;
}

/* Iterate every area slot and add an entry per area that has at least one
 * mapped room. The room headers come from the asset pipeline, so areas
 * with no extracted data (NULL_xx slots in include/area.h) won't appear.
 * Selecting an area pushes a per-area submenu listing its rooms. */
MenuPage BuildAllAreasPage(void) {
    MenuPage p;
    p.title = "WARP - all areas";
    for (unsigned int area = 0; area < 0x90; ++area) {
        unsigned char a = static_cast<unsigned char>(area);
        int count = Port_DebugQuery_AreaRoomCount(a);
        if (count <= 0) {
            continue;
        }
        const char* name = Port_DebugQuery_AreaName(a);
        char buf[80];
        if (name) {
            std::snprintf(buf, sizeof(buf), "0x%02X %s (%d)", area, name, count);
        } else {
            std::snprintf(buf, sizeof(buf), "0x%02X Area (%d rooms)", area, count);
        }
        p.items.push_back({ buf, [a]() { Push(BuildAreaRoomsPage(a)); } });
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Per-area room list. Each entry warps to the room with x = pixel_width/2,
 * y = pixel_height/2 — geometric centre. Not guaranteed walkable (could
 * spawn inside an obstacle) but good enough for debug; if you land on a
 * wall, just F8 → warp again to a different room. Layer defaults to 1. */
MenuPage BuildAreaRoomsPage(unsigned char area) {
    MenuPage p;
    char title[48];
    std::snprintf(title, sizeof(title), "WARP - area 0x%02X rooms", area);
    p.title = title;
    int count = Port_DebugQuery_AreaRoomCount(area);
    for (int r = 0; r < count; ++r) {
        unsigned short w = 0, h = 0;
        if (!Port_DebugQuery_RoomDimensions(area, static_cast<unsigned char>(r), &w, &h)) {
            continue;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Room 0x%02X (%ux%u px)", r, w, h);
        unsigned char rr = static_cast<unsigned char>(r);
        unsigned short cx = static_cast<unsigned short>(w / 2);
        unsigned short cy = static_cast<unsigned short>(h / 2);
        p.items.push_back({ buf, [area, rr, cx, cy]() { DoWarp(area, rr, cx, cy, 1); } });
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

MenuPage BuildDisplaySettingsPage(void) {
    /* Mirrors the file-select "L Settings" panel (src/fileselect.c
     * HandlePortSettingsMenu): same four knobs, same Left/Right ergonomics,
     * but reachable mid-game via F8 instead of only on the title screen.
     * Each item has a labelFn that re-reads the current value every frame
     * so the row updates immediately as you cycle. */
    MenuPage p;
    p.title = "DISPLAY SETTINGS";

    MenuItem scale;
    scale.cycleLeft  = []() { Port_PPU_CycleWindowScale(-1); };
    scale.cycleRight = []() { Port_PPU_CycleWindowScale(+1); };
    scale.labelFn = []() {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Scale       %ux", (unsigned)Port_PPU_WindowScale());
        return std::string(buf);
    };
    p.items.push_back(std::move(scale));

    MenuItem filter;
    filter.cycleLeft  = []() { Port_PPU_CyclePresentationMode(-1); };
    filter.cycleRight = []() { Port_PPU_CyclePresentationMode(+1); };
    filter.labelFn = []() {
        const char* name = Port_PPU_PresentationModeName();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Filter      %s", name ? name : "?");
        return std::string(buf);
    };
    p.items.push_back(std::move(filter));

    MenuItem fps;
    fps.cycleLeft  = []() { Port_Config_CycleTargetFps(-1); };
    fps.cycleRight = []() { Port_Config_CycleTargetFps(+1); };
    fps.labelFn = []() {
        unsigned int v = Port_Config_TargetFps();
        char buf[32];
        if (v == 0) {
            std::snprintf(buf, sizeof(buf), "FPS         uncapped");
        } else {
            std::snprintf(buf, sizeof(buf), "FPS         %u", v);
        }
        return std::string(buf);
    };
    p.items.push_back(std::move(fps));

    MenuItem fs;
    /* Fullscreen is binary, so left/right both toggle. */
    fs.cycleLeft  = []() { Port_PPU_ToggleFullscreen(); };
    fs.cycleRight = []() { Port_PPU_ToggleFullscreen(); };
    fs.labelFn = []() {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Fullscreen  %s", Port_PPU_IsFullscreen() ? "on" : "off");
        return std::string(buf);
    };
    p.items.push_back(std::move(fs));

    MenuItem internalScale;
    internalScale.cycleLeft  = []() { Port_Config_CycleInternalScale(-1); };
    internalScale.cycleRight = []() { Port_Config_CycleInternalScale(+1); };
    internalScale.labelFn = []() {
        char buf[64];
        unsigned s = (unsigned)Port_Config_InternalScale();
        /* Affine OAM is sub-pixel at scale > 1; everything else is S*S
         * replicate. Affine BG2 / mode 7 are still TODO. */
        std::snprintf(buf, sizeof(buf),
                      s == 1 ? "Internal    %ux  (off)"
                             : "Internal    %ux  (affine OBJ sub-pixel)",
                      s);
        return std::string(buf);
    };
    p.items.push_back(std::move(internalScale));

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

MenuPage BuildMainPage(void) {
    MenuPage p;
    p.title = "DEBUG MENU (F8 to close)";
    p.items.push_back({ "Items / progress",  []() { Push(BuildItemsPage()); } });
    p.items.push_back({ "Warp",              []() { Push(BuildWarpPage());  } });
    p.items.push_back({ "Display settings",  []() { Push(BuildDisplaySettingsPage()); } });
    p.items.push_back({ "Heal to full",      []() { Port_DebugAction_HealFull(); Toast("Healed"); } });
    p.items.push_back({ "Close menu",        []() { Pop(); } });
    return p;
}

} /* namespace */

/* ============================================================ */
/*                          Public API                          */
/* ============================================================ */

extern "C" void Port_DebugMenu_Toggle(void) {
    if (sOpen) {
        sOpen = false;
        sPageStack.clear();
    } else {
        sOpen = true;
        sPageStack.clear();
        sPageStack.push_back(BuildMainPage());
    }
}

extern "C" bool Port_DebugMenu_IsOpen(void) {
    return sOpen;
}

extern "C" bool Port_DebugMenu_HandleKey(int sdlKey) {
    if (!sOpen || sPageStack.empty()) {
        return false;
    }
    bool consumed = false;
    {
        MenuPage& page = sPageStack.back();
        int n = static_cast<int>(page.items.size());

        auto clampViewport = [&]() {
            int visible = std::min(n, kVisibleItemsMax);
            if (page.cursor < page.viewportTop) {
                page.viewportTop = page.cursor;
            } else if (page.cursor >= page.viewportTop + visible) {
                page.viewportTop = page.cursor - visible + 1;
            }
            if (page.viewportTop < 0) {
                page.viewportTop = 0;
            }
            if (page.viewportTop + visible > n) {
                page.viewportTop = std::max(0, n - visible);
            }
        };

        switch (sdlKey) {
            case SDLK_UP:
                if (n > 0) {
                    page.cursor = (page.cursor - 1 + n) % n;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_DOWN:
                if (n > 0) {
                    page.cursor = (page.cursor + 1) % n;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_PAGEUP:
                if (n > 0) {
                    page.cursor = std::max(0, page.cursor - kVisibleItemsMax);
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_PAGEDOWN:
                if (n > 0) {
                    page.cursor = std::min(n - 1, page.cursor + kVisibleItemsMax);
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_HOME:
                if (n > 0) {
                    page.cursor = 0;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_END:
                if (n > 0) {
                    page.cursor = n - 1;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (page.cursor >= 0 && page.cursor < n) {
                    /* Copy the function so the std::function we're calling
                     * stays alive even if the page (and its items) get
                     * popped/cleared inside the lambda. For cycle items,
                     * Enter behaves like Right (forward cycle). */
                    auto& it = page.items[page.cursor];
                    auto fn = it.action ? it.action : it.cycleRight;
                    if (fn) fn();
                }
                consumed = true;
                break;
            case SDLK_LEFT:
                if (page.cursor >= 0 && page.cursor < n) {
                    auto fn = page.items[page.cursor].cycleLeft;
                    if (fn) fn();
                }
                consumed = true;
                break;
            case SDLK_RIGHT:
                if (page.cursor >= 0 && page.cursor < n) {
                    auto fn = page.items[page.cursor].cycleRight;
                    if (fn) fn();
                }
                consumed = true;
                break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                Pop();
                consumed = true;
                break;
            default:
                break;
        }
        /* `page` reference must not be used after this scope ends — the
         * pending-mutation step below may invalidate it. */
    }
    ApplyPendingMutations();
    return consumed;
}

extern "C" void Port_DebugMenu_Render(SDL_Renderer* renderer, int winW, int winH) {
    if (!renderer) {
        return;
    }

    /* Toast: visible whether menu is open or not, e.g. after a warp. */
    if (!sToast.empty() && SDL_GetTicks() < sToastUntilTicks) {
        const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        int textW = static_cast<int>(sToast.size()) * charW;
        SDL_FRect bg = { (winW - textW) * 0.5f - 6.0f, winH - 28.0f, static_cast<float>(textW) + 12.0f, 18.0f };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        SDL_RenderDebugText(renderer, bg.x + 6.0f, bg.y + 5.0f, sToast.c_str());
    }

    if (!sOpen || sPageStack.empty()) {
        return;
    }

    const MenuPage& page = sPageStack.back();
    const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

    /* Scroll viewport: clamp to a window of kVisibleItemsMax items. The
     * key handler keeps page.cursor inside [viewportTop, viewportTop + visible). */
    const int total = static_cast<int>(page.items.size());
    const int visible = std::min(total, kVisibleItemsMax);
    int top = page.viewportTop;
    if (top < 0) {
        top = 0;
    }
    if (top + visible > total) {
        top = std::max(0, total - visible);
    }
    const bool moreAbove = top > 0;
    const bool moreBelow = (top + visible) < total;

    /* Materialize each visible label up-front: cycle items reconstruct a
     * fresh string from labelFn() each frame, and we need the same value
     * for both column-width sizing and rendering below. */
    std::vector<std::string> visibleLabels;
    visibleLabels.reserve(static_cast<size_t>(visible));
    for (int i = top; i < top + visible && i < total; ++i) {
        const MenuItem& it = page.items[i];
        visibleLabels.push_back(it.labelFn ? it.labelFn() : it.label);
    }

    /* Reserve up to 4 extra rows for: title, "..." above, "..." below,
     * blank, and 2 hint lines at the bottom. */
    int rows = 2 + visible + (moreAbove ? 1 : 0) + (moreBelow ? 1 : 0) + 3;
    int cols = static_cast<int>(page.title.size());
    for (const auto& lbl : visibleLabels) {
        cols = std::max(cols, static_cast<int>(lbl.size()) + 4);
    }
    cols = std::max(cols, 36);

    float boxW = static_cast<float>(cols * charW + 16);
    float boxH = static_cast<float>(rows * (charW + 4) + 12);
    SDL_FRect box = { (winW - boxW) * 0.5f, (winH - boxH) * 0.5f, boxW, boxH };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderRect(renderer, &box);

    float y = box.y + 8.0f;
    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
    char titleBuf[160];
    if (total > kVisibleItemsMax) {
        std::snprintf(titleBuf, sizeof(titleBuf), "%s  [%d/%d]",
                      page.title.c_str(), page.cursor + 1, total);
    } else {
        std::snprintf(titleBuf, sizeof(titleBuf), "%s", page.title.c_str());
    }
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, titleBuf);
    y += charW + 8.0f;

    if (moreAbove) {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, "  ^ ^ ^");
        y += charW + 4.0f;
    }

    for (int i = top; i < top + visible && i < total; ++i) {
        bool sel = i == page.cursor;
        const std::string& lbl = visibleLabels[static_cast<size_t>(i - top)];
        std::string line = (sel ? "> " : "  ") + lbl;
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        }
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, line.c_str());
        y += charW + 4.0f;
    }

    if (moreBelow) {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, "  v v v");
        y += charW + 4.0f;
    }

    y += 4.0f;
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, "Up/Dn move  PgUp/PgDn page  Home/End ends");
    y += charW + 4.0f;
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, "Enter select  L/R cycle  Esc back  F5/F6 save/load");
}
