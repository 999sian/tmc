#!/usr/bin/env python3
"""Compile the production sprite JSON parser in isolation with ASan/UBSan.

Run from any directory. Requires c++, and nlohmann_json installed by xmake
(or supply its include directory through CPLUS_INCLUDE_PATH).
"""
from pathlib import Path
import subprocess, tempfile
s=(Path(__file__).resolve().parents[1] / 'port/port_asset_loader.cpp').read_text()
parser=s[s.index('void ParseSpritePtrs('):s.index('\nvoid WriteLe32(')]
helper=s[s.index('std::string JsonStringOrEmpty('):s.index('\nbool IsRomPointer(')]
struct=s[s.index('struct SpritePtrEntryData {'):s.index('\nconstexpr size_t kAreaCount')]
code='''#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <charconv>
#include <cassert>
#include <climits>
using u32 = unsigned int;
constexpr size_t kSpritePtrMax = 512;
'''+struct+'\nstruct { std::vector<SpritePtrEntryData> spritePtrs; } gAssetGroupCache;\n'+helper+parser+'''
int main() {
 auto root=nlohmann::json::object();
 root[std::to_string(ULONG_MAX)]={{"pad",1}};
 ParseSpritePtrs(root);
 assert(gAssetGroupCache.spritePtrs.size()<=512);
 root={{"0",{{"pad",7}}},{"511",{{"pad",9}}},{"512",{{"pad",10}}},{"-1",{{"pad",2}}},{"x",{{"pad",3}}},{"1junk",{{"pad",4}}}};
 ParseSpritePtrs(root);
 assert(gAssetGroupCache.spritePtrs.size()==512);
 assert(gAssetGroupCache.spritePtrs[0].pad==7);
 assert(gAssetGroupCache.spritePtrs[511].pad==9);
 assert(gAssetGroupCache.spritePtrs[1].pad==0);
 auto array=nlohmann::json::array();
 for(int i=0;i<513;++i) array.push_back({{"pad",i}});
 ParseSpritePtrs(array);
 assert(gAssetGroupCache.spritePtrs.size()==512);
 assert(gAssetGroupCache.spritePtrs[511].pad==511);
}
'''
header=next(Path.home().glob('.xmake/packages/n/nlohmann_json/*/*/include/nlohmann/json.hpp'), None)
includes=['-I'+str(header.parent.parent)] if header else []
with tempfile.TemporaryDirectory() as td:
 p=Path(td); (p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g',*includes,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

print("Sprite asset parser regression checks passed.")
