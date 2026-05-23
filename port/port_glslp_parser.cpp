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
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

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

/* ----- Step 2 helpers ---------------------------------------------- */

namespace {

/* Look up a key with a default fallback. Returns the default when
 * the key is absent so per-pass options collapse cleanly to defaults. */
static const std::string* Find(const std::map<std::string, std::string>& m,
                               const std::string& key) {
    auto it = m.find(key);
    return (it == m.end()) ? nullptr : &it->second;
}

static int ParseInt(const std::string& s, int dflt) {
    try { return std::stoi(s); } catch (...) { return dflt; }
}
static float ParseFloat(const std::string& s, float dflt) {
    try { return std::stof(s); } catch (...) { return dflt; }
}
static bool ParseBool(const std::string& s, bool dflt) {
    if (s.empty()) return dflt;
    if (s == "true"  || s == "True"  || s == "TRUE"  || s == "1" || s == "yes") return true;
    if (s == "false" || s == "False" || s == "FALSE" || s == "0" || s == "no")  return false;
    return dflt;
}

static ScaleType ParseScaleType(const std::string& s) {
    if (s == "source")   return ScaleType::Source;
    if (s == "viewport") return ScaleType::Viewport;
    if (s == "absolute") return ScaleType::Absolute;
    return ScaleType::Source;  /* libretro default */
}

static WrapMode ParseWrapMode(const std::string& s) {
    if (s == "clamp_to_border")  return WrapMode::ClampBorder;
    if (s == "clamp_to_edge")    return WrapMode::ClampEdge;
    if (s == "repeat")           return WrapMode::Repeat;
    if (s == "mirrored_repeat")  return WrapMode::MirroredRepeat;
    return WrapMode::ClampBorder;  /* libretro default */
}

/* Split "A;B;C" into ["A", "B", "C"]. Empty tokens dropped. */
static std::vector<std::string> SplitSemicolons(const std::string& s) {
    std::vector<std::string> out;
    size_t lo = 0;
    while (lo <= s.size()) {
        size_t hi = s.find(';', lo);
        if (hi == std::string::npos) hi = s.size();
        std::string tok = s.substr(lo, hi - lo);
        /* Trim per-token whitespace — libretro presets sometimes write
         * "A; B; C" with spaces. */
        size_t ts = tok.find_first_not_of(" \t");
        size_t te = tok.find_last_not_of(" \t");
        if (ts != std::string::npos) {
            out.push_back(tok.substr(ts, te - ts + 1));
        }
        lo = hi + 1;
    }
    return out;
}

/* Resolve a maybe-relative path against the .glslp file's dirname.
 * Absolute paths return unchanged. */
static std::string ResolvePath(const std::string& glslp_path,
                               const std::string& rel) {
    namespace fs = std::filesystem;
    fs::path p(rel);
    if (p.is_absolute()) return p.lexically_normal().string();
    fs::path base = fs::path(glslp_path).parent_path();
    return (base / p).lexically_normal().string();
}

}  // namespace

/* Step 2 — assemble the raw key/value map from Step 1 into a Preset.
 * Catches malformed pass counts, missing required keys, and missing
 * referenced files (warn-but-continue, so partial trees still load). */
