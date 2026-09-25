/*
 * port/port_repro_rando.c — headless end-to-end randomizer check (TMC_REPRO_RANDO=1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "rando/rando.h"
#include "rando/rando_file_menu.h"
#include "rando/rando_logic.h"
#include "port_debug_actions.h"
#include "room.h"
#include "rando/rando_save.h"
#include "rando/rando_runtime.h"
#include "area.h"
#include "save.h"
#include "flags.h"
#include "game.h"
#include "message.h"
#include "player.h"
#include "item_ids.h"
#include "item.h"
#include "main.h"
#include "port_gba_mem.h"
#include "port_imgui_menu.h"
#include "port_runtime_config.h"
#include "port_softslots.h"
#include "port_repro.h"
#include "port_region_data.h"

extern bool Rando_OverrideLocationKey(uint32_t location_key, unsigned char* type, unsigned char* subtype);
extern void*** gAreaTable[];
extern void sub_0804AFB0(void** properties);
extern u32 sub_unk3_HyruleTown_0(void);
extern void Port_FileSelectRando_StartSlot(int slot);

typedef struct LogicAwardSnapshot {
    uint32_t key;
    u8 item;
    u8 subtype;
} LogicAwardSnapshot;

static LogicAwardSnapshot sFirstAwards[RANDO_LOGIC_MAX_LOCATIONS];
static LogicAwardSnapshot sLaterAwards[RANDO_LOGIC_MAX_LOCATIONS];

static int is_shuffled_award_type(RandoLogicLocationType type) {
    return type == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE || type == RANDO_LOGIC_LOCATION_MAJOR ||
           type == RANDO_LOGIC_LOCATION_DUNGEON || type == RANDO_LOGIC_LOCATION_ANY ||
           type == RANDO_LOGIC_LOCATION_MINOR;
}

static int capture_logic_awards(LogicAwardSnapshot* out, unsigned* out_count) {
    if (!Rando_IsActive() || !Rando_IsLogicSeed() || !RandoLogic_IsLoaded() || Rando_GetLogicFingerprint() == 0) {
        fprintf(stderr, "[rando-repro] FAIL: parser-mode seed not active\n");
        return 0;
    }
    unsigned count = 0;
    for (uint32_t i = 0; i < RandoLogic_GetLocationCountRaw(); ++i) {
        if (!is_shuffled_award_type(RandoLogic_GetLocationType(i)))
            continue;
        uint32_t key = RandoLogic_GetLocationKeyAt(i);
        u8 item = ITEM_NONE;
        u8 subtype = 0;
        if (key == UINT32_MAX || RandoLogic_FindLocationByKey(key) != (int)i ||
            !Rando_OverrideLocationKey(key, &item, &subtype) || item == ITEM_NONE ||
            count >= RANDO_LOGIC_MAX_LOCATIONS) {
            fprintf(stderr, "[rando-repro] FAIL: no native award for %s\n", RandoLogic_GetLocationName(i));
            return 0;
        }
        out[count++] = (LogicAwardSnapshot){ key, item, subtype };
    }
    *out_count = count;
    return 1;
}

static int same_logic_awards(const LogicAwardSnapshot* expected, unsigned expected_count) {
    unsigned count = 0;
    if (!capture_logic_awards(sLaterAwards, &count) || count != expected_count)
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        if (sLaterAwards[i].key != expected[i].key || sLaterAwards[i].item != expected[i].item ||
            sLaterAwards[i].subtype != expected[i].subtype) {
            fprintf(stderr, "[rando-repro] FAIL: placement changed at key %06X\n", expected[i].key);
            return 0;
        }
    }
    return 1;
}

static int run_menu_path(void) {
    Port_RandoFileMenu_Open(0);
    if (!Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay did not open\n");
        return 0;
    }

    Port_RandoFileMenu_SetSeed("12345");
    if (strcmp(Port_RandoFileMenu_SeedBuffer(), "12345") != 0) {
        fprintf(stderr, "[rando-repro] FAIL: menu seed edit was not retained\n");
        return 0;
    }
    Port_RandoFileMenu_Close();
    if (Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay still open after close\n");
        return 0;
    }
    fprintf(stderr, "[rando-repro] menu edit path OK\n");
    return 1;
}

static int run_logic_key_path(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    const uint64_t requested_seed = 0xABCDEFu;
    unsigned count = 0;
    if (!GenerateSeed(requested_seed, settings) || !capture_logic_awards(sFirstAwards, &count)) {
        fprintf(stderr, "[rando-repro] FAIL: fixed parser seed generation failed\n");
        return 0;
    }
#if defined(PC_PORT) && defined(MULTI_REGION)
    uint32_t falls_flag = Port_RemapBaselineLocalFlag(GetFlagBankOffset(0x0A), 0xAA);
    uint32_t falls_key = (0x0Au << 16) | falls_flag;
    int falls_location = RandoLogic_FindLocationByKey(falls_key);
    if (falls_location < 0 || strcmp(RandoLogic_GetLocationName((uint32_t)falls_location), "Falls_SouthDigSpot") != 0) {
        fprintf(stderr, "[rando-repro] FAIL: Falls dig spot key differs from the active ROM\n");
        return 0;
    }
#endif
    if (count != 259 || !Rando_VerifyCurrentSeed()) {
        fprintf(stderr, "[rando-repro] FAIL: fixed seed has %u keyed awards (expected 259) or failed verification\n",
                count);
        return 0;
    }
    uint64_t effective_seed = Rando_GetSeed64();
    uint64_t fingerprint = Rando_GetLogicFingerprint();
    if (!GenerateSeed(requested_seed, settings) || Rando_GetSeed64() != effective_seed ||
        Rando_GetLogicFingerprint() != fingerprint || !same_logic_awards(sFirstAwards, count)) {
        fprintf(stderr, "[rando-repro] FAIL: fixed seed is nondeterministic\n");
        return 0;
    }

    if (!Port_RandoSave_SaveActiveSlot(0)) {
        fprintf(stderr, "[rando-repro] FAIL: parser sidecar save failed\n");
        return 0;
    }
    Rando_Reset();
    if (!Port_RandoSave_LoadSlot(0) || Rando_GetSeed64() != effective_seed ||
        Rando_GetLogicFingerprint() != fingerprint || !same_logic_awards(sFirstAwards, count)) {
        fprintf(stderr, "[rando-repro] FAIL: parser sidecar reload changed placements\n");
        return 0;
    }
    fprintf(stderr, "[rando-repro] logic keys OK: %u native awards, deterministic seed %llu, sidecar reload\n",
            count, (unsigned long long)effective_seed);
    return 1;
}

static int run_world_open_test(void) {
    static const struct {
        u16 bank;
        u16 flag;
        const char* name;
    } pickup_flags[] = {
        { FLAG_BANK_1, MORI_00_H1, "Minish Woods heart piece" },
        { FLAG_BANK_3, MOGURA_51_00, "Fortress top pot" },
        { FLAG_BANK_3, MOGURA_51_01, "Fortress bottom pot" },
        { FLAG_BANK_8, LV4_0a_TSUBO, "Droplets right pot" },
        { FLAG_BANK_8, LV4_34_01, "Droplets underwater key" },
        { FLAG_BANK_1, KUMOUR_01_K0, "Cloud Tops dig 1" },
        { FLAG_BANK_1, KUMOUR_01_K1, "Cloud Tops dig 2" },
        { FLAG_BANK_1, KUMOUR_01_K2, "Cloud Tops dig 3" },
        { FLAG_BANK_1, KUMOUR_01_K3, "Cloud Tops dig 4" },
        { FLAG_BANK_1, YAMA_04_R00, "Crenel vine" },
        { FLAG_BANK_1, SOUGEN_06_R1, "North Field dig" },
        { FLAG_BANK_1, SOUGEN_05_R0, "Lon Lon dig" },
        { FLAG_BANK_1, HIKYOU_00_T1, "Swamp center chest" },
        { FLAG_BANK_1, KUMOUE_01_T4, "Cloud Tops northwest chest" },
        { FLAG_BANK_1, KUMOUE_01_T5, "Cloud Tops south left chest" },
        { FLAG_BANK_1, KUMOUE_01_T6, "Cloud Tops south right chest" },
    };
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.open_world = true;
    if (!GenerateSeed(0x5EEDu, settings) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: world-open generation failed\n");
        return 0;
    }
    memset(&gSave, 0, sizeof(gSave));
    Rando_Runtime_OnNewFile();
    SaveFile new_file_save = gSave;
    int ok = 1;
    if (!CheckGlobalFlag(START) || !CheckGlobalFlag(EZERO_1ST) || !CheckGlobalFlag(TABIDACHI)) {
        fprintf(stderr, "[rando-repro] FAIL: story-skip globals not set\n");
        ok = 0;
    }
    if (!CheckGlobalFlag(KUMOTATSUMAKI) || !CheckGlobalFlag(WARP_EVENT_END) ||
        !CheckGlobalFlag(TINGLE_TALK1ST) || !CheckGlobalFlag(MIZUKAKI_START)) {
        fprintf(stderr, "[rando-repro] FAIL: open-world speed-up flags not set\n");
        ok = 0;
    }
    if (GetInventoryValue(ITEM_SMITH_SWORD) == 0 ||
        (gSave.stats.equipped[SLOT_A] != ITEM_SMITH_SWORD && gSave.stats.equipped[SLOT_B] != ITEM_SMITH_SWORD)) {
        fprintf(stderr, "[rando-repro] FAIL: starting sword is not equipped\n");
        ok = 0;
    }
    for (size_t i = 0; i < sizeof(pickup_flags) / sizeof(pickup_flags[0]); ++i) {
        if (CheckLocalFlagByBankB(pickup_flags[i].bank, pickup_flags[i].flag)) {
            fprintf(stderr, "[rando-repro] FAIL: new file hides %s\n", pickup_flags[i].name);
            ok = 0;
        }
    }
    if (GetInventoryValue(ITEM_BOMBBAG) != 0 || GetInventoryValue(ITEM_BOMBS) != 0) {
        fprintf(stderr, "[rando-repro] FAIL: new file starts with a Bomb Bag or bombs\n");
        ok = 0;
    } else {
        GiveItem(ITEM_BOMBBAG, 0);
        if (GetInventoryValue(ITEM_BOMBS) == 0) {
            fprintf(stderr, "[rando-repro] FAIL: first Bomb Bag did not grant bombs\n");
            ok = 0;
        }
    }
    gSave = new_file_save;
    Rando_Reset();
    return ok;
}

static int run_story_skip_town_test(void) {
    void** previous = gCurrentRoomProperties;
    void** normal = gAreaTable[2][0];
    void* expected[4];
    int ok = 1;

    sub_0804AFB0(normal);
    for (int i = 0; i < 4; ++i)
        expected[i] = GetCurrentRoomProperty(i);
    sub_unk3_HyruleTown_0();
    for (int i = 0; i < 4; ++i) {
        if (GetCurrentRoomProperty(i) != expected[i]) {
            fprintf(stderr, "[rando-repro] FAIL: story-skipped town cached festival property %d\n", i);
            ok = 0;
            break;
        }
    }
    sub_0804AFB0(previous);
    return ok;
}

static int check_live_normal_town(void) {
    void** live = gCurrentRoomProperties;
    void** normal = gAreaTable[2][0];
    void* expected[4];
    int ok = 1;

    if (gRoomControls.area != 2 || gRoomControls.room != 0 || live == NULL ||
        !CheckGlobalFlag(TABIDACHI)) {
        fprintf(stderr, "[rando-repro] FAIL: live town entered wrong story/room state (area=%u room=%u)\n",
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        return 0;
    }
    sub_0804AFB0(normal);
    for (int i = 0; i < 4; ++i)
        expected[i] = GetCurrentRoomProperty(i);
    sub_0804AFB0(live);
    for (int i = 0; i < 4; ++i) {
        if (GetCurrentRoomProperty(i) != expected[i]) {
            fprintf(stderr, "[rando-repro] FAIL: live town loaded festival property %d\n", i);
            ok = 0;
            break;
        }
    }
    if (ok)
        fprintf(stderr, "[rando-repro] live Hyrule Town OK: normal properties with story skip\n");
    return ok;
}

static int run_real_logic_chest_probe(void) {
    static const struct {
        const char* name;
        u8 area;
        u8 room;
        u16 tile_pos;
        int raw_index; /* -1: locate by stable map tile (town room varies by region) */
    } samples[] = {
        { "Town_Inn_LedgeChest", 0x02, 0x00, 0x836, -1 },
        { "Town_School_Roof_Chest", 0x02, 0x00, 0x16D, -1 },
        { "Town_Shop_AtticChest", 0x2E, 0x01, 0, 1 },
    };
    RandomizerSettings s = Rando_DefaultSettings();
    if (!GenerateSeed(0xABCDu, s) || !Rando_IsLogicSeed()) {
        fprintf(stderr, "[rando-repro] FAIL: parser chest seed generation failed\n");
        return 0;
    }

    for (size_t n = 0; n < sizeof(samples) / sizeof(samples[0]); ++n) {
        const TileEntity* tiles = (const TileEntity*)GetRoomProperty(samples[n].area, samples[n].room, 3);
        const TileEntity* chest = NULL;
        int raw_index = -1;
        int location = -1;
        for (uint32_t i = 0; i < RandoLogic_GetLocationCountRaw(); ++i) {
            if (strcmp(RandoLogic_GetLocationName(i), samples[n].name) == 0) {
                location = (int)i;
                break;
            }
        }
        for (int i = 0; tiles != NULL && i < 256 && tiles[i].type != NONE; ++i) {
            if (tiles[i].type != SMALL_CHEST && tiles[i].type != BIG_CHEST)
                continue;
            if (samples[n].raw_index >= 0 ? i == samples[n].raw_index :
                                            tiles[i].tilePos == samples[n].tile_pos) {
                chest = &tiles[i];
                raw_index = i;
                break;
            }
        }
        int ordinal = chest != NULL ? Rando_RoomChestIndex(samples[n].area, samples[n].room, chest->localFlag) : -1;
        uint32_t key = ((uint32_t)samples[n].area << 16) | ((uint32_t)samples[n].room << 8) | (u32)ordinal;
        u8 item = chest != NULL ? chest->_2 : ITEM_NONE;
        u8 subtype = chest != NULL ? chest->_3 : 0;
        if (location < 0 || ordinal < 0 || raw_index == ordinal ||
            RandoLogic_GetLocationKeyAt((uint32_t)location) != key ||
            !Rando_OverrideLocationKey(key, &item, &subtype) || item == ITEM_NONE) {
            fprintf(stderr, "[rando-repro] FAIL: translated chest award %s\n", samples[n].name);
            return 0;
        }
        fprintf(stderr, "[rando-repro] chest %s raw=%d ordinal=%d -> item %02X\n",
                samples[n].name, raw_index, ordinal, item);
    }
    return 1;
}

