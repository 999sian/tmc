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
void Port_DebugAction_AllKinstones(void);
int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                          unsigned short x, unsigned short y,
                          unsigned char layer);
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
};

struct MenuPage {
    std::string title;
    std::vector<MenuItem> items;
    int cursor = 0;
};

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
    p.items.push_back({ "Hyrule Field - Link's house",  []() { DoWarp(AREA_HYRULE_FIELD, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Minish Woods",                 []() { DoWarp(AREA_MINISH_WOODS, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Minish Village",               []() { DoWarp(AREA_MINISH_VILLAGE, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Mt Crenel",                    []() { DoWarp(AREA_MT_CRENEL,    0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Melari's Mines",               []() { DoWarp(AREA_MELARIS_MINE, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Deepwood Shrine",              []() { DoWarp(AREA_DEEPWOOD_SHRINE,    0x0B, 0xa8, 0xb8, 1); } });
    p.items.push_back({ "Deepwood Shrine - boss",       []() { DoWarp(AREA_DEEPWOOD_SHRINE_BOSS, 0x00, 0x80, 0x80, 1); } });
    p.items.push_back({ "Cave of Flames",               []() { DoWarp(AREA_CAVE_OF_FLAMES,     0x04, 0x98, 0xa8, 1); } });
    /* Room 0x08 = Rollobite lava room (#36 — moving lava platforms).
     * Local coords come from the bug report's world (610, 3578) minus the
     * room origin (336, 3200) recorded in area_room_headers.json. */
    p.items.push_back({ "Cave of Flames - Rollobite",   []() { DoWarp(AREA_CAVE_OF_FLAMES,     0x08, 0x112, 0x17A, 1); } });
    p.items.push_back({ "Cave of Flames - boss",        []() { DoWarp(AREA_CAVE_OF_FLAMES_BOSS, 0x00, 0x80, 0x80, 1); } });
    p.items.push_back({ "Fortress of Winds",            []() { DoWarp(AREA_FORTRESS_OF_WINDS,  0x21, 0x78, 0xa8, 1); } });
    p.items.push_back({ "Temple of Droplets",           []() { DoWarp(AREA_TEMPLE_OF_DROPLETS, 0x03, 0x108, 0xf8, 1); } });
    p.items.push_back({ "Royal Crypt",                  []() { DoWarp(AREA_ROYAL_CRYPT,        0x08, 0x88, 0x78, 1); } });
    p.items.push_back({ "Palace of Winds",              []() { DoWarp(AREA_PALACE_OF_WINDS,    0x31, 0x238, 0x58, 1); } });
    p.items.push_back({ "<- Back",                      []() { Pop(); } });
    return p;
}

MenuPage BuildMainPage(void) {
    MenuPage p;
    p.title = "DEBUG MENU (F8 to close)";
    p.items.push_back({ "Items / progress",  []() { Push(BuildItemsPage()); } });
    p.items.push_back({ "Warp",              []() { Push(BuildWarpPage());  } });
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

        switch (sdlKey) {
            case SDLK_UP:
                if (n > 0) {
                    page.cursor = (page.cursor - 1 + n) % n;
                }
                consumed = true;
                break;
            case SDLK_DOWN:
                if (n > 0) {
                    page.cursor = (page.cursor + 1) % n;
                }
                consumed = true;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (page.cursor >= 0 && page.cursor < n) {
                    /* Copy the action so the std::function we're calling
                     * stays alive even if the page (and its items) get
                     * popped/cleared inside the lambda. */
                    auto fn = page.items[page.cursor].action;
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

    int rows = 2 + static_cast<int>(page.items.size()) + 2;
    int cols = static_cast<int>(page.title.size());
    for (const auto& it : page.items) {
        cols = std::max(cols, static_cast<int>(it.label.size()) + 4);
    }
    cols = std::max(cols, 30);

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
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, page.title.c_str());
    y += charW + 8.0f;

    for (size_t i = 0; i < page.items.size(); ++i) {
        bool sel = static_cast<int>(i) == page.cursor;
        std::string line = (sel ? "> " : "  ") + page.items[i].label;
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        }
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, line.c_str());
        y += charW + 4.0f;
    }

    y += 4.0f;
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, "Up/Down move  Enter select  Esc back");
    y += charW + 4.0f;
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, "F5 quicksave   F6 quickload");
}
