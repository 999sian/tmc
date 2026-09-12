// Exercise the production loader with user-editable numeric settings.
#include "port_runtime_config.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

static void load(const char* path, const char* json) {
    FILE* f = fopen(path, "w");
    assert(f);
    fputs(json, f);
    assert(fclose(f) == 0);
    Port_Config_Load(path);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* path = argv[1];
    load(path, R"({"window_scale":1e100,"frame_time_ns":-1,"practice_slowmo":1e-30})");
    assert(Port_Config_WindowScale() == 3);
    assert(Port_Config_FrameTimeNs() == 1000000000ULL / 60);
    assert(Port_Config_GetPracticeSlowmo() == 0.05f);
    load(path, R"({"window_scale":4294967297,"practice_slowmo":1e100,"bg_fill_color":[1e100,-1e100,127.5]})");
    assert(Port_Config_WindowScale() == 3);
    assert(Port_Config_GetPracticeSlowmo() == 1.0f);
    u8 r, g, b;
    Port_Config_BgFillColor(&r, &g, &b);
    assert(r == 255 && g == 0 && b == 127);
    load(path, R"({"window_scale":4,"frame_time_ns":0,"practice_slowmo":0.5,"master_volume":"bad"})");
    assert(Port_Config_WindowScale() == 4);
    assert(Port_Config_FrameTimeNs() == 0);
    assert(Port_Config_GetPracticeSlowmo() == 0.5f);
    assert(Port_Config_GetMasterVolume() == 1.0f);
    load(path, R"({"reborn_features":4294967297})");
    assert(Port_Config_GetRebornMask() == 0);
    load(path, R"({"reborn_features":-1})");
    assert(Port_Config_GetRebornMask() == 0);
    for (const char* json : {R"({"practice_slowmo":0})", R"({"practice_slowmo":-0.001})"}) {
        load(path, json);
        assert(Port_Config_GetPracticeSlowmo() == 0.05f);
    }
    Port_Config_SetPracticeSlowmo(0);
    assert(Port_Config_GetPracticeSlowmo() == 0.05f);
    Port_Config_SetPracticeSlowmo(2);
    assert(Port_Config_GetPracticeSlowmo() == 1.0f);
    puts("runtime config regression checks passed");
}