static int is_baseline_award(uint32_t key) {
    for (unsigned i = 0; i < 259; ++i) {
        if (sFirstAwards[i].key == key)
            return 1;
    }
    return 0;
}

static int check_extension_flags(unsigned award_count, int open_world) {
    unsigned added = 0;
    unsigned hidden = 0;
    for (unsigned i = 0; i < award_count; ++i) {
        uint32_t key = sLaterAwards[i].key;
        if (is_baseline_award(key))
            continue;
        if (key & 0x80000000u) {
            fprintf(stderr, "[rando-repro] FAIL: unexpected scripted extension key %08X\n", key);
            return 0;
        }
        unsigned area = (key >> 16) & 0xff;
        unsigned flag = key & 0xff;
        if (ReadBit(gSave.flags, GetFlagBankOffset(area) + flag)) {
            int index = RandoLogic_FindLocationByKey(key);
            fprintf(stderr, "[rando-repro] FAIL: %s new file precollects %s (key %06X)\n",
                    open_world ? "open-world" : "normal", index >= 0 ? RandoLogic_GetLocationName((uint32_t)index) : "?", key);
            ++hidden;
        }
        ++added;
    }
    if (added != 93) {
        fprintf(stderr, "[rando-repro] FAIL: extension added %u locations, expected 93\n", added);
        return 0;
    }
    return hidden == 0;
}

