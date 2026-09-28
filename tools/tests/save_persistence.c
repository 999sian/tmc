#include "port/port_save.c"
#include <assert.h>
#include <sys/stat.h>

static void Put16(u8* p, u16 value) { p[0] = value; p[1] = value >> 8; }
static void Put32(u8* p, u32 value) { for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i); }
static void SetFlag(u8* flags, u32 id) { flags[id / 8] |= 1u << (id % 8); }
/* Global flags (include/flags.h). */
enum { LV1_CLEAR = 0x02, GLEEROK = 0x0B, START = 0x13, BEAN = 0x1B, GORON = 0x41, OUTDOOR = 0x49, ENDING = 0x51 };

/* Slot 0 holds START and OUTDOOR, plus the flags 8 ordinals away that make
 * the wrong-offset reading look like progress too; dungeonKeys[0] = 7. */
static void MakeSave(u32 flagsOffset, int beaten) {
    u8 image[EEPROM_SIZE] = {0};
    const char* signature = (REGION_IS_EU || REGION_IS_JP) ? EEPROM_SIGNATURE_EU_JP : EEPROM_SIGNATURE_USA;
    memcpy(image, signature, strlen(signature) + 1);
    u8* flags = image + 0x80 + flagsOffset;
    SetFlag(flags, START);
    SetFlag(flags, OUTDOOR);
    if (flagsOffset == SAVE_RETAIL_FLAGS) {
        SetFlag(flags, GLEEROK);
        SetFlag(flags, GORON);
    } else {
        SetFlag(flags, BEAN);
    }
    if (beaten) {
        SetFlag(flags, LV1_CLEAR);
        SetFlag(flags, ENDING);
    }
    flags[0x200] = 7;
    Put32(image + 0x34, (u32)'MCZ3');
    u16 checksum = CalculateImageChecksum(image + 0x34, 4) + CalculateImageChecksum(image + 0x80, 0x500);
    Put16(image + 0x30, checksum);
    Put16(image + 0x32, -checksum);
    ReverseEepromBlocks(image);
    FILE* f = fopen("tmc.sav", "wb");
    assert(f && fwrite(image, 1, sizeof(image), f) == sizeof(image));
    assert(fclose(f) == 0);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "switch") == 0) {
        EEPROMConfigure(0x40);
        /* A directory at the temporary-file path gives a deterministic write failure. */
        assert(mkdir("tmc.sav.tmp", 0700) == 0);
        const u16 data[4] = {0x1234, 0x5678, 0, 0};
        Port_Save_BeginTransaction();
        assert(EEPROMWrite0_8k_Check(100, data) == 0);
        assert(!Port_Save_EndTransaction());
        assert(!Port_Save_SetActivePath("tmc_other.sav"));
        assert(strcmp(Port_Save_GetActivePath(), "tmc.sav") == 0);
        u16 readback[4];
        assert(EEPROMRead(100, readback) == 0 && memcmp(data, readback, sizeof(data)) == 0);
        assert(sEepromDirty);
        assert(rmdir("tmc.sav.tmp") == 0);
        assert(Port_Save_SetActivePath("tmc_other.sav"));
        assert(strcmp(Port_Save_GetActivePath(), "tmc_other.sav") == 0);
        FILE* f = fopen("tmc.sav", "rb");
        assert(f);
        u8 image[EEPROM_SIZE];
        assert(fread(image, 1, sizeof(image), f) == sizeof(image));
        fclose(f);
        ReverseEepromBlocks(image);
        assert(memcmp(image + 800, data, sizeof(data)) == 0);
    } else if (strcmp(argv[1], "backup") == 0) {
        MakeSave(SAVE_OLD_FLAGS, 0);
        assert(mkdir("tmc.sav.bak", 0700) == 0);
        EEPROMConfigure(0x40);
        assert(sEepromWriteBlocked && !sEepromDirty);
        assert(SlotFlag(sEeprom + 0x80, SAVE_OLD_FLAGS, START) && sEeprom[0x80 + SAVE_LAYOUT_STAMP_OFFSET] == 0);
        const u16 data[4] = {0};
        assert(EEPROMWrite0_8k_Check(100, data) != 0);
    } else {
        /* retail: emulator save, left alone. legacy: PC save from <= v0.9.0
         * early in the game. legacy-beaten: the same after beating the game,
         * where only the padding byte tells the layouts apart. */
        const int retail = strcmp(argv[1], "retail") == 0;
        const int beaten = strcmp(argv[1], "legacy-beaten") == 0;
        MakeSave(retail ? SAVE_RETAIL_FLAGS : SAVE_OLD_FLAGS, beaten);
        EEPROMConfigure(0x40);
        const u8* slot = sEeprom + 0x80;
        assert(SlotFlag(slot, SAVE_RETAIL_FLAGS, START) && SlotFlag(slot, SAVE_RETAIL_FLAGS, OUTDOOR));
        assert(SlotFlag(slot, SAVE_RETAIL_FLAGS, retail ? GLEEROK : BEAN));
        assert(SlotFlag(slot, SAVE_RETAIL_FLAGS, LV1_CLEAR) == beaten);
        assert(slot[SAVE_OLD_FLAGS] == 0 && slot[0x45C] == 7);
        assert(slot[SAVE_LAYOUT_STAMP_OFFSET] == !retail);
        assert(StatusChecksumCoversData(sEeprom + 0x30, slot, 0x500));
        /* Reload must not shift the slot again. */
        u8 loaded[SAVE_SLOT_SIZE];
        memcpy(loaded, slot, sizeof(loaded));
        sEepromInited = 0;
        EEPROMConfigure(0x40);
        assert(memcmp(loaded, sEeprom + 0x80, sizeof(loaded)) == 0);
    }
    return 0;
}
