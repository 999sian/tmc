#include <cstdint>
#include <string>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cstdio>
constexpr unsigned kSongCount = 1;
const char* variant;
const char* GetCurrentVariantName() {
    return variant;
}
const char* Port_GetSongLabel(unsigned short) {
    return "sfxPlyJump";
}
#include "song_map_parser.inc"
int main() {
    std::ifstream f("assets/sounds.json");
    std::string json((std::istreambuf_iterator<char>(f)), {});
    std::array<size_t, 1> out;
    int failures = 0;
    variant = "USA";
    ParseSongMap(json, out);
    failures += out[0] != 0xddf2fc;
    variant = "JP";
    ParseSongMap(json, out);
    printf("JP sfxPlyJump header: got %zx, expected %zx\n", out[0], size_t(0xdc127c));
    failures += out[0] != size_t(0xdc127c);
    const std::string fixture =
        R"([{"offsets":{"JP":-10,"EU":20}},{"offsets":{"EU":30}},{"path": "sounds/sfxPlyJump.bin","start":100,"options":{"headerOffset":8}}])";
    ParseSongMap(fixture, out);
    failures += out[0] != 98;
    printf("JP retains offset across EU-only update: %s\n", out[0] == 98 ? "PASS" : "FAIL");
    variant = "EU";
    ParseSongMap(json, out);
    failures += out[0] != 0xeea894;
    variant = "EU";
    ParseSongMap(fixture, out);
    failures += out[0] != 138;
    variant = "USA";
    ParseSongMap(fixture, out);
    failures += out[0] != 108;
    variant = "JP";
    const std::string reset =
        R"([{"offsets":{"JP":-10}},{"offsets":{"JP":0}},{"path": "sounds/sfxPlyJump.bin","start":100,"options":{"headerOffset":8}}])";
    ParseSongMap(reset, out);
    failures += out[0] != 108;
    const std::string direct =
        R"([{"offsets":{"JP":-10}},{"path": "sounds/sfxPlyJump.bin","starts":{"JP":200},"options":{"headerOffset":8}}])";
    ParseSongMap(direct, out);
    failures += out[0] != 208;
    printf("song map regression failures: %d\n", failures);
    return failures;
}
