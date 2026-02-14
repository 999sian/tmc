set_project("tmc")
set_version("0.1.0")
set_xmakever("2.7.0")

-- ====================
-- Configuration
-- ====================
add_rules("mode.debug", "mode.release")
set_defaultmode("release")

-- Force MinGW toolchain
set_toolchains("mingw")

-- Game version options
option("game_version")
    set_default("USA")
    set_showmenu(true)
    set_description("Game version to build", "USA", "EU", "JP", "DEMO_USA", "DEMO_JP")
option_end()

-- Build directories
local build_dir = "build/$(plat)"
local tools_bin = "tools/bin"

-- ====================
-- Third-party packages
-- ====================
add_requires("nlohmann_json", {configs = {cmake = false}})
add_requires("fmt", {configs = {header_only = true}})
add_requires("libpng")
add_requires("zlib")
add_requires('libsdl3')

-- ====================
-- Tools
-- ====================

-- agb2mid
target("agb2mid")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/agb2mid/*.cpp")
    add_includedirs("tools/src/agb2mid")
target_end()

-- aif2pcm
target("aif2pcm")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/aif2pcm/*.c")
    add_includedirs("tools/src/aif2pcm")
target_end()

-- asset_processor
target("asset_processor")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/asset_processor/*.cpp")
    add_files("tools/src/asset_processor/assets/*.cpp")
    add_includedirs("tools/src/asset_processor")
    add_includedirs("tools/src/util")
    add_packages("nlohmann_json", "fmt")
target_end()

-- bin2c
target("bin2c")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/bin2c/*.c")
    add_includedirs("tools/src/bin2c")
target_end()

-- gbafix
target("gbafix")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/gbafix/*.c")
    add_includedirs("tools/src/gbafix")
target_end()

-- gbagfx
target("gbagfx")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/gbagfx/*.c")
    add_includedirs("tools/src/gbagfx")
    add_packages("libpng", "zlib")
target_end()

-- mid2agb
target("mid2agb")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/mid2agb/*.cpp")
    add_includedirs("tools/src/mid2agb")
target_end()

-- preproc
target("preproc")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/preproc/*.cpp")
    add_includedirs("tools/src/preproc")
target_end()

-- scaninc
target("scaninc")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/scaninc/*.cpp")
    add_includedirs("tools/src/scaninc")
    add_packages("fmt")
target_end()

-- tmc_strings
target("tmc_strings")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/tmc_strings/*.cpp")
    add_includedirs("tools/src/tmc_strings")
    add_packages("nlohmann_json", "fmt")
target_end()

-- Group all tools
target("tools")
    set_kind("phony")
    add_deps("agb2mid", "aif2pcm", "asset_processor", "bin2c", "gbafix", "gbagfx", "mid2agb", "preproc", "scaninc", "tmc_strings")
target_end()

-- ====================
-- Asset Tasks
-- ====================

-- Extract assets task
task("extract_assets")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        config.load()
        
        local game_version = get_config("game_version") or "USA"
        local build_assets_dir = "build/" .. game_version .. "/assets"
        
        print("===========================================")
        print("Extracting assets for " .. game_version)
        print("===========================================")
        
        -- Build tools first
        print("[1/3] Building tools...")
        os.exec("xmake build tools")
        print("[1/3] Tools built successfully!")
        
        -- Create build directory
        print("[2/3] Creating build directory: " .. build_assets_dir)
        os.mkdir(build_assets_dir)
        
        -- Run asset_processor extract
        print("[3/3] Running asset_processor extract (verbose mode)...")
        print("-------------------------------------------")
        os.execv("tools/bin/asset_processor", {"-v", "extract", game_version, build_assets_dir})
        print("-------------------------------------------")
        
        print("===========================================")
        print("Assets extracted to " .. build_assets_dir)
        print("===========================================")
    end)
    set_menu {
        usage = "xmake extract_assets [options]",
        description = "Extract assets from baserom",
        options = {}
    }
task_end()

-- Convert assets task  
task("convert_assets")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        config.load()
        
        local game_version = get_config("game_version") or "USA"
        local build_assets_dir = "build/" .. game_version .. "/assets"
        
        print("===========================================")
        print("Converting assets for " .. game_version)
        print("===========================================")
        
        -- Build tools first
        print("[1/3] Building tools...")
        os.exec("xmake build tools")
        print("[1/3] Tools built successfully!")
        
        -- Create build directory
        print("[2/3] Checking build directory: " .. build_assets_dir)
        os.mkdir(build_assets_dir)
        
        -- Run asset_processor convert
        print("[3/3] Running asset_processor convert (verbose mode)...")
        print("-------------------------------------------")
        os.execv("tools/bin/asset_processor", {"-v", "convert", game_version, build_assets_dir})
        print("-------------------------------------------")
        
        print("===========================================")
        print("Assets converted to " .. build_assets_dir)
        print("===========================================")
    end)
    set_menu {
        usage = "xmake convert_assets [options]",
        description = "Convert extracted assets",
        options = {}
    }
task_end()

-- Build assets task
task("build_assets")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        config.load()
        
        local game_version = get_config("game_version") or "USA"
        local build_assets_dir = "build/" .. game_version .. "/assets"
        
        -- Build tools first
        os.exec("xmake build tools")
        
        -- Create build directory
        os.mkdir(build_assets_dir)
        
        -- Run asset_processor build
        os.execv("tools/bin/asset_processor", {"build", game_version, build_assets_dir})
        
        print("Assets built to " .. build_assets_dir)
    end)
    set_menu {
        usage = "xmake build_assets [options]",
        description = "Build assets from source",
        options = {}
    }
task_end()

-- ====================
-- PC Port Target
-- ====================
target("tmc_pc")
    set_kind("binary")
    set_languages("c11", "cxx17")
    set_targetdir("build/pc")
    
    -- PC port version configurations
    local pc_versions = {
        USA = { region = "USA", language = "ENGLISH" },
        EU  = { region = "EU",  language = "ENGLISH" },
    }
    local pc_game_version = get_config("game_version") or "USA"
    local pc_ver = pc_versions[pc_game_version] or pc_versions["USA"]
    
    -- Define PC_PORT, NON_MATCHING and game version
    add_defines("PC_PORT", "NON_MATCHING", pc_ver.region, pc_ver.language, "REVISION=0")
    
    -- Include directories
    add_includedirs("include", "libs")
    add_includedirs("port")
    add_includedirs(".")
    add_includedirs("build/" .. pc_game_version) -- For assets/map_offsets.h etc (USA or EU)
    add_includedirs("libs/ViruaPPU/include")     -- ViruaPPU PPU renderer

    

    add_files("port/port_main.c")
    add_files("port/port_ppu.cpp")      -- PPU bridge (C++ → ViruaPPU)
    add_files("port/port_rom.c")        -- ROM loading & symbol resolution
        -- PC port stubs for undefined symbols
    add_files("port/port_stubs.c")
    add_files("port/stubs_autogen.c")
    add_files("port/data_stubs_autogen.c")
    add_files("port/port_bios.c")
    add_files("port/port_linked_stubs.c")
    add_files("port/port_draw.c")
    add_files("port/port_gba_mem.c")
    add_files("port/port_save.c")        -- EEPROM save emulation
    add_files("port/port_animation.c")   -- Animation system (ported from ASM)
    add_files("port/port_math.c")        -- Math functions (CalcDistance, direction, Sqrt, Div)
    add_files("port/port_text_render.c") -- Text rendering (UnpackTextNibbles, glyph pixel writers)
    add_files("port/port_gameplay_stubs.c") -- Ported gameplay helpers from ASM (tile interaction, ice movement, SFX queue)
    add_files("port/port_room_funcs.c") -- Room function pointer lookup table (generated)
    add_files("port/port_script_funcs.c") -- Script Call/CallWithArg function lookup (generated)
    
    -- Game source files - Main game code
    add_files("src/main.c")
    add_files("src/common.c")
    add_files("src/interrupts.c")
    add_files("src/game.c")
    add_files("src/title.c")
    add_files("src/fileselect.c")
    add_files("src/flags.c")
    add_files("src/save.c")
    add_files("src/fade.c")
    add_files("src/color.c")
    add_files("src/vram.c")
    add_files("src/screen*.c")
    add_files("src/message.c")
    add_files("src/text.c")
    add_files("src/sound.c")
    add_files("src/script.c")
    add_files("src/scroll.c")
    add_files("src/room.c")
    add_files("src/roomInit.c")
    add_files("src/entity.c")
    add_files("src/physics.c")
    add_files("src/collision.c")
    add_files("src/movement.c")
    add_files("src/affine.c")
    add_files("src/sineTable.c")
    add_files("src/ui.c")
    
    -- Player
    add_files("src/player.c")
    add_files("src/playerUtils.c")
    add_files("src/playerHitbox.c")
    add_files("src/playerItem.c")
    add_files("src/playerItemUtils.c")
    add_files("src/playerItemDefinitions.c")
    add_files("src/playerItem/*.c")
    
    -- Entities
    add_files("src/enemy.c")
    add_files("src/enemyUtils.c")
    add_files("src/enemy/*.c")
    add_files("src/npc.c")
    add_files("src/npcUtils.c")
    add_files("src/npcFunctions.c")
    add_files("src/npcDefinitions.c")
    add_files("src/npc/*.c")
    add_files("src/object.c")
    add_files("src/objectUtils.c")
    add_files("src/objectDefinitions.c")
    add_files("src/object/*.c")
    add_files("src/projectile.c")
    add_files("src/projectileUtils.c")
    add_files("src/projectile/*.c")
    add_files("src/manager.c")
    add_files("src/manager/*.c")
    add_files("src/item.c")
    add_files("src/itemUtils.c")
    add_files("src/itemDefinitions.c")
    add_files("src/itemMetaData.c")
    add_files("src/item/*.c")
    
    -- Other game systems
    add_files("src/subtask.c")
    add_files("src/subtask/*.c")
    add_files("src/cutscene.c")  -- re-enabled for PC port
    add_files("src/backgroundAnimations.c")
    add_files("src/beanstalkSubtask.c")
    add_files("src/enterPortalSubtask.c")
    add_files("src/gameOverTask.c")
    add_files("src/gameUtils.c")
    add_files("src/gameData.c")
    add_files("src/kinstone.c")
    add_files("src/droptables.c")
    add_files("src/demo.c")
    add_files("src/debug.c")
    add_files("src/flagDebug.c")
    add_files("src/staffroll.c")
    add_files("src/menu/*.c")
    add_files("src/data/figurineMenuData.c")
    add_files("src/worldEvent/*.c")
    
    -- Code stubs (functions that need decompilation)
    add_files("src/code_08049CD4.c")
    add_files("src/code_08049DF4.c")
    add_files("src/code_0805EC04.c")
    

    
    -- GBA library (m4a sound) - skipped for PC, using stubs
    -- add_files("src/gba/m4a.c")
    
    add_packages("libsdl3")
    
    -- Math library
    if is_plat("linux", "macosx") then
        add_links("m")
    end
    
    -- Compiler flags
    add_cflags("-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers",
               "-fno-strict-aliasing", "-fwrapv", "-fno-strict-overflow", "-O0", "-g")
    add_cxxflags("-Wall", "-Wextra", "-Wno-unused-parameter",
                 "-fno-strict-aliasing", "-fwrapv", "-fno-strict-overflow", "-O3", "-g")
target_end()


-- ====================
-- ROM Build Task
-- ====================
task("rom")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        import("lib.detect.find_program")
        import("async.runjobs")
        config.load()
        
        -- Number of parallel jobs (default: number of CPU cores)
        local njobs = tonumber(os.getenv("XMAKE_JOBS")) or os.cpuinfo().ncpu or 8
        
        local game_version = get_config("game_version") or "USA"
        local build_dir = "build/" .. game_version
        local assets_dir = build_dir .. "/assets"
        
        -- Game version configurations
        local versions = {
            USA = { code = "BZME", name = "tmc", language = "ENGLISH" },
            EU = { code = "BZMP", name = "tmc_eu", language = "ENGLISH" },
            JP = { code = "BZMJ", name = "tmc_jp", language = "JAPANESE" },
            DEMO_USA = { code = "BZHE", name = "tmc_demo_usa", language = "ENGLISH" },
            DEMO_JP = { code = "BZMJ", name = "tmc_demo_jp", language = "JAPANESE" }
        }
        
        local ver = versions[game_version]
        if not ver then
            print("Error: Unknown game version: " .. game_version)
            return
        end
        
        local rom_name = ver.name .. ".gba"
        local elf_name = ver.name .. ".elf"
        
        print("===========================================")
        print("Building ROM for " .. game_version)
        print("ROM: " .. rom_name)
        print("===========================================")
        
        -- Check for arm-none-eabi toolchain
        local arm_gcc = find_program("arm-none-eabi-gcc")
        if not arm_gcc then
            -- Try DevkitARM
            local devkitarm = os.getenv("DEVKITARM")
            if devkitarm then
                arm_gcc = path.join(devkitarm, "bin", "arm-none-eabi-gcc")
            end
        end
        if not arm_gcc then
            -- Try common Windows installation paths
            local common_paths = {
                "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe",
                "C:/Program Files/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe",
                "C:/devkitPro/devkitARM/bin/arm-none-eabi-gcc.exe"
            }
            for _, p in ipairs(common_paths) do
                if os.isfile(p) then
                    arm_gcc = p
                    break
                end
            end
        end
        
        if not arm_gcc or (not os.isfile(arm_gcc .. ".exe") and not os.isfile(arm_gcc)) then
            print("Error: arm-none-eabi-gcc not found!")
            print("Please install DevkitARM or arm-none-eabi toolchain")
            return
        end
        
        local toolchain_dir = path.directory(arm_gcc)
        local as_cmd = path.join(toolchain_dir, "arm-none-eabi-as")
        local ld_cmd = path.join(toolchain_dir, "arm-none-eabi-ld")
        local objcopy_cmd = path.join(toolchain_dir, "arm-none-eabi-objcopy")
        local cpp_cmd = arm_gcc
        
        -- Check agbcc
        local agbcc = "tools/agbcc/bin/agbcc"
        if is_host("windows") then
            agbcc = agbcc .. ".exe"
        end
        if not os.isfile(agbcc) then
            print("Error: agbcc not found at " .. agbcc)
            return
        end
        
        print("[1/9] Building tools...")
        os.exec("xmake build tools")
        
        print("[2/9] Checking assets...")
        if not os.isdir(assets_dir) then
            print("  Assets not found, extracting...")
            os.exec("xmake extract_assets")
        end
        
        -- Create build directories
        os.mkdir(build_dir .. "/src")
        os.mkdir(build_dir .. "/src/gba")
        os.mkdir(build_dir .. "/asm")
        os.mkdir(build_dir .. "/asm/lib/src")
        os.mkdir(build_dir .. "/data")
        os.mkdir(build_dir .. "/enum_include")
        
        -- Common flags
        local asinclude = "-I " .. assets_dir .. " -I " .. build_dir .. "/enum_include"
        local asflags = "-mcpu=arm7tdmi --defsym " .. game_version .. "=1 --defsym REVISION=0 --defsym " .. ver.language .. "=1 " .. asinclude
        local cinclude = "-I include -I " .. build_dir
        local cppflags = "-I tools/agbcc -I tools/agbcc/include " .. cinclude .. " -nostdinc -undef -D" .. game_version .. " -DREVISION=0 -D" .. ver.language
        local cflags = "-O2 -Wimplicit -Wparentheses -Werror -Wno-multichar -g3"
        
        -- Interwork files need special flags
        local interwork_files = {
            "src/interrupts.c", "src/collision.c", "src/playerItem.c",
            "src/object.c", "src/manager.c", "src/npc.c", "src/gba/m4a.c"
        }
        
        print("[3/9] Generating enum includes...")
        -- Generate enum includes from headers using gcc (not arm-none-eabi-gcc)
        local gcc_cmd = "gcc"
        local headers = os.files("include/*.h")
        local enum_count = 0
        for _, header in ipairs(headers) do
            local filename = path.filename(header)
            local inc_file = build_dir .. "/enum_include/" .. filename:gsub("%.h$", ".inc")
            -- Use os.iorunv to capture output and write to file
            local output, err = os.iorunv("python", {
                "tools/extract_include_enum.py",
                header:gsub("\\", "/"),  -- Use forward slashes for gcc
                gcc_cmd,
                "-D__attribute__(x)=",
                "-D" .. game_version,
                "-E",
                "-nostdinc",
                "-Itools/agbcc",
                "-Itools/agbcc/include",
                "-iquote",
                "include"
            })
            if output and #output > 0 then
                -- Write with ASCII encoding (no BOM) to avoid assembler issues
                io.writefile(inc_file, output)
                enum_count = enum_count + 1
            end
        end
        print("  Generated " .. enum_count .. " enum include files")
        
        print("[4/9] Compiling translations...")
        -- Compile translation JSON files to binary
        local translations = {"English", "French", "German", "Spanish", "Italian"}
        for _, lang in ipairs(translations) do
            local json_file = "translations/" .. lang .. ".json"
            local bin_file = "translations/" .. lang .. ".bin"
            if os.isfile(json_file) and not os.isfile(bin_file) then
                os.execv("tools/bin/tmc_strings", {"-p", "--source", json_file, "--dest", bin_file})
                print("  Compiled: " .. lang .. ".bin")
            end
        end
        
        print("[5/9] Preprocessing linker script...")
        -- Preprocess linker script using os.execv to handle paths with spaces
        os.execv(cpp_cmd, {
            "-I", "tools/agbcc",
            "-I", "tools/agbcc/include",
            "-I", "include",
            "-I", build_dir,
            "-nostdinc",
            "-undef",
            "-D" .. game_version,
            "-DREVISION=0",
            "-D" .. ver.language,
            "-E",
            "-x", "c",
            "linker.ld",
            "-o", build_dir .. "/linker_pp.ld"
        })
        -- Remove preprocessor lines
        local linker_content = io.readfile(build_dir .. "/linker_pp.ld")
        linker_content = linker_content:gsub("#[^\n]*\n", "")
        io.writefile(build_dir .. "/linker.ld", linker_content)
        
        print("[6/9] Checking libc.a...")
        -- Ensure libc.a exists (needed for linking)
        local libc_path = "tools/agbcc/lib/libc.a"
        if not os.isfile(libc_path) then
            print("  Building minimal libc.a...")
            os.mkdir("tools/agbcc/lib")
            local old_agbcc = "tools/agbcc/bin/old_agbcc"
            if is_host("windows") then
                old_agbcc = old_agbcc .. ".exe"
            end
            if os.isfile(old_agbcc) then
                -- Compile memcpy.c from agbcc libc
                local memcpy_src = "tools/agbcc/libc/string/memcpy.c"
                if os.isfile(memcpy_src) then
                    os.execv(old_agbcc, {"-O2", "-o", "tools/agbcc/lib/memcpy.s", memcpy_src})
                    os.execv(as_cmd, {"-mcpu=arm7tdmi", "-o", "tools/agbcc/lib/memcpy.o", "tools/agbcc/lib/memcpy.s"})
                    local ar_cmd = path.join(toolchain_dir, "arm-none-eabi-ar")
                    os.execv(ar_cmd, {"rcs", libc_path, "tools/agbcc/lib/memcpy.o"})
                    print("  Created libc.a with memcpy")
                end
            else
                print("  Warning: old_agbcc not found, cannot build libc.a")
            end
        end
        
        print("[7/9] Compiling C files (" .. njobs .. " jobs)...")
        -- Compile C files in parallel
        local c_files = os.files("src/**.c")
        local obj_files = {}
        
        -- Create all directories first
        for _, cfile in ipairs(c_files) do
            local rel_path = path.relative(cfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".o")
            os.mkdir(path.directory(obj_path))
            table.insert(obj_files, obj_path)
        end
        
        -- Compile in parallel
        local compiled_count = 0
        runjobs("compile_c", function (index)
            local cfile = c_files[index]
            local rel_path = path.relative(cfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".o")
            local i_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".i")
            local s_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".s")
            
            -- Check if interwork
            local extra_cflags = {"-O2", "-Wimplicit", "-Wparentheses", "-Werror", "-Wno-multichar", "-g3"}
            for _, iw in ipairs(interwork_files) do
                if rel_path:gsub("\\", "/") == iw then
                    table.insert(extra_cflags, "-mthumb-interwork")
                    break
                end
            end
            if rel_path:find("eeprom%.c") then
                extra_cflags = {"-O1", "-Wimplicit", "-Wparentheses", "-Werror", "-Wno-multichar", "-g3", "-mthumb-interwork"}
            end
            
            -- Preprocess
            os.execv(cpp_cmd, {
                "-I", "tools/agbcc",
                "-I", "tools/agbcc/include",
                "-I", "include",
                "-I", build_dir,
                "-nostdinc",
                "-undef",
                "-D" .. game_version,
                "-DREVISION=0",
                "-D" .. ver.language,
                "-E",
                cfile,
                "-o", i_path
            })
            
            -- Compile with agbcc
            local agbcc_args = {}
            for _, flag in ipairs(extra_cflags) do
                table.insert(agbcc_args, flag)
            end
            table.insert(agbcc_args, "-o")
            table.insert(agbcc_args, s_path)
            table.insert(agbcc_args, i_path)
            os.execv("tools/agbcc/bin/agbcc", agbcc_args)
            
            -- Append alignment
            local f = io.open(s_path, "a")
            f:write("\t.text\n\t.align\t2, 0 @ Don't pad with nop\n")
            f:close()
            
            -- Assemble
            os.execv(as_cmd, {
                "-mcpu=arm7tdmi",
                "--defsym", game_version .. "=1",
                "--defsym", "REVISION=0",
                "--defsym", ver.language .. "=1",
                "-I", assets_dir,
                "-I", build_dir .. "/enum_include",
                "-o", obj_path,
                s_path
            })
        end, {total = #c_files, comax = njobs})
        print("  Compiled " .. #c_files .. " C files")
        
        print("[8/9] Assembling ASM files (" .. njobs .. " jobs)...")
        -- Assemble ASM files in parallel
        local asm_files = os.files("asm/**.s")
        table.join2(asm_files, os.files("data/**.s"))
        
        -- Create all directories first
        for _, asmfile in ipairs(asm_files) do
            local rel_path = path.relative(asmfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.s$", ".o")
            os.mkdir(path.directory(obj_path))
            table.insert(obj_files, obj_path)
        end
        
        -- Assemble in parallel
        runjobs("assemble_asm", function (index)
            local asmfile = asm_files[index]
            local rel_path = path.relative(asmfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.s$", ".o")
            
            -- Preprocess and assemble
            local asm_output = os.iorunv("tools/bin/preproc", {
                ver.name,
                asmfile,
                "--",
                "-I", assets_dir,
                "-I", build_dir .. "/enum_include"
            })
            
            -- Write preprocessed output and assemble
            local pp_path = obj_path:gsub("%.o$", ".pp.s")
            -- Convert CRLF to LF for assembler compatibility
            if asm_output then
                asm_output = asm_output:gsub("\r\n", "\n")
            end
            io.writefile(pp_path, asm_output)
            
            os.execv(as_cmd, {
                "-mcpu=arm7tdmi",
                "--defsym", game_version .. "=1",
                "--defsym", "REVISION=0",
                "--defsym", ver.language .. "=1",
                "-I", assets_dir,
                "-I", build_dir .. "/enum_include",
                "-o", obj_path,
                pp_path
            })
        end, {total = #asm_files, comax = njobs})
        print("  Assembled " .. #asm_files .. " ASM files")
        
        print("[9/9] Linking...")
        -- Link - need to run from build dir for relative paths in linker script
        local old_dir = os.cd(build_dir)
        os.execv(ld_cmd, {
            "-Map", ver.name .. ".map",
            "-n",
            "-T", "linker.ld",
            "-o", "../../" .. elf_name,
            "-L", "../../tools/agbcc/lib",
            "-lc"
        })
        os.cd(old_dir)
        
        -- Convert to GBA ROM binary
        os.execv(objcopy_cmd, {
            "-O", "binary",
            "--gap-fill", "0xFF",
            "--pad-to", "0x9000000",
            elf_name,
            rom_name
        })
        
        -- Fix ROM header
        os.execv("tools/bin/gbafix", {
            rom_name,
            "-tGBAZELDA MC",
            "-c" .. ver.code,
            "-m01",
            "-r0",
            "--silent"
        })
        
        print("===========================================")
        print("ROM built successfully: " .. rom_name)
        print("===========================================")
    end)
    set_menu {
        usage = "xmake rom [options]",
        description = "Build the GBA ROM",
        options = {}
    }
task_end()
