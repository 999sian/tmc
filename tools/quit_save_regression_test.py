#!/usr/bin/env python3
"""Save failure must keep both the game and the quit dialog open."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from voxel_regression_test import function

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'port/port_imgui_menu.cpp').read_text()
fixture = '''#include <cassert>
#include <cstring>
struct ImVec2 { float x,y; ImVec2(float a,float b):x(a),y(b){} };
constexpr int ImGuiCond_Appearing=1;
constexpr int ImGuiWindowFlags_AlwaysAutoResize=1,ImGuiWindowFlags_NoCollapse=2;
static bool saveOk, closed, toggled, toasted;
static int task=-1;
static int Port_QuickSave_SaveSlot(int slot) { assert(slot==0); return saveOk; }
static void Port_DebugMenu_ToastFromExternal(const char*) { toasted=true; }
static void SetTask(unsigned int value) { task=value; }
static void Port_DebugMenu_Toggle() { toggled=true; }
static bool sQuitModalArmed=false, sQuitModalConfirmed=false;
namespace ImGui {
struct Viewport { ImVec2 GetCenter() { return ImVec2(0,0); } } viewport;
static Viewport* GetMainViewport() { return &viewport; }
static void OpenPopup(const char*) {}
static void SetNextWindowPos(ImVec2,int,ImVec2) {}
static bool BeginPopupModal(const char*,void*,int) { return true; }
static void TextUnformatted(const char*) {}
static void TextWrapped(const char*) {}
static void Separator() {}
static void Spacing() {}
static bool Button(const char* text,ImVec2) { return std::strcmp(text,"Save & Quit")==0; }
static void CloseCurrentPopup() { closed=true; }
static void SameLine() {}
static void EndPopup() {}
}
''' + function(source, 'static void DoQuitToTitle(bool saveFirst) {') + '\n' + function(source, 'static void DrawQuitModal(void) {') + '''
int main() {
 saveOk=false; DoQuitToTitle(true); assert(task==-1 && !toggled && toasted);
 toasted=false; DrawQuitModal(); assert(!sQuitModalConfirmed && !closed && toasted);
 saveOk=true; DoQuitToTitle(true); assert(task==0 && toggled);
 DrawQuitModal(); assert(sQuitModalConfirmed && closed);
 saveOk=false; task=-1; DoQuitToTitle(false); assert(task==0);
}
'''
with tempfile.TemporaryDirectory(prefix='tmc-quit-save-') as directory:
    tmp=Path(directory)
    (tmp/'test.cpp').write_text(fixture)
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('quit-save failure handling: PASS')
