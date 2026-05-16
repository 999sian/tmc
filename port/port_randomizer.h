#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minish Cap Randomizer integration (Phase A: external-CLI shell-out).
 *
 * The randomizer (github.com/minishmaker/randomizer) is a C# tool that
 * takes a clean EU Minish Cap ROM + seed + settings and produces:
 *   - a randomized .gba
 *   - an IPS / UPS patch
 *   - a spoiler log
 *
 * This module finds the CLI binary, builds a Commands.txt file, and
 * invokes the CLI to produce a new ROM. It does NOT bundle the CLI —
 * the user must install it separately. Phase B (later) will fetch the
 * matching release at build time; Phase C will AOT-embed the engine.
 *
 * The randomizer's input ROM must be EU; the resulting ROM is also
 * EU-shaped, so tmc_pc must be built with `--game_version=EU` to play
 * the output cleanly.
 */

typedef enum {
    PORT_RANDO_OK                = 0,
    PORT_RANDO_CLI_NOT_FOUND     = 1,
    PORT_RANDO_INPUT_ROM_MISSING = 2,
    PORT_RANDO_RUN_FAILED        = 3,
    PORT_RANDO_OUTPUT_MISSING    = 4,
} PortRandomizerStatus;

/*
 * Resolve the path to the randomizer CLI binary. Search order:
 *   1. TMC_RANDOMIZER_CLI env var (exact path, used as-is if non-empty)
 *   2. <exe-dir>/randomizer/MinishCapRandomizerCLI[.exe]
 *   3. PATH lookup for "MinishCapRandomizerCLI"
 *
 * Writes the resolved path to out (up to out_len bytes, NUL-terminated).
 * Returns true if a CLI was found and looks runnable.
 */
bool Port_Randomizer_FindCLI(char* out, size_t out_len);

/*
 * Generate a randomized ROM via the external CLI.
 *
 *   input_rom_path  : path to clean EU Minish Cap ROM
 *   seed            : 0 → CLI picks a random seed; non-zero → use this
 *   output_rom_path : where to write the rolled ROM
 *   spoiler_path    : optional spoiler-log output path; pass NULL to skip
 *   error_out       : optional buffer for human-readable error (max
 *                     error_len bytes); pass NULL to skip
 *
 * Returns PORT_RANDO_OK on success. On failure, the binary at
 * output_rom_path is left in whatever state the CLI produced (may be
 * absent, partial, or unchanged from a prior run).
 */
PortRandomizerStatus Port_Randomizer_RollSeed(
    const char*  input_rom_path,
    uint32_t     seed,
    const char*  output_rom_path,
    const char*  spoiler_path,
    char*        error_out,
    size_t       error_len
);

#ifdef __cplusplus
}
#endif