static int run_obscure_extension_test(void) {
    static const struct {
        const char* name;
        u8 area;
        u8 room;
        u8 usa_flag;
        u8 eu_jp_flag;
        u8 object_id;
        u8 vanilla_item;
    } samples[] = {
        { "Crenel_Melari_LowerMiddle_Dig", 0x10, 0x00, 0xB9, 0, 0, 0x5C },
        { "Crenel_Melari_UpperMiddle_MiddleDig", 0x10, 0x00, 0xBE, 0, 0, 0x5C },
        { "Crenel_Melari_UpperTop_LeftDig", 0x10, 0x00, 0xBB, 0, 0, 0x5C },
        { "Crenel_Melari_VeryBottom_Dig", 0x10, 0x00, 0xC0, 0, 0, 0x5C },
        { "Hylia_SunkenHP", 0x0B, 0x00, 0x09, 0, 0, 0x63 },
        { "LonLon_RanchPot", 0x28, 0x02, 0x4D, 0, 5, 0x37 },
        { "Swamp_Underwater_Bottom", 0x04, 0x00, 0x22, 0x24, 0, 0x5C },
        { "Swamp_Underwater_Middle", 0x04, 0x00, 0x21, 0x23, 0, 0x5C },
        { "Swamp_Underwater_Top", 0x04, 0x00, 0x20, 0x22, 0, 0x5C },
        { "Town_Inn_Pot", 0x21, 0x09, 0x27, 0, 5, 0x5C },
    };
    RandomizerSettings settings = Rando_DefaultSettings();
    SaveFile original_save = gSave;
    unsigned count = 0;

    settings.obscure_locations = true;
    if (!GenerateSeed(0x0B5C0u, settings) || !capture_logic_awards(sLaterAwards, &count) || count != 352) {
        fprintf(stderr, "[rando-repro] FAIL: all optional pools have %u keyed awards (expected 352)\n", count);
        return 0;
    }
    for (size_t n = 0; n < sizeof(samples) / sizeof(samples[0]); ++n) {
        u8 flag = samples[n].eu_jp_flag != 0 && (REGION_IS_EU || REGION_IS_JP) ?
                  samples[n].eu_jp_flag : samples[n].usa_flag;
        uint32_t key = ((uint32_t)samples[n].area << 16) | ((uint32_t)samples[n].room << 8) | flag;
        int index = RandoLogic_FindLocationByKey(key);
        int rom_found = 0;
        for (unsigned prop = 0; prop < 3; ++prop) {
            const EntityData* list = (const EntityData*)Port_ResolveRegionData(
                GetRoomProperty(samples[n].area, samples[n].room, prop));
            for (unsigned j = 0; list != NULL && j < 256 && list[j].kind != 0xff; ++j) {
                if (list[j].id == samples[n].object_id && list[j].type == samples[n].vanilla_item &&
                    ((list[j].spritePtr >> 16) & 0xff) == flag &&
                    (samples[n].object_id != 5 || (list[j].type2 & 0xff) == 2)) {
                    rom_found = 1;
                    break;
                }
            }
        }
        u8 item = ITEM_NONE;
        u8 subtype = 0;
        if (index < 0 || strcmp(RandoLogic_GetLocationName((uint32_t)index), samples[n].name) != 0 ||
            !rom_found || !Rando_OverrideLocationKey(key, &item, &subtype) || item == ITEM_NONE) {
            fprintf(stderr, "[rando-repro] FAIL: optional pickup binding %s (key %06X, ROM=%d)\n",
                    samples[n].name, key, rom_found);
            return 0;
        }
    }
    for (int open_world = 0; open_world <= 1; ++open_world) {
        settings.open_world = open_world != 0;
        if (open_world && (!GenerateSeed(0x0B5C0u, settings) ||
                           !capture_logic_awards(sLaterAwards, &count) || count != 352)) {
            fprintf(stderr, "[rando-repro] FAIL: open-world optional pools changed award count (%u)\n", count);
            return 0;
        }
        memset(&gSave, 0, sizeof(gSave));
        Rando_Runtime_OnNewFile();
        if (!check_extension_flags(count, open_world))
            return 0;
    }
    gSave = original_save;
    Rando_Reset();
    RandoLogic_ClearOverrides();
    fprintf(stderr, "[rando-repro] optional pools OK: %u awards, 10 ROM bindings, 93 fresh pickups\n", count);
    return 1;
}

