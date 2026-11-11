/* Regression test for port_rom_zip.c. Builds archives in memory, so no
 * ROM or fixtures are needed:
 *   cc -I port port/port_rom_zip.c port/port_rom_zip_test.c -lz -o /tmp/t && /tmp/t
 */
#include "port_rom_zip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    const char* name;
    const uint8_t* data;
    size_t len;
    int deflate;
    int descriptor; /* bit 3: local header carries zero sizes */
} Entry;

static uint8_t sZip[1 << 16];
static size_t sZipLen;

static void Put16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void Put32(uint8_t* p, uint32_t v) {
    Put16(p, v);
    Put16(p + 2, v >> 16);
}

static void BuildZip(const Entry* e, int n) {
    uint8_t central[4096];
    size_t clen = 0;
    size_t off = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t packed[8192];
        size_t plen = e[i].len;
        if (e[i].deflate) {
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            deflateInit2(&zs, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            zs.next_in = (Bytef*)e[i].data;
            zs.avail_in = (uInt)e[i].len;
            zs.next_out = packed;
            zs.avail_out = sizeof(packed);
            deflate(&zs, Z_FINISH);
            plen = zs.total_out;
            deflateEnd(&zs);
        } else {
            memcpy(packed, e[i].data, plen);
        }
        const uint32_t crc = (uint32_t)crc32(0, e[i].data, (uInt)e[i].len);
        const uint16_t nlen = (uint16_t)strlen(e[i].name);
        const uint16_t method = e[i].deflate ? 8 : 0;
        const uint16_t flags = e[i].descriptor ? 8 : 0;

        uint8_t* l = sZip + off;
        memset(l, 0, 30);
        Put32(l, 0x04034b50u);
        Put16(l + 6, flags);
        Put16(l + 8, method);
        if (!e[i].descriptor) {
            Put32(l + 14, crc);
            Put32(l + 18, (uint32_t)plen);
            Put32(l + 22, (uint32_t)e[i].len);
        }
        Put16(l + 26, nlen);
        memcpy(l + 30, e[i].name, nlen);
        memcpy(l + 30 + nlen, packed, plen);
        size_t localEnd = off + 30 + nlen + plen;
        if (e[i].descriptor) {
            Put32(sZip + localEnd, 0x08074b50u);
            Put32(sZip + localEnd + 4, crc);
            Put32(sZip + localEnd + 8, (uint32_t)plen);
            Put32(sZip + localEnd + 12, (uint32_t)e[i].len);
            localEnd += 16;
        }

        uint8_t* c = central + clen;
        memset(c, 0, 46);
        Put32(c, 0x02014b50u);
        Put16(c + 8, flags);
        Put16(c + 10, method);
        Put32(c + 16, crc);
        Put32(c + 20, (uint32_t)plen);
        Put32(c + 24, (uint32_t)e[i].len);
        Put16(c + 28, nlen);
        Put32(c + 42, (uint32_t)off);
        memcpy(c + 46, e[i].name, nlen);
        clen += 46 + nlen;
        off = localEnd;
    }
    memcpy(sZip + off, central, clen);
    uint8_t* d = sZip + off + clen;
    memset(d, 0, 22);
    Put32(d, 0x06054b50u);
    Put16(d + 8, (uint16_t)n);
    Put16(d + 10, (uint16_t)n);
    Put32(d + 12, (uint32_t)clen);
    Put32(d + 16, (uint32_t)off);
    sZipLen = off + clen + 22;
}

static int sFailures;

static void Expect(const char* label, const uint8_t* want, size_t wantLen, size_t cap) {
    size_t gotLen = 0;
    uint8_t* got = (uint8_t*)Port_RomZip_ExtractGba(sZip, sZipLen, cap, &gotLen);
    int ok = want ? (got && gotLen == wantLen && memcmp(got, want, wantLen) == 0) : got == NULL;
    printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    sFailures += !ok;
    free(got);
}

int main(void) {
    uint8_t rom[4096];
    for (size_t i = 0; i < sizeof(rom); ++i)
        rom[i] = (uint8_t)(i * 7 + (i >> 5));
    const uint8_t readme[] = "not a rom";

    Entry stored = { "tmc.gba", rom, sizeof(rom), 0, 0 };
    BuildZip(&stored, 1);
    if (!Port_RomZip_IsZip(sZip, sZipLen) || Port_RomZip_IsZip(rom, sizeof(rom))) {
        printf("FAIL IsZip\n");
        sFailures++;
    }
    Expect("stored entry", rom, sizeof(rom), 1 << 20);

    Entry deflated = { "Minish Cap (USA).GBA", rom, sizeof(rom), 1, 1 };
    BuildZip(&deflated, 1);
    Expect("deflated entry with data descriptor, uppercase ext", rom, sizeof(rom), 1 << 20);

    Entry mixed[] = { { "readme.txt", readme, sizeof(readme), 1, 0 }, { "dir/tmc.gba", rom, sizeof(rom), 1, 0 } };
    BuildZip(mixed, 2);
    Expect("skips non-gba entries", rom, sizeof(rom), 1 << 20);
    Expect("rejects entry over size cap", NULL, 0, sizeof(rom) - 1);

    BuildZip(mixed, 1);
    Expect("no gba entry", NULL, 0, 1 << 20);

    BuildZip(&deflated, 1);
    sZip[30 + 20 + 8] ^= 0xFF; /* inside the deflate stream */
    Expect("corrupt deflate stream", NULL, 0, 1 << 20);

    BuildZip(&stored, 1);
    sZip[30 + 7 + 100] ^= 0xFF; /* stored bytes: only the CRC catches it */
    Expect("crc mismatch", NULL, 0, 1 << 20);

    BuildZip(&stored, 1);
    sZipLen -= 30; /* truncated: EOCD gone */
    Expect("truncated archive", NULL, 0, 1 << 20);

    return sFailures ? 1 : 0;
}
