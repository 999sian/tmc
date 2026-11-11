#ifndef PORT_ROM_ZIP_H
#define PORT_ROM_ZIP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when `data` starts with a zip local-file-header signature. */
int Port_RomZip_IsZip(const void* data, size_t len);

/* Extracts the first *.gba entry from an in-memory zip archive.
 * Supports stored and deflate entries; uncompressed size is capped at
 * `max_out`. Returns a malloc'd buffer (caller frees) and sets *out_len,
 * or NULL on any malformed/unsupported archive. */
void* Port_RomZip_ExtractGba(const void* data, size_t len, size_t max_out, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROM_ZIP_H */