std::optional<Preset> LoadPresetFile(const char* glslp_path) {
    if (!glslp_path) return std::nullopt;

    auto map = TokenizeGlslpFile(glslp_path);
    if (map.empty()) {
        /* TokenizeGlslpFile already logged the reason. */
        return std::nullopt;
    }

    Preset out;
    out.source_path = glslp_path;

    /* Pass count (`shaders = N`). Required. */
    const std::string* shaders_str = Find(map, "shaders");
    if (!shaders_str) {
        std::fprintf(stderr, "[glslp] %s: missing required key 'shaders'\n", glslp_path);
        return std::nullopt;
    }
    int npasses = ParseInt(*shaders_str, -1);
    if (npasses < 0 || npasses > 64) {  /* libretro caps at ~16 in practice */
        std::fprintf(stderr, "[glslp] %s: bad shaders count %d\n", glslp_path, npasses);
        return std::nullopt;
    }

    /* Per-pass keys. libretro accepts both unified `scale_typeN/scaleN`
     * and per-axis `scale_type_xN/scale_xN` forms; prefer the per-axis
     * keys when present, fall back to the unified key for both axes. */
    out.passes.reserve(npasses);
    for (int i = 0; i < npasses; ++i) {
        ShaderPass p;
        const std::string idx = std::to_string(i);

        const std::string* shader = Find(map, "shader" + idx);
        if (!shader) {
            std::fprintf(stderr, "[glslp] %s: pass %d missing 'shader%d' key\n",
                         glslp_path, i, i);
            return std::nullopt;
        }
        p.path = ResolvePath(glslp_path, *shader);

        if (const std::string* v = Find(map, "filter_linear" + idx))    p.filter_linear    = ParseBool(*v, false);
        if (const std::string* v = Find(map, "wrap_mode" + idx))        p.wrap             = ParseWrapMode(*v);
        if (const std::string* v = Find(map, "mipmap_input" + idx))     p.mipmap_input     = ParseBool(*v, false);
        if (const std::string* v = Find(map, "srgb_framebuffer" + idx)) p.srgb_framebuffer = ParseBool(*v, false);
        if (const std::string* v = Find(map, "float_framebuffer" + idx)) p.float_framebuffer = ParseBool(*v, false);

        /* Scale type/factor: per-axis takes precedence over unified. */
        ScaleType st_unified = ScaleType::Source;
        float     sc_unified = 1.0f;
        if (const std::string* v = Find(map, "scale_type" + idx)) st_unified = ParseScaleType(*v);
        if (const std::string* v = Find(map, "scale" + idx))      sc_unified = ParseFloat(*v, 1.0f);
        p.scale_type_x = st_unified;
        p.scale_type_y = st_unified;
        p.scale_x = sc_unified;
        p.scale_y = sc_unified;
        if (const std::string* v = Find(map, "scale_type_x" + idx)) p.scale_type_x = ParseScaleType(*v);
        if (const std::string* v = Find(map, "scale_type_y" + idx)) p.scale_type_y = ParseScaleType(*v);
        if (const std::string* v = Find(map, "scale_x" + idx))      p.scale_x      = ParseFloat(*v, p.scale_x);
        if (const std::string* v = Find(map, "scale_y" + idx))      p.scale_y      = ParseFloat(*v, p.scale_y);

        if (const std::string* v = Find(map, "alias" + idx)) {
            if (!v->empty()) p.alias = *v;
        }

        /* Soft-validate the shader file exists. Missing-but-continue
         * so a partial extractor output doesn't kill load — Step 5
         * (render-graph executor) will fail loudly if the shader
         * can't be compiled. */
        std::error_code ec;
        if (!std::filesystem::exists(p.path, ec)) {
            std::fprintf(stderr, "[glslp] %s: pass %d shader missing: %s\n",
                         glslp_path, i, p.path.c_str());
        }

        out.passes.push_back(std::move(p));
    }

    /* Lookup-textures (LUTs). `textures = "A;B;..."` then per-name
     * keys. Each LUT's path is the `NAME = path` line. */
    if (const std::string* tex_list = Find(map, "textures")) {
        for (const auto& name : SplitSemicolons(*tex_list)) {
            const std::string* path = Find(map, name);
            if (!path) {
                std::fprintf(stderr, "[glslp] %s: LUT '%s' listed in textures= but no '%s = ...' line\n",
                             glslp_path, name.c_str(), name.c_str());
                continue;
            }
            LutTexture lut;
            lut.name = name;
            lut.path = ResolvePath(glslp_path, *path);
            if (const std::string* v = Find(map, name + "_linear"))    lut.linear = ParseBool(*v, true);
            if (const std::string* v = Find(map, name + "_wrap_mode")) lut.wrap   = ParseWrapMode(*v);
            if (const std::string* v = Find(map, name + "_mipmap"))    lut.mipmap = ParseBool(*v, false);

            std::error_code ec;
            if (!std::filesystem::exists(lut.path, ec)) {
                std::fprintf(stderr, "[glslp] %s: LUT '%s' file missing: %s\n",
                             glslp_path, name.c_str(), lut.path.c_str());
            }
            out.luts.push_back(std::move(lut));
        }
    }

    /* Runtime-tunable parameters declared in `parameters = "..."` and
     * the matching `paramName = value` overrides. The per-parameter
     * label/min/max/step come from `#pragma parameter` directives in
     * the GLSL source itself — that's Step 3 (preprocessor). For now
     * we only record the names + override values; Step 3 will fill in
     * the metadata when it walks the GLSL sources. */
    if (const std::string* param_list = Find(map, "parameters")) {
        for (const auto& id : SplitSemicolons(*param_list)) {
            ShaderParam pp;
            pp.id = id;
            pp.label = id;  /* placeholder; Step 3 reads `#pragma parameter` for real */
            if (const std::string* v = Find(map, id)) {
                pp.default_value = pp.current_value = ParseFloat(*v, 0.0f);
            }
            out.parameters.push_back(std::move(pp));
        }
    }

    /* Optional feedback pass. */
    if (const std::string* fb = Find(map, "feedback_pass")) {
        int fp = ParseInt(*fb, -1);
        if (fp >= 0 && fp < npasses) out.feedback_pass = fp;
    }

    return out;
}

}  // namespace PortGlslp

/* C-callable runtime API stubs — return failure / not-active until
 * the multi-pass executor (Step 5) is in place. */
extern "C" int Port_GlslpRuntime_Load(const char* /*glslp_path*/) {
    return 0;
}
extern "C" void Port_GlslpRuntime_Unload(void) {}
extern "C" int Port_GlslpRuntime_IsActive(void) { return 0; }
