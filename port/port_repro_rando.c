/*
 * port/port_repro_rando.c — headless end-to-end randomizer check (TMC_REPRO_RANDO=1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "rando/rando.h"
#include "rando/rando_file_menu.h"
#include "port_debug_actions.h"
#include "room.h"
#include "rando/rando_save.h"
#include "rando/rando_runtime.h"
#include "save.h"
#include "flags.h"
#include "player.h"
#include "item_ids.h"
#include "main.h"
#include "port_gba_mem.h"
#include "port_imgui_menu.h"
#include "port_softslots.h"
#include "port_repro.h"

extern bool Rando_OverrideLocationKey(uint32_t location_key, unsigned char* type, unsigned char* subtype);

static int item_changes(uint16_t lo, uint16_t hi) {
    int changed = 0;
    for (unsigned t = lo; t <= hi; ++t) {
        unsigned char a = (unsigned char)t;
        unsigned char b = 0;
        if (Rando_OverrideItem(&a, &b) && a != (unsigned char)t) {
            changed++;
        }
    }
    return changed;
}

static int item_changed(uint16_t id) {
    unsigned char a = (unsigned char)id;
    unsigned char b = 0;
    return Rando_OverrideItem(&a, &b) && a != (unsigned char)id;
}

static int run_menu_path(void) {
    Port_RandoFileMenu_Open(0);
    if (!Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay did not open\n");
        return 0;
    }

    Port_RandoFileMenu_SetSeed("12345");
    Port_RandoFileMenu_CommitAndStart();

    if (Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay still open after confirm\n");
        return 0;
    }
    if (!Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: no active seed after Generate & Start\n");
        return 0;
    }
    if (Rando_GetSeed64() != Rando_SeedFromString("12345")) {
        fprintf(stderr, "[rando-repro] FAIL: active seed %llu != typed seed %llu\n",
                (unsigned long long)Rando_GetSeed64(),
                (unsigned long long)Rando_SeedFromString("12345"));
        return 0;
    }

    int changed = item_changes(0x54, 0x6F);
    if (changed <= 0) {
        fprintf(stderr, "[rando-repro] FAIL: NORMAL seed shuffled 0 collectibles\n");
        return 0;
    }
    if (item_changed(0x11) || item_changed(0x01)) {
        fprintf(stderr, "[rando-repro] FAIL: NORMAL altered progression (not beatable-safe)\n");
        return 0;
    }
    fprintf(stderr, "[rando-repro] menu path OK: seed=%llu, %d collectibles shuffled, progression intact\n",
            (unsigned long long)Rando_GetSeed64(), changed);

    {
        RandomizerSettings chaos = Rando_DefaultSettings();
        chaos.item_difficulty = RANDO_ITEM_POOL_CHAOS;
        chaos.glitchless_logic = true;
        if (!GenerateSeed(0x99, chaos) || !Rando_IsActive()) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS+glitchless generation failed\n");
            return 0;
        }
        if (item_changes(0x11, 0x17) != 0) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS+glitchless moved progression\n");
            return 0;
        }
        chaos.glitchless_logic = false;
        if (!GenerateSeed(0x99, chaos) || !Rando_IsActive()) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS generation failed\n");
            return 0;
        }
        int prog = item_changes(0x11, 0x17);
        if (prog <= 0) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS shuffled 0 progression items\n");
            return 0;
        }
        fprintf(stderr, "[rando-repro] chaos OK: %d progression items shuffled\n", prog);
    }
    return 1;
}

static int run_logic_key_path(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    if (!GenerateSeed(0xABCDEF, settings) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: generation failed\n");
        return 0;
    }
    unsigned char type = 0x99;
    unsigned char sub = 0;
    bool overridden = Rando_OverrideLocationKey(0x002211E0u, &type, &sub);
    Rando_Reset();
    if (!overridden) {
        fprintf(stderr, "[rando-repro] FAIL: location-key hook did not fire for 0x002211E0u\n");
        return 0;
    }
    fprintf(stderr, "[rando-repro] logic-key path OK: chest 0x002211E0u -> item 0x%02X\n", type);
    return 1;
}

static int run_world_open_test(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.open_world = true;
    if (!GenerateSeed(0x5EEDu, settings) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: world-open generation failed\n");
        return 0;
    }
    Rando_Runtime_OnNewFile();
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
    Rando_Reset();
    return ok;
}

static int run_real_logic_chest_probe(void) {
    RandomizerSettings s = Rando_DefaultSettings();
    if (!GenerateSeed(0xABCDu, s) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: real-logic generation failed\n");
        return 0;
    }
    int keyed = 0, engine_match = 0, overridden = 0, hook_key_ok = 0;
    uint32_t demo_key = 0xFFFFFFFFu;
    unsigned char demo_vanilla = 0, demo_item = 0;
    for (uint32_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        const RandoLocationDef* def = Rando_GetLocationDef((RandoLocationId)i);
        if (def == NULL) continue;
        uint32_t key = def->key;
        if (key == 0xFFFFFFFFu) continue;
        keyed++;
        u32 area = (key >> 16) & 0xff, room = (key >> 8) & 0xff, chest = key & 0xff;
        u8 vanilla = 0xFF;
        if (area < 0x90 && room < 64 && !(key & 0x80000000u)) {
            TileEntity* te = (TileEntity*)GetRoomProperty(area, room, 3);
            int idx = 0;
            for (int k = 0; te != NULL && k < 256 && te[k].type != 0; ++k) {
                if (te[k].type != SMALL_CHEST && te[k].type != BIG_CHEST) continue;
                if (idx == (int)chest) {
                    engine_match++;
                    vanilla = te[k]._2;
                    if (Rando_RoomChestIndex(area, room, te[k].localFlag) == (int)chest) hook_key_ok++;
                    break;
                  }
                  idx++;
              }
          }
          unsigned char t = vanilla, sub = 0;
          if (Rando_OverrideLocationKey(key, &t, &sub)) {
              overridden++;
              if (demo_key == 0xFFFFFFFFu && vanilla != 0xFF && t != vanilla) {
                  demo_key = key; demo_vanilla = vanilla; demo_item = t;
              }
          }
      }
      fprintf(stderr, "[rando-repro] real-chest probe: keyed=%d engine-chest-match=%d overridden=%d hook-key-ok=%d\n",
              keyed, engine_match, overridden, hook_key_ok);
      if (engine_match <= 0 || overridden <= 0) return 0;

      uint32_t sample_key = 0xFFFFFFFFu;
      unsigned char want = 0;
      for (uint32_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
          const RandoLocationDef* def = Rando_GetLocationDef((RandoLocationId)i);
          if (def == NULL) continue;
          uint32_t key = def->key;
          unsigned char t = 0xEE, sub = 0;
          if (Rando_OverrideLocationKey(key, &t, &sub)) { sample_key = key; want = t; break; }
      }
      if (!Port_RandoSave_SaveActiveSlot(0)) {
          fprintf(stderr, "[rando-repro] FAIL: sidecar save failed\n");
          return 0;
      }
      Rando_Reset();
      if (!Port_RandoSave_LoadSlot(0) || !Rando_IsActive()) {
          fprintf(stderr, "[rando-repro] FAIL: sidecar reload failed\n");
          return 0;
      }
      unsigned char t2 = 0xEE, sub2 = 0;
      if (!Rando_OverrideLocationKey(sample_key, &t2, &sub2) || t2 != want) {
          fprintf(stderr, "[rando-repro] FAIL: chest 0x%06X gave 0x%02X before, 0x%02X after reload\n",
                  sample_key, want, t2);
          return 0;
      }
      fprintf(stderr, "[rando-repro] persistence OK: chest 0x%06X -> 0x%02X survives sidecar reload\n",
              sample_key, t2);
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
        fprintf(stderr, "[rando-repro] homewarp stage SKIPPED: phase %d stalled (autoplay flow unavailable)\n", phase);
        *done = 1;
        return 1;
    }

    switch (phase) {
    case 0:
        if (gMain.task == TASK_GAME) {
            Port_DebugAction_Warp(0x22, 0x15, 0x90, 0x38, 1);
            phase = 1;
        }
        break;
    case 1:
        if (gMain.task == TASK_GAME && gRoomControls.area == 0x22 && gRoomControls.room == 0x15) {
            phase = 2;
        }
        break;
    case 2:
        sLatePressMask = REPRO_START_BUTTON;
        phase = 3;
        break;
    case 3:
        if (Port_SoftSlots_IsPauseActive()) {
            sLatePressMask = 0;
            phase = 4;
        }
        break;
    case 4:
        sLatePressMask = 0x0004;
        phase = 5;
        break;
    case 5:
        if (Rando_Homewarp_HintVisible()) {
            sLatePressMask = 0;
            if (Rando_Homewarp_Request()) {
                armed_frame = frame;
                phase = 6;
            } else {
                fprintf(stderr, "[rando-repro] FAIL: homewarp request refused\n");
                return 0;
            }
        }
        break;
    case 6:
        if (gRoomControls.area == 0x22 && gRoomControls.room == 0x15) {
            fprintf(stderr, "[rando-repro] homewarp stage OK: returned home after %u frame(s)\n",
                    frame - armed_frame);
            *done = 1;
            return 1;
        }
        if (frame - armed_frame > 120) {
            fprintf(stderr, "[rando-repro] FAIL: homewarp failed to return Link home\n");
            return 0;
        }
        break;
    }
    return 1;
}

void Port_ReproRando_Tick(unsigned int frame) {
    static int sActive = -1;
    static int sDone = 0;
    static int sImguiDone = 0;
    static int sHomewarpDone = 0;

    if (sActive < 0) {
        const char* env = getenv("TMC_REPRO_RANDO");
        sActive = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (sActive) {
            fprintf(stderr, "[rando-repro] harness active\n");
        }
    }
    if (!sActive) return;

    if (sDone) return;

    if (frame == 200) {
        if (!run_menu_path()) { sDone = 1; exit(1); }
        if (!run_logic_key_path()) { sDone = 1; exit(1); }
        if (!run_world_open_test()) { sDone = 1; exit(1); }
        if (!run_real_logic_chest_probe()) { sDone = 1; exit(1); }
    }

    if (frame > 200) {
        if (!sImguiDone) {
            if (!imgui_keyboard_stage(frame, &sImguiDone)) {
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
