#include "port/port_asset_loader.cpp"
#include <cassert>

extern "C" {
u8 gEwram[0x40000], gIwram[0x8000], gVram[0x18000], gIoMem[0x400];
u16 gBgPltt[256], gObjPltt[256], gOamMem[0x200];
u8* gRomData = nullptr;
u32 gRomSize = 0;
RomRegion gRomRegion = ROM_REGION_USA;
int gActiveRegion = TMC_REGION_USA;
bool Port_DebugVerbose = false;
MapLayer gMapTop, gMapBottom;
u16 gMapDataTopSpecial[0x4000], gMapDataBottomSpecial[0x4000];
void Port_LogRomAccess(u32, const char*) {}
void* Port_ResolveEwramPtr(u32 addr) {
    if (addr >= 0x0200b650 && addr < 0x02017660) {
        const u32 offset = addr - 0x0200b650;
        return reinterpret_cast<u8*>(&gMapTop) + offset + (offset < 4 ? 0 : 4);
    }
    if (addr >= 0x02002f00 && addr < 0x0200af00)
        return reinterpret_cast<u8*>(gMapDataTopSpecial) + (addr - 0x02002f00);
    return gba_TryMemPtr(addr);
}
void MemCopy(const void* src, void* dest, u32 size) {
    std::memcpy(gba_MemPtr(static_cast<u32>(reinterpret_cast<uintptr_t>(dest))), src, size);
}
}
namespace port {
std::optional<std::filesystem::path> ExecutableDir() { return std::nullopt; }
}
namespace PortAssetPipeline {
bool EnsureRuntimeAssetsBuilt(const std::filesystem::path&, const std::filesystem::path&, std::string*) { return false; }
}

static bool load(u32 dest, u32 expected, size_t actual) {
    ParseGfxGroups({{"10", {{{"unknown", 7}, {"dest", dest}, {"size", expected},
                             {"file", "test.bin"}, {"terminator", true}}}}});
    gAssetGroupCache.binaryFiles["test.bin"] = std::make_unique<std::vector<u8>>(actual, 0xab);
    return Port_LoadGfxGroupFromAssets(10);
}

int main() {
    gAssetGroupCache.initAttempted = gAssetGroupCache.ready = true;
    assert(!load(0x02001a40, 1280, 1281));
    assert(!load(0x02001a40, 1280, 1279));
    assert(!load(0x02001a40, 0, 0));
    assert(!load(0x02001a40, 1280, 0x40000));
    assert(!load(0x0203ffff, 2, 2));
    assert(!load(0x06017fff, 2, 2));
    assert(!load(0x08000000, 32, 32));
    assert(!load(0xfffffff0, 32, 32));
    assert(!load(0x02017654, 1, 1)); // Resolver range extends past the native MapLayer.
    assert(!load(0x02017650, 8, 8)); // Starts inside native map, ends outside it.
    assert(!load(0x0200b652, 4, 4)); // Native pointer layout has a gap at GBA offset 4.
    assert(!load(0x0200aef0, 32, 32)); // Leaves remapped map storage.
    assert(!load(0x02002ef0, 0x8020, 0x8020)); // Both ends EWRAM, but a map is inside.
    assert(load(0x02001a40, 1280, 1280));
    assert(gEwram[0x1a40] == 0xab && gEwram[0x1f3f] == 0xab);
    assert(load(0x0200aef0, 16, 16));
    assert(reinterpret_cast<u8*>(gMapDataTopSpecial)[0x7fff] == 0xab);
    assert(load(0x0200b654, 16, 16));
    assert(reinterpret_cast<u8*>(&gMapTop)[8] == 0xab);
    assert(load(0x06017ff0, 16, 16));
    assert(gVram[0x17fff] == 0xab);
    std::puts("GFX ASSET BOUNDS OK");
}