extern SDL_Window* Port_PPU_ActiveWindow(void);

static void commit_random_seed_from_menu(void) {
    Port_RandoFileMenu_CommitAndStart();
}

#define REPRO_IMGUI_OPEN_FRAME 240
#define REPRO_IMGUI_DEADLINE   480

static int imgui_keyboard_stage(unsigned int frame, int* done) {
    static int phase = 0;
    if (frame < REPRO_IMGUI_OPEN_FRAME) {
        return 1;
    }
    if (frame >= REPRO_IMGUI_DEADLINE) {
        fprintf(stderr, "[rando-repro] FAIL: ImGui keyboard stage timed out (frame %u)\n", frame);
        return 0;
    }
    switch (phase) {
    case 0:
        Port_RandoFileMenu_Open(0);
        Port_RandoFileMenu_SetSeed("");
        *Port_RandoFileMenu_ObscureLocations() = true;
        /* A new file must not inherit overrides loaded from another slot. */
        RandoLogic_SetOverride("START_PACCI", "true");
        phase = 1;
        break;
    case 1:
        if (Port_RandoFileMenu_IsModalOpen()) {
            commit_random_seed_from_menu();
            phase = 2;
        }
        break;
    case 2:
        if (!Port_RandoFileMenu_IsOpen()) {
            if (!Rando_IsActive() || Rando_GetSeed64() == 0) {
                fprintf(stderr, "[rando-repro] FAIL: menu commit did not generate a random seed\n");
                return 0;
            }
            SaveFile saved;
            if (ReadSaveFile(0, &saved) != 1 ||
                memcmp(saved.filler4ac, PC_RANDO_SAVE_MARKER, sizeof(PC_RANDO_SAVE_MARKER)) != 0) {
                fprintf(stderr, "[rando-repro] FAIL: menu commit did not persist a marked EEPROM slot\n");
                return 0;
            }
            uint64_t binding = 0;
            memcpy(&binding, saved.filler4ac + PC_RANDO_SAVE_BINDING_OFFSET, sizeof(binding));
            if (binding == 0 || binding != Port_RandoSave_ActiveBindingHash()) {
                fprintf(stderr, "[rando-repro] FAIL: menu commit saved the wrong sidecar binding\n");
                return 0;
            }
            unsigned award_count = 0;
            if (!capture_logic_awards(sLaterAwards, &award_count) || award_count != 352) {
                fprintf(stderr, "[rando-repro] FAIL: Obscure menu seed has %u awards (expected 352)\n",
                        award_count);
                return 0;
            }
            for (uint32_t i = 0; i < RandoLogic_GetOverrideCount(); ++i) {
                const char* name = NULL;
                if (RandoLogic_GetOverride(i, &name, NULL) && name != NULL && strcmp(name, "START_PACCI") == 0) {
                    fprintf(stderr, "[rando-repro] FAIL: menu seed inherited an old logic override\n");
                    return 0;
                }
            }
            fprintf(stderr, "[rando-repro] menu commit stage OK: seed=%llu\n",
                    (unsigned long long)Rando_GetSeed64());
            *done = 1;
            return 1;
        }
        break;
    }
    return 1;
}


