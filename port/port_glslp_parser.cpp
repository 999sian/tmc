/*
 * port_glslp_parser.cpp — Stage 5+C step 1: INI tokenizer + raw
 * key/value map.
 *
 * This is the first concrete piece of the .glslp runtime described
 * in port_glslp_parser.h. Subsequent steps (Preset assembly, libretro
 * GLSL preprocessor, multi-pass render graph, LUT loader) build on
 * top of the key/value map this file produces.
 *
 * Scope of this step:
 *   - Read a .glslp file from disk.
 *   - Strip comments (# and ;).
 *   - Strip whitespace.
 *   - Parse `key = value` lines into a flat std::map<string, string>.
 *   - Unquote string values ("..." with backslash escapes).
 *   - Return nullopt on syntactic errors with a stderr diagnostic.
 *
 * Out of scope here (left for next steps):
 *   - Semantic assembly into PortGlslp::Preset (turn shader0/...
 *     keys into ShaderPass[]).
 *   - Path resolution (relative paths get the source-file dirname
 *     joined later, in the Preset assembler).
 *   - INI section headers — libretro .glslp files don't use them.
 */

#include "port_glslp_parser.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace PortGlslp {

namespace {

/* Drop leading whitespace + comments (# / ;) from `s`. */
static std::string Trim(const std::string& s) {
    size_t lo = 0;
    while (lo < s.size() && std::isspace((unsigned char)s[lo])) ++lo;
    size_t hi = s.size();
    while (hi > lo && std::isspace((unsigned char)s[hi - 1])) --hi;
    return s.substr(lo, hi - lo);
}

/* Strip a trailing comment from `line`. Comments start at # or ;
 * outside of a quoted string. The libretro spec doesn't formally
 * specify quote-aware comments but several presets in the wild use
 * # inside paths (rare but possible), so we honour quoting. */
static std::string StripComment(const std::string& line) {
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
            in_quote = !in_quote;
        } else if (!in_quote && (c == '#' || c == ';')) {
            return line.substr(0, i);
        }
    }
    return line;
}

/* Unquote a value: if it starts AND ends with a double quote, strip
 * them and process the standard JSON-style escapes (\" \\ \n \t).
 * Otherwise return unchanged. */
static std::string Unquote(const std::string& s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return s;
    std::string out;
    out.reserve(s.size() - 2);
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 2 < s.size()) {
            char n = s[i + 1];
            switch (n) {
                case '"':  out += '"';  ++i; break;
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += c;   /* keep the backslash literal */
            }
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

/* Tokenize one .glslp file into key/value pairs. Returns an empty
 * map AND prints to stderr on error so the caller can distinguish
 * "no file" from "valid empty file" by inspecting `*ok` if needed.
 * For now: empty return on any failure; diagnostics go to stderr. */
std::map<std::string, std::string> TokenizeGlslpFile(const char* path) {
    std::map<std::string, std::string> out;

    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[glslp] open failed: %s\n", path);
        return out;
    }

    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        /* Strip the BOM SDL/Notepad sometimes appends — UTF-8 BOM is
         * EF BB BF, present at byte 0 of line 1 only. */
        if (lineno == 1 && line.size() >= 3
            && (unsigned char)line[0] == 0xEF
            && (unsigned char)line[1] == 0xBB
            && (unsigned char)line[2] == 0xBF) {
            line.erase(0, 3);
        }

        std::string s = StripComment(line);
        s = Trim(s);
        if (s.empty()) continue;

        /* Find the `=`. Spaces around it are optional but common. */
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "[glslp] %s:%d: missing '=' in '%s'\n",
                         path, lineno, s.c_str());
            continue;
        }
        std::string key = Trim(s.substr(0, eq));
        std::string val = Trim(s.substr(eq + 1));
        if (key.empty()) {
            std::fprintf(stderr, "[glslp] %s:%d: empty key\n", path, lineno);
            continue;
        }
        out[key] = Unquote(val);
    }
    return out;
}

/* Step 2 (LoadPresetFile) is not implemented yet. The future
 * implementation will:
 *   1. TokenizeGlslpFile(path) → raw key/value map.
 *   2. Read `shaders = N` for the pass count.
 *   3. For each pass i in 0..N-1, walk shaderN/filter_linearN/
 *      scale_typeN/etc keys and assemble a ShaderPass struct.
 *   4. Read `textures = "A;B;..."` and for each LUT name, walk the
 *      A_linear / A_wrap_mode / A_mipmap keys + the `A = path` line
 *      to assemble LutTexture structs.
 *   5. Read `parameters = "p0;p1;..."`; the per-parameter default
 *      comes from `p0 = N` lines AND/OR `#pragma parameter` lines
 *      in the referenced .glsl files (Step 3 — preprocessor).
 *   6. Resolve relative shader/LUT paths against the .glslp file's
 *      dirname.
 *   7. Validate: each pass references an existing shader file, each
 *      LUT references an existing image. Skip-with-warning on
 *      missing files so partial extractor outputs don't kill load.
 *
 * Estimated effort: ~120 LOC, no external dependencies. The data
 * structures it populates live in port_glslp_parser.h. */
std::optional<Preset> LoadPresetFile(const char* /*glslp_path*/) {
    /* TODO. See implementation sketch above. */
    return std::nullopt;
}

}  // namespace PortGlslp

/* C-callable runtime API stubs — return failure / not-active until
 * the multi-pass executor (Step 5) is in place. */
extern "C" int Port_GlslpRuntime_Load(const char* /*glslp_path*/) {
    return 0;
}
extern "C" void Port_GlslpRuntime_Unload(void) {}
extern "C" int Port_GlslpRuntime_IsActive(void) { return 0; }
