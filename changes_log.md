# Changelog - Level Editor Project (The Minish Cap PC Port)

This file registers the solutions and improvements implemented in the PC port of *The Minish Cap* to integrate the level editor and fix stability and same-area transition rendering issues.

## New Features

### 0. Integration of the Level Editor (Direct Painting Overlay)
* **Added/Affected Files:** `port/port_level_editor.cpp`, `port/port_level_editor.h`
* **Description:** Integrated a complete interactive real-time map editor right onto the game screen. Features include:
  * Direct tile painting with the mouse (left-click to paint, drag to continuously draw).
  * Eyedropper tool to sample tiles directly from the screen using right-click.
  * Keyboard hotkeys to toggle between the Top and Bottom layers, increment/decrement active tile ID, and dynamically cycle room lighting and music (BGM & Fight BGM).
  * Instant, persistent saving to the local `edited_levels/areaXX_roomXX.bin` directory with associated metadata.

## Fixes and Improvements

### 1. Boot Crash Fix (Memory Alignment Correction)
* **Affected File:** `port/port_level_editor.cpp` (`MapLayer` struct)
* **Issue:** The PC port redefined the `MapLayer` struct in C++ but omitted the `void* bgSettings;` pointer at the beginning of the struct. This caused an 8-byte misalignment for all subsequent struct members. When the layout loader wrote to `gMapBottom.mapData`, it corrupted `bgSettings`, resulting in a segmentation fault (Access Violation) during `UpdateScreenShake()`.
* **Solution:** Restored `void* bgSettings;` to the front of `MapLayer` in C++, guaranteeing structure compatibility and memory alignment with the original C game engine.

### 2. Out-of-Bounds Index Guard in Map Rendering
* **Affected File:** `src/beanstalkSubtask.c` (`RenderMapLayerToSubTileMap` function)
* **Issue:** In custom map layouts, certain tiles return special values (such as `0xFFFF`). During metatile lookups, this caused out-of-bounds array reads in the `tileIndices` table (indices `>= 2048`).
* **Solution:** Implemented a `GetSafeTileSetIndex` helper function to safely check indices, preventing buffer-overflow reads during metatile indexing.

### 3. Transition Scroll Black Screen Bug Fix (Same-Area Rooms)
* **Affected File:** `port/port_level_editor.cpp` (`Port_LevelEditor_OnRoomLoad` function)
* **Issue:** During same-area camera scroll transitions, the engine uses the first half of the map buffers (`gMapBottom.mapData`, `gMapTop.mapData`, and `Original` counterparts) for the new room, and the second half to store the layout of the leaving room. Overwriting the entire 4096-element (64x64 grid) buffers wiped out the leaving room's tiles to zero, causing the old room to turn pitch black during scroll transitions.
* **Solution:** 
  1. Modified the loader to **dynamically** read and copy only the row bounds needed by the current room: `(gRoomControls.height / 16) * 64` elements.
  2. This allows large rooms to load fully, while smaller rooms leave the second half of the buffers untouched, preserving the adjacent room graphics during scrolling.

### 4. Room BGM and Light Level Corruption Fix
* **Affected File:** `port/port_level_editor.cpp` (`Port_LevelEditor_OnRoomLoad` function)
* **Issue:** Aligning the map reads to only the occupied rows left the file read pointer offset. The metadata loader read room BGM and lighting values from the middle of the tile index data. This corrupted the light level (e.g. loading `129` instead of `256`), rendering the entire room pitch black (except for sprites).
* **Solution:** Forced an absolute seek `input.seekg(16384, std::ios::beg)` to the end of the map layers before reading BGM and light level properties. A validation guard was also added to discard invalid values on previously corrupted saves.

### 5. Safe VRAM Syncing during Room Load
* **Affected File:** `port/port_level_editor.cpp` (`Port_LevelEditor_OnRoomLoad` function)
* **Issue:** Calling `UpdateScrollVram()` synchronously in `OnRoomLoad` during room transitions (when the camera scroll registers are not yet synced to the new coordinates) forced VRAM updates with offset/blank tiles, causing black screens.
* **Solution:** Retained EWRAM collision and map updates, but removed the manual synchronous VRAM update call, allowing the main engine loop to safely upload EWRAM contents to VRAM on the subsequent frame once coordinates are synchronized.

### 6. Debug ImGui Tab for Map Editor Controls
* **Affected File:** `port/port_imgui_menu.cpp`
* **Issue:** Keyboard hotkeys, eyedropper controls, and the level editor state toggle were not documented or exposed in the PC port's debug menu.
* **Solution:** Implemented a new `"Map Editor"` tab in the ImGui ribbon debug menu with a checkbox to toggle the direct painting overlay and a detailed list of controls and hotkeys. Connected the drawing callback to the screen rendering pipeline in `Port_ImGui_Render()`.