#define REPRO_KEYINPUT_REG 0x130
#define REPRO_A_BUTTON 0x0001
#define REPRO_START_BUTTON 0x0008

static unsigned short sLatePressMask = 0;

void Port_ReproRando_LateTick(void) {
    if (sLatePressMask != 0) {
        uint16_t* io = (uint16_t*)gIoMem;
        io[REPRO_KEYINPUT_REG / 2] &= (uint16_t)~sLatePressMask;
    }
}

#define REPRO_HOMEWARP_PHASE_TIMEOUT 600

static int homewarp_stage(unsigned int frame, int* done) {
    static int phase = 0;
    static unsigned int armed_frame = 0;
    static unsigned int phase_started_frame = 0;
    static int last_phase = -1;

    if (phase != last_phase) {
        last_phase = phase;
        phase_started_frame = frame;
    }
    if (phase < 6 && frame - phase_started_frame > REPRO_HOMEWARP_PHASE_TIMEOUT) {
        fprintf(stderr,
                "[rando-repro] FAIL: homewarp phase %d stalled at task=%u state=%u substate=%u ui=%u health=%u control=%u message=%u pause-screen=%u pause-state=%u\n",
                phase, (unsigned)gMain.task, (unsigned)gMain.state, (unsigned)gMain.substate,
                (unsigned)gUI.state, (unsigned)gSave.stats.health, (unsigned)gPlayerState.controlMode,
                (unsigned)gMessage.state, (unsigned)gPauseMenuOptions.screen,
                (unsigned)gPauseMenuOptions.unk11);
        return 0;
    }

    switch (phase) {
    case 0:
        if (gMain.task == TASK_GAME && gMain.state == GAMETASK_MAIN &&
            gMain.substate == GAMEMAIN_UPDATE &&
            Port_DebugAction_Warp(0x02, 0x00, 0x88, 0x110, 1)) {
            phase = 1;
        }
        break;
    case 1:
        if (gMain.task == TASK_GAME && gMain.substate == GAMEMAIN_UPDATE &&
            gRoomControls.area == 0x02 && gRoomControls.room == 0x00) {
            if (!check_live_normal_town())
                return 0;
            phase = 2;
        }
        break;
    case 2:
        sLatePressMask = 0;
        phase = 3;
        break;
    case 3:
        if (Port_SoftSlots_IsPauseActive()) {
            sLatePressMask = 0;
            phase = 4;
        } else if ((gMessage.state & MESSAGE_ACTIVE) && frame % 30 == 0) {
            Port_Config_TestForceEdge(PORT_INPUT_A);
        } else if (frame % 30 == 0) {
            Port_Config_TestForceEdge(PORT_INPUT_START);
        }
        break;
    case 4:
        if (Rando_Homewarp_HintVisible() && gPauseMenuOptions.unk11 == 2) {
            phase = 5;
        } else if (frame % 40 == 0) {
            Port_Config_TestForceEdge(PORT_INPUT_R);
        }
        break;
    case 5:
        Port_Config_TestForceEdge(PORT_INPUT_SELECT);
        armed_frame = frame;
        phase = 6;
        break;
    case 6:
        if (gRoomControls.area == 0x22 && gRoomControls.room == 0x15) {
            fprintf(stderr, "[rando-repro] homewarp stage OK: returned home after %u frame(s)\n",
                    frame - armed_frame);
            *done = 1;
            return 1;
        }
        if (frame - armed_frame > 240) {
            fprintf(stderr, "[rando-repro] FAIL: homewarp failed to return Link home\n");
            return 0;
        }
        break;
    }
    return 1;
}

