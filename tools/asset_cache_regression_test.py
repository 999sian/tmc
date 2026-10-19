#!/usr/bin/env python3
"""Standalone asset cleanup/cache checks; no ROM, game build, or live saves."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tmc_build', ROOT / 'build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class AssetCacheTests(unittest.TestCase):
    def test_cleanup_and_runtime_version(self):
        source = (ROOT / 'tools/src/assets_extractor/assets_extractor_api.cpp').read_text()
        start = source.index('        std::vector<std::filesystem::path> files;', source.index('if (opt.runtime_only && rename_ec)'))
        end = source.index('        tlog("remove editable tree', start)
        cleanup = source[start:end]
        start = source.index('struct RomFingerprint {')
        end = source.index('void StampRomFingerprint(', start)
        validator = source[start:end]
        header = (ROOT / 'port/port_asset_pipeline.hpp').read_text()
        version = re.search(r'inline constexpr int kBuildStateVersion = \d+;', header).group()
        harness = r'''
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
namespace PortAssetPipeline { VERSION }
namespace PortAssetLog {
template<class T, class F> void ParallelFor(T first, T last, F f) { for (; first < last; ++first) f(first); }
}
VALIDATOR
void cleanup(const std::filesystem::path& root) {
    struct { std::filesystem::path editable_root; } opt{root};
CLEANUP
}
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path base = argv[1];
    fs::create_directories(base / "external");
    std::ofstream(base / "external/sentinel") << "user data";
    fs::create_directory_symlink(base / "external", base / "editable-link");
    cleanup(base / "editable-link");
    assert(fs::exists(base / "external/sentinel"));
    assert(!fs::exists(fs::symlink_status(base / "editable-link")));
    fs::create_directories(base / "editable/subdir");
    std::ofstream(base / "editable/subdir/file") << "generated";
    fs::create_directory_symlink(base / "external", base / "editable/nested-link");
    cleanup(base / "editable");
    assert(!fs::exists(base / "editable"));
    assert(fs::exists(base / "external/sentinel"));
    fs::create_directories(base / "runtime");
    const auto stamp = [&](int version) {
        std::ofstream(base / "runtime/.asset_build_state.json") << nlohmann::json{
            {"builder_version", version}, {"rom_size", 16777216}, {"rom_mtime", 0}, {"pack_format", "loose"}};
    };
    stamp(3);
    assert(!RuntimeAssetsUpToDateImpl(base / "runtime", {16777216, 0}, false));
    stamp(PortAssetPipeline::kBuildStateVersion);
    assert(RuntimeAssetsUpToDateImpl(base / "runtime", {16777216, 0}, false));
    assert(!RuntimeAssetsUpToDateImpl(base / "runtime", {16777216, 0}, true));
}
'''.replace('VERSION', version).replace('VALIDATOR', validator).replace('CLEANUP', cleanup)
        with tempfile.TemporaryDirectory(prefix='tmc-asset-cache-') as directory:
            tmp = Path(directory)
            (tmp / 'test.cpp').write_text(harness)
            subprocess.run(['c++', '-std=c++20', str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
            subprocess.run([str(tmp / 'test'), str(tmp)], check=True)

    def test_pipeline_fingerprint_inputs(self):
        with tempfile.TemporaryDirectory(prefix='tmc-asset-inputs-') as directory:
            root = Path(directory)
            paths = ['assets/index.json', 'tools/src/assets_extractor/asset.cpp',
                     'port/port_asset_pipeline.cpp', 'port/port_asset_pipeline.hpp',
                     'port/port_asset_index.c', 'port/port_asset_index.h',
                     'port/port_asset_pak.cpp', 'port/port_asset_pak.hpp',
                     'port/port_asset_log.cpp', 'port/port_asset_log.hpp']
            for name in paths:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('original')
            with patch.object(build, 'REPO_ROOT', root):
                for name in paths:
                    before = build._assets_fingerprint('USA')
                    (root / name).write_text('changed')
                    self.assertNotEqual(before, build._assets_fingerprint('USA'), name)

    def test_regional_cache_invalidation(self):
        with tempfile.TemporaryDirectory(prefix='tmc-dist-cache-') as directory:
            dist = Path(directory)
            logic = dist / 'assets/rando/default.logic'
            logic.parent.mkdir(parents=True)
            logic.write_text('user logic')
            for stamp in (None, 'old', 'current'):
                for tree in ('assets', 'assets_src'):
                    for region in ('usa', 'eu', 'jp'):
                        cache = dist / tree / region
                        cache.mkdir(parents=True, exist_ok=True)
                        (cache / 'cached').touch()
                if stamp is not None:
                    (dist / '.assets_fingerprint').write_text(stamp + '\n')
                build._invalidate_region_caches(dist, 'current')
                for tree in ('assets', 'assets_src'):
                    for region in ('usa', 'eu', 'jp'):
                        self.assertEqual((dist / tree / region).exists(), stamp == 'current')
                self.assertEqual(logic.read_text(), 'user logic')
            external = dist / 'external'
            external.mkdir()
            (external / 'sentinel').write_text('user data')
            cache = dist / 'assets/usa'
            shutil.rmtree(cache)
            cache.symlink_to(external, target_is_directory=True)
            build._invalidate_region_caches(dist, 'changed')
            self.assertFalse(cache.is_symlink())
            self.assertEqual((external / 'sentinel').read_text(), 'user data')


if __name__ == '__main__':
    unittest.main()
