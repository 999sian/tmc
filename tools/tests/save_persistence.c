#include "port/port_save.c"
#include <assert.h>
#include <sys/stat.h>

static void Put16(u8* p, u16 value) { p[0] = value; p[1] = value >> 8; }
static void Put32(u8* p, u32 value) { for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i); }
static void MakeSave(int legacy) {
    u8 image[EEPROM_SIZE] = {0};
    const char* signature = (REGION_IS_EU || REGION_IS_JP) ? EEPROM_SIGNATURE_EU_JP : EEPROM_SIGNATURE_USA;
    memcpy(image, signature, strlen(signature) + 1);
    image[0x80 + (legacy ? 0x25B : 0x25C)] = 0x42;
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
    } else {
        const int legacy = strcmp(argv[1], "retail") != 0;
        const int migrate = strcmp(argv[1], "legacy") == 0 || strcmp(argv[1], "backup") == 0;
        MakeSave(legacy);
        if (migrate) setenv("TMC_SAVE_MIGRATE_LEGACY_FLAGS", "1", 1);
        if (strcmp(argv[1], "backup") == 0) {
            assert(mkdir("tmc.sav.bak", 0700) == 0);
            EEPROMConfigure(0x40);
            assert(sEepromWriteBlocked && !sEepromDirty);
            assert(sEeprom[0x80 + 0x25B] == 0x42);
            const u16 data[4] = {0};
            assert(EEPROMWrite0_8k_Check(100, data) != 0);
            return 0;
        }
        EEPROMConfigure(0x40);
        const int flagOffset = legacy && !migrate ? 0x25B : 0x25C;
        assert(sEeprom[0x80 + flagOffset] == 0x42);
        assert(sEeprom[0x80 + 0x25D] == 0);
        assert(StatusChecksumCoversData(sEeprom + 0x30, sEeprom + 0x80, 0x500));
        /* Reload must not shift an explicitly migrated slot a second time. */
        sEepromInited = 0;
        EEPROMConfigure(0x40);
        assert(sEeprom[0x80 + flagOffset] == 0x42);
    }
    return 0;
}
