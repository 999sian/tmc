/*
 * port/port_repro_catperson.c — issue #152 "the game crashes every time I talk
 * to the woman on the left".
 *
 * The Romio/Julietta cat-vs-dog house (AREA_HOUSE_INTERIORS_2 room 6) shows a
 * feuding couple. The left NPC — the "cat person", a townsperson with type 0x0e
 * — runs script_CatPersonTalkingToDogPerson; on talk the script does
 * "Call sub_08062048". At global_progress 5 (iVar1 = progress - 2 = 3) that
 * selects dialog gUnk_0810B7C0[0x0e*8 + 3] — the ONLY DIALOG_CALL_FUNC entry in
 * the whole townsperson table, whose GBA func slot is the Thumb address
 * 0x0806200D (= sub_0806200C). The PC dialog unpacker (TownspersonGBADialog)
 * stored that raw GBA address straight into Dialog.data.func, and
 * ShowNPCDialogue invokes data.func directly -> a jump into GBA address space ->
 * SIGSEGV. The dog person (type 0x0d) has no CALL_FUNC entry, which is exactly
 * why "only the woman on the left" crashes.
 *
 * The couple's room spawn itself is gated by
 * src/roomInit.c::sub_StateChange_HouseInteriors2_Romio:
 *   !GetInventoryValue(ITEM_FLIPPERS) && CheckGlobalFlag(MIZUKAKI_START)
 * loads gUnk_080F238C, the two-NPC list containing the cat woman. Otherwise the
 * room loads only gUnk_additional_c_HouseInteriors2_Romio (one dog-person NPC).
 *
 * TMC_REPRO_CATPERSON=1 drives title -> file-select -> game, then invokes the
 * EXACT talk handler sub_08062048 on a townsperson entity (id 6, type 0x0e) with
 * global_progress forced to 5 — the precise crashing path the script reaches.
 * With the fix it resolves 0x0806200D via Port_LookupScriptFunc to native
 * sub_0806200C and shows the message (PASS); pre-fix it jumps to a raw GBA
 * address and the crash handler fires. It also exits(3) if the Call target is
 * unregistered, regression-guarding the port_script_funcs.c entry.
 *
 *   TMC_REPRO_CATPERSON=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy \
 *     SDL_AUDIODRIVER=dummy ./tmc_pc
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"         /* gMain, TASK_TITLE / TASK_FILE_SELECT / TASK_GAME */
#include "entity.h"       /* Entity */
#include "save.h"         /* gSave.global_progress */
#include "message.h"      /* gMessage (PASS evidence) */
#include "flags.h"        /* SetGlobalFlag, MIZUKAKI_START */
#include "item.h"         /* ITEM_FLIPPERS */
#include "player.h"       /* SetInventoryValue */
#include "port_gba_mem.h" /* gIoMem */

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

void Port_ReproCatPerson_Tick(unsigned int frame) {
    static int active = -1;
    static unsigned char last_task = 255;
    static unsigned int task_change_frame = 0;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_CATPERSON");
        active = (env && *env && env[0] != '0') ? 1 : 0;
        if (active) {
            fprintf(stderr, "[catperson] harness active (#152)\n");
        }
    }
    if (!active) {
        return;
    }

    if (gMain.task != last_task) {
        fprintf(stderr, "[catperson] frame %u: gMain.task %u -> %u\n", frame, (unsigned)last_task,
                (unsigned)gMain.task);
        last_task = gMain.task;
        task_change_frame = frame;
    }

    /* Drive title + file-select via pulsed key edges (GBA KEYINPUT: 0 = pressed).
     * Press 2 of every 16 frames so each screen sees fresh newKeys edges. */
    {
        unsigned short presses = 0;
        int pulse = (frame % 16) < 2;
        if (gMain.task == TASK_TITLE && frame >= 120 && pulse) {
            presses |= START_BUTTON | A_BUTTON;
        }
        if (gMain.task == TASK_FILE_SELECT && pulse) {
            presses |= A_BUTTON;
        }
        if (presses) {
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
        }
    }

    /* Once solidly in-game, fire the exact handler the cat person's script reaches
     * on interaction ("Call sub_08062048"). */
    if (gMain.task == TASK_GAME && frame - task_change_frame > 180) {
        extern void* Port_LookupScriptFunc(unsigned int gba_addr);
        extern void sub_08062048(Entity*);
        Entity ent;

        void* fn = Port_LookupScriptFunc(0x0806200Du);
        fprintf(stderr, "[catperson] Port_LookupScriptFunc(0x0806200D)=%p (NULL=bug)\n", fn);
        if (!fn) {
            fprintf(stderr, "[catperson] FAIL: cat-person CALL_FUNC target unregistered\n");
            fflush(stderr);
            _Exit(3);
        }

        memset(&ent, 0, sizeof(ent));
        ent.id = 6;                /* townsperson */
        ent.type = 0x0e;           /* cat person (the left NPC) */
        gSave.global_progress = 5;      /* iVar1 = 3 -> gUnk_0810B7C0[0x0e*8+3] = CALL_FUNC */
        SetGlobalFlag(MIZUKAKI_START);  /* real Romio-room spawn gate for the couple */
        SetInventoryValue(ITEM_FLIPPERS, 0);

        fprintf(stderr, "[catperson] calling talk handler sub_08062048 (type=0x0e, progress=5)...\n");
        fflush(stderr);
        sub_08062048(&ent); /* pre-fix: jumps to raw GBA 0x0806200D -> SIGSEGV */

        fprintf(stderr, "[catperson] PASS: sub_08062048 returned, no crash; gMessage.textIndex=%u\n",
                (unsigned)gMessage.textIndex);
        fflush(stderr);
        _Exit(0);
    }
}