static int start_marked_slot_without_sidecar(void) {
    SaveFile saved;
    uint64_t binding = 0;
    if (ReadSaveFile(0, &saved) != 1 ||
        memcmp(saved.filler4ac, PC_RANDO_SAVE_MARKER, sizeof(PC_RANDO_SAVE_MARKER)) != 0) {
        fprintf(stderr, "[rando-repro] FAIL: missing-sidecar fixture has no marked EEPROM slot\n");
        return 0;
    }
    memcpy(&binding, saved.filler4ac + PC_RANDO_SAVE_BINDING_OFFSET, sizeof(binding));
    if (binding == 0) {
        fprintf(stderr, "[rando-repro] FAIL: missing-sidecar fixture has no binding hash\n");
        return 0;
    }
    Port_FileSelectRando_StartSlot(0);
    return 1;
}

void Port_ReproRando_Tick(unsigned int frame) {
    static int sActive = -1;
    static int sMissingSidecarMode = 0;
    static unsigned int sMissingSidecarStartFrame = 0;
    static int sDone = 0;
    static int sCoreDone = 0;
    static unsigned int sCoreFrame = 0;
    static int sImguiDone = 0;
    static int sHomewarpDone = 0;

    if (sActive < 0) {
        const char* env = getenv("TMC_REPRO_RANDO");
        const char* missing_env = getenv("TMC_REPRO_RANDO_MISSING_SIDECAR");
        sActive = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        sMissingSidecarMode = missing_env && *missing_env && strcmp(missing_env, "0") != 0;
        if (sActive) {
            fprintf(stderr, "[rando-repro] harness active\n");
        }
    }
    if (!sActive) return;

    if (sDone) return;

    if (gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3)
        Port_Config_TestForceEdge(PORT_INPUT_START);

    if (sMissingSidecarMode) {
        if (sMissingSidecarStartFrame == 0 && frame >= 200 &&
            gMain.task == TASK_FILE_SELECT && gMain.state == GAMETASK_INIT) {
            if (!start_marked_slot_without_sidecar()) { sDone = 1; exit(1); }
            sMissingSidecarStartFrame = frame;
        } else if (sMissingSidecarStartFrame != 0 && frame - sMissingSidecarStartFrame >= 90) {
            if (gMain.task != TASK_FILE_SELECT || gUI.state != 0 || Rando_IsActive()) {
                fprintf(stderr, "[rando-repro] FAIL: missing-sidecar save entered gameplay\n");
                sDone = 1;
                exit(1);
            }
            fprintf(stderr, "[rando-repro] missing sidecar refused marked slot\n");
            sDone = 1;
            exit(0);
        }
        if (frame > 1200 && sMissingSidecarStartFrame == 0) {
            fprintf(stderr, "[rando-repro] FAIL: missing-sidecar file select never became ready\n");
            sDone = 1;
            exit(1);
        }
        return;
    }

    if (!sCoreDone && frame >= 200 && gMain.task == TASK_FILE_SELECT && gMain.state == GAMETASK_INIT) {
        fprintf(stderr, "[rando-repro] file select ready at frame %u\n", frame);
        if (!run_menu_path()) { sDone = 1; exit(1); }
        if (!run_logic_key_path()) { sDone = 1; exit(1); }
        if (!run_world_open_test()) { sDone = 1; exit(1); }
        if (!run_story_skip_town_test()) { sDone = 1; exit(1); }
        if (!run_real_logic_chest_probe()) { sDone = 1; exit(1); }
        if (!run_obscure_extension_test()) { sDone = 1; exit(1); }
        sCoreDone = 1;
        sCoreFrame = frame;
    }

    if (!sCoreDone && frame > 1200) {
        fprintf(stderr, "[rando-repro] FAIL: file select never became ready (task=%u state=%u substate=%u)\n",
                (unsigned)gMain.task, (unsigned)gMain.state, (unsigned)gMain.substate);
        sDone = 1;
        exit(1);
    }

    if (sCoreDone && frame > sCoreFrame) {
        if (!sImguiDone) {
            if (!imgui_keyboard_stage(frame - sCoreFrame + 200, &sImguiDone)) {
                sDone = 1;
                exit(1);
            }
        } else if (!sHomewarpDone) {
            if (!homewarp_stage(frame, &sHomewarpDone)) {
                sDone = 1;
                exit(1);
            }
        } else {
            fprintf(stderr, "[rando-repro] ALL STAGES PASS\n");
            sDone = 1;
            exit(0);
        }
    }
}
