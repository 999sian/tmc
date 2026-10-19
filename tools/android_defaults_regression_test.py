#!/usr/bin/env python3
"""Check APK default selection and native staging without an Android SDK or ROM."""
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidDefaultsTests(unittest.TestCase):
    def test_apk_packages_only_public_defaults(self):
        source = (ROOT / 'android/app/build.gradle').read_text()
        task = re.search(r'tasks.register\("stagePublicDefaults", Sync\) \{(.*?)\n\}', source, re.S)
        self.assertIsNotNone(task, 'Missing APK defaults staging task')
        self.assertIn('from("${rootDir}/../assets")', task[1])
        self.assertIn('into("defaults")', task[1])
        self.assertIn('into("defaults/assets")', task[1])
        self.assertEqual(set(re.findall(r'include "([^"]+)"', task[1])),
                         {'voxel_shapes.json', 'rando/default.logic'})
        self.assertIn('assets.srcDir(publicDefaultsDir)', source)
        self.assertIn('dependsOn(stagePublicDefaults)', source)

    def test_native_staging_preserves_edits_and_retries_failed_writes(self):
        source = (ROOT / 'port/port_main.c').read_text()
        self.assertTrue('static void Port_AndroidStageDefaults(void)' in source, 'Missing native defaults staging')
        start = source.index('static void Port_AndroidStageDefaults(void)')
        end = source.index('\n}\n', start) + 3
        helper = source[start:end]
        self.assertLess(source.index('chdir(ext)'), source.index('    Port_AndroidStageDefaults();'))
        self.assertLess(source.index('    Port_AndroidStageDefaults();'), source.index('    Port_Config_Load('))
        code = r'''
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static const char *apk;
static bool fail_write;
static void *LoadDefault(const char *path, size_t *size) {
    char source[2048];
    snprintf(source, sizeof(source), "%s/%s", apk, path);
    return SDL_LoadFile(source, size);
}
static bool SaveDefault(const char *path, const void *data, size_t size) {
    if (fail_write) {
        FILE *out = fopen(path, "wb");
        assert(out);
        fputs("partial", out);
        fclose(out);
        SDL_SetError("injected write failure");
        return false;
    }
    return SDL_SaveFile(path, data, size);
}
#define SDL_LoadFile LoadDefault
#define SDL_SaveFile SaveDefault
HELPER
#undef SDL_LoadFile
#undef SDL_SaveFile
static void expect(const char *path, const char *content) {
    char *bytes = SDL_LoadFile(path, NULL);
    assert(bytes && strcmp(bytes, content) == 0);
    SDL_free(bytes);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    apk = argv[1];
    assert(chdir(argv[2]) == 0);
    Port_AndroidStageDefaults();
    expect("voxel_shapes.json", "public shapes");
    expect("assets/rando/default.logic", "public logic");
    assert(SDL_SaveFile("assets/rando/default.logic", "edited logic", 12));
    assert(SDL_SaveFile("voxel_shapes.json", "edited shapes", 13));
    Port_AndroidStageDefaults();
    expect("assets/rando/default.logic", "edited logic");
    expect("voxel_shapes.json", "edited shapes");
    assert(chdir(argv[3]) == 0);
    fail_write = true;
    Port_AndroidStageDefaults();
    assert(!SDL_GetPathInfo("voxel_shapes.json", NULL));
    assert(!SDL_GetPathInfo("assets/rando/default.logic", NULL));
    fail_write = false;
    Port_AndroidStageDefaults();
    expect("voxel_shapes.json", "public shapes");
    expect("assets/rando/default.logic", "public logic");
}
'''.replace('HELPER', helper)
        with tempfile.TemporaryDirectory(prefix='tmc-android-defaults-') as directory:
            tmp = Path(directory)
            apk = tmp / 'apk/defaults'
            (apk / 'assets/rando').mkdir(parents=True)
            (apk / 'voxel_shapes.json').write_text('public shapes')
            (apk / 'assets/rando/default.logic').write_text('public logic')
            for name in ('external', 'private'):
                (tmp / name).mkdir()
            (tmp / 'test.c').write_text(code)
            flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', str(tmp / 'test.c'), '-o', str(tmp / 'test')] + flags, check=True)
            subprocess.run([str(tmp / 'test'), str(tmp / 'apk'), str(tmp / 'external'), str(tmp / 'private')], check=True)


if __name__ == '__main__':
    unittest.main()
