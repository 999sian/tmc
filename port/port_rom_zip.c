/*
 * port/port_rom_zip.c — pull a .gba out of a .zip so users can pick the
 * archive their dump came in. Reads the central directory (sizes in the
 * local header are zero when bit 3 is set) and inflates with zlib, which
 * the port already links for libpng. Input is untrusted: every offset is
 * bounds-checked against the buffer.
 */
#include "port_rom_zip.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint16_t Le16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t Le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int Port_RomZip_IsZip(const void* data, size_t len) {
    return len >= 4 && memcmp(data, "PK\x03\x04", 4) == 0;
}

static int EndsWithGba(const uint8_t* name, size_t n) {
    return n >= 4 && name[n - 4] == '.' && tolower(name[n - 3]) == 'g' && tolower(name[n - 2]) == 'b' &&
           tolower(name[n - 1]) == 'a';
}

void* Port_RomZip_ExtractGba(const void* data, size_t len, size_t max_out, size_t* out_len) {
    const uint8_t* buf = (const uint8_t*)data;
    if (len < 22)
        return NULL;

    /* End-of-central-directory record: last 22 bytes plus up to 64 KiB comment. */
    size_t eocd = (size_t)-1;
    size_t floor = len > 22 + 0xFFFF ? len - 22 - 0xFFFF : 0;
    for (size_t i = len - 22 + 1; i-- > floor;) {
        if (Le32(buf + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == (size_t)-1)
        return NULL;

    const uint16_t count = Le16(buf + eocd + 10);
    size_t p = Le32(buf + eocd + 16);
    for (uint16_t e = 0; e < count; ++e) {
        if (p > len || len - p < 46 || Le32(buf + p) != 0x02014b50u)
            return NULL;
        const uint16_t method = Le16(buf + p + 10);
        const uint32_t crc = Le32(buf + p + 16);
        const uint32_t csize = Le32(buf + p + 20);
        const uint32_t usize = Le32(buf + p + 24);
        const uint16_t nlen = Le16(buf + p + 28);
        const size_t next = p + 46 + nlen + Le16(buf + p + 30) + Le16(buf + p + 32);
        const size_t lho = Le32(buf + p + 42);
        if (len - p - 46 < nlen)
            return NULL;
        if (!EndsWithGba(buf + p + 46, nlen) || (method != 0 && method != 8) || usize == 0 || usize > max_out) {
            p = next;
            continue;
        }

        if (lho > len || len - lho < 30 || Le32(buf + lho) != 0x04034b50u)
            return NULL;
        const size_t dataOff = lho + 30 + Le16(buf + lho + 26) + Le16(buf + lho + 28);
        if (dataOff > len || len - dataOff < csize)
            return NULL;

        uint8_t* out = (uint8_t*)malloc(usize);
        if (!out)
            return NULL;
        if (method == 0) {
            if (csize != usize) {
                free(out);
                return NULL;
            }
            memcpy(out, buf + dataOff, usize);
        } else {
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
                free(out);
                return NULL;
            }
            zs.next_in = (Bytef*)(buf + dataOff);
            zs.avail_in = csize;
            zs.next_out = out;
            zs.avail_out = usize;
            const int rc = inflate(&zs, Z_FINISH);
            const uLong produced = zs.total_out;
            inflateEnd(&zs);
            if (rc != Z_STREAM_END || produced != usize) {
                free(out);
                return NULL;
            }
        }
        if ((uint32_t)crc32(0, out, usize) != crc) {
            free(out);
            return NULL;
        }
        *out_len = usize;
        return out;
    }
    return NULL;
}
