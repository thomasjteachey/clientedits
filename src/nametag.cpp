// nametag.cpp - stage 1 of the world/tournament/bot marker: a read-only probe
// on the per-unit name tag, plus the toggle the finished feature will use.
//
// REVERSING (Wow.exe 3.3.5a 12340, VAs; full notes in docs/NAME_TAG_RENDER_RE.md)
// -----------------------------------------------------------------------------
//   0x00D380A0  the name-display flags: every UnitName* CVar is one bit in it
//               (Own 1, NPC 2, PlayerGuild 4, PlayerPVPTitle 8). Registered at
//               0x007E6150, all through the callback 0x007E60E0, which is also
//               the shape this file copies to register its own CVar.
//   0x007E5640  THE PER-UNIT NAME TAG. __thiscall. this = a tag record:
//                 +0x10/+0x14  guid of the unit it belongs to
//                 +0x18        flags; bit 1 consumed and cleared at 0x007E56C0
//               It resolves the unit through 0x004D4DB0, asks
//               [unitVtable + 0xD0] what to show (the 0x00D380A0 mask in ecx)
//               and splits the answer into `& 3` and `>> 2` - the name line and
//               the guild line under it - then emits four 0x18-byte vertices
//               into the batch at [0x00C5DF88].
//               Nothing CALLS it: it is reached through the dispatch table at
//               0x00D380B0, so the hook goes on the function itself.
//   0x004D4DB0  ObjectPtr(guidLo, guidHi, typeMask, file, line) -> unit or null
//   0x00767FC0  CVar registration, cdecl, 9 args, caller cleans 0x24
//
// WHAT THE PROBE IS FOR
// ---------------------
// Two things static reading could not settle. First, where the letters are
// drawn: the quad this function emits carries a colour whose only varying
// component is alpha, which reads as a backdrop rather than glyphs, so the text
// is emitted further on or by whatever consumes the batch. Second, where the
// screen position comes from: 0x007E52A0 nearby is only a distance gate
// (camera at [0x00B7436C] + 0x7E20), not a projection, and the position globals
// at 0x00AF46AC are already written by the time this runs.
//
// So the probe logs, for the first few tags of a session: the tag record, the
// guid, the unit and the unit's vtable. The vtable gives [+0xD0] as a concrete
// address to go and read, which is the shortest path to both answers.
//
// It writes nothing into the client and changes no behaviour: the hook runs the
// original prologue and jumps back.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "nametag.h"

static char g_dir[MAX_PATH] = { 0 };
static int  g_enabled = 0;          // [NameTag] Enabled
static int  g_probeLines = 12;      // [NameTag] ProbeLines, 0 = no probe logging
static int  g_defaultOn = 1;        // [NameTag] DefaultOn -> the CVar's default
static int  g_cvarValue = 1;        // what the CVar currently says
static int  g_probed = 0;

static const DWORD kNameTagSite = 0x007E5640;
static const BYTE  kNameTagSig[] = {
    0x55,                               // push ebp
    0x8B, 0xEC,                         // mov ebp, esp
    0x81, 0xEC, 0xEC, 0x04, 0x00, 0x00, // sub esp, 0x4EC
    0x56,                               // push esi
    0x8B, 0xF1                          // mov esi, ecx
};
// The whole prologue above is relocation-free, so all nine bytes before the
// `push esi` can be stolen for the jmp and replayed in the trampoline.
static const size_t kStolen = 9;

// Both read off the file with peexplore (disasm), not remembered: the first
// build guessed ObjectPtr's prologue and the fail-safe refused to install.
static const DWORD kObjectPtr    = 0x004D4DB0;
static const BYTE  kObjectPtrSig[] = {
    0x55,                                   // push ebp
    0x8B, 0xEC,                             // mov ebp, esp
    0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00 // mov ecx, fs:[0x2C]  (the objmgr is per-thread)
};
static const DWORD kCVarRegister = 0x00767FC0;
static const BYTE  kCVarRegSig[] = {
    0x55,                                   // push ebp
    0x8B, 0xEC,                             // mov ebp, esp
    0x83, 0xEC, 0x08,                       // sub esp, 8
    0x53                                    // push ebx
};
static const DWORD kNameFlags    = 0x00D380A0;

typedef void* (__cdecl* ObjectPtr_t)(DWORD guidLo, DWORD guidHi, int typeMask,
                                     const char* file, int line);

// ------------------------------------------------------------------- logging
static void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sAnimSpeedFix.log", g_dir);

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(msg, sizeof(msg) - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)strlen(msg);

    char line[600];
    int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "[nametag pid%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (len < 0) return;

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, line, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

static bool Readable(void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (SIZE_T)((BYTE*)p - (BYTE*)mbi.BaseAddress) + n <= mbi.RegionSize;
}

static bool VerifySig(DWORD addr, const BYTE* sig, size_t n, const char* name)
{
    if (!Readable((void*)addr, n)) {
        Log("0x%08X (%s) not readable - disabled", addr, name);
        return false;
    }
    if (memcmp((void*)addr, sig, n) != 0) {
        Log("0x%08X (%s) signature mismatch - disabled (wrong Wow.exe build?)", addr, name);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the toggle
//
// The client's own UnitName* CVars are registered at 0x007E6150 as:
//
//     push 0 / push <userArg> / push 0 / push 4 / push <callback>
//     push <defaultValue> / push 0x10 / push 0 / push <name>
//     call 0x00767FC0                       (cdecl, caller cleans 0x24)
//
// and their shared callback reads the new value at [ebp+0x10] and its own
// userArg at [ebp+0x14]. This registers `centurionNameTags` the same way, with
// a callback that keeps the value here. Until the first run confirms the
// argument order, the ini default is what the marker actually obeys - hence
// NameTag_Enabled() preferring the CVar only once the callback has spoken.
static int g_cvarSpoke = 0;

static void __cdecl CVarChanged(void* cvar, void* a, const char* newValue, int userArg)
{
    (void)cvar; (void)a; (void)userArg;
    if (newValue && *newValue)
        g_cvarValue = (newValue[0] != '0');
    g_cvarSpoke = 1;
    Log("centurionNameTags -> %d", g_cvarValue);
}

typedef void* (__cdecl* CVarRegister_t)(const char* name, const char* help, int flags,
                                        const char* defaultValue, void* callback,
                                        int category, int unk0, int userArg, int unk1);

static void RegisterCVar()
{
    if (!VerifySig(kCVarRegister, kCVarRegSig, sizeof(kCVarRegSig), "CVar::Register"))
        return;

    CVarRegister_t reg = (CVarRegister_t)kCVarRegister;
    void* cvar = reg("centurionNameTags",
                     "show World / Tournament / Bot above player names",
                     0x10,
                     g_defaultOn ? "1" : "0",
                     (void*)&CVarChanged,
                     4, 0, 1, 0);
    Log("registered centurionNameTags (cvar=%p, default %d)", cvar, g_defaultOn);
}

bool NameTag_Enabled()
{
    if (!g_enabled) return false;
    return g_cvarSpoke ? (g_cvarValue != 0) : (g_defaultOn != 0);
}

// ----------------------------------------------------------------- the probe
//
// Called with the tag record before the original body runs. Read-only.
extern "C" void __cdecl NameTagProbe(void* tag)
{
    if (g_probed >= g_probeLines) return;
    if (!Readable(tag, 0x20)) return;

    DWORD guidLo = *(DWORD*)((BYTE*)tag + 0x10);
    DWORD guidHi = *(DWORD*)((BYTE*)tag + 0x14);
    DWORD flags  = *(DWORD*)((BYTE*)tag + 0x18);
    if (!guidLo && !guidHi) return;

    void* unit = NULL;
    void* vtable = NULL;
    void* nameFn = NULL;
    ObjectPtr_t objectPtr = (ObjectPtr_t)kObjectPtr;
    unit = objectPtr(guidLo, guidHi, 1, "nametag.cpp", 0);
    if (unit && Readable(unit, 4)) {
        vtable = *(void**)unit;
        if (vtable && Readable(vtable, 0xD4))
            nameFn = *(void**)((BYTE*)vtable + 0xD0);
    }

    ++g_probed;
    Log("tag=%p guid=%08X%08X flags=0x%X unit=%p vtable=%p vtable+0xD0=%p mask=0x%X",
        tag, guidHi, guidLo, flags, unit, vtable, nameFn, *(DWORD*)kNameFlags);

    if (g_probed == g_probeLines)
        Log("probe budget spent; go read vtable+0xD0 and what it returns");
}

// The hook: log, then run the stolen prologue and jump back past it.
static DWORD g_resume = kNameTagSite + kStolen;

__declspec(naked) static void NameTagHook()
{
    __asm {
        pushad
        pushfd
        push ecx                    // this (the tag record)
        call NameTagProbe
        add  esp, 4
        popfd
        popad

        push ebp                    // the stolen prologue, replayed
        mov  ebp, esp
        sub  esp, 0x4EC
        jmp  [g_resume]
    }
}

// ------------------------------------------------------------------ install
static bool WriteJump(DWORD at, void* to)
{
    DWORD old = 0;
    if (!VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    BYTE* p = (BYTE*)at;
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)to - (at + 5);
    // The remaining stolen bytes become one-byte nops so a debugger reading the
    // site does not show half an instruction.
    for (size_t i = 5; i < kStolen; ++i) p[i] = 0x90;
    DWORD ignored = 0;
    VirtualProtect((void*)at, 5, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, kStolen);
    return true;
}

void NameTag_LoadSettings(const char* dir)
{
    if (dir) {
        strncpy_s(g_dir, sizeof(g_dir), dir, _TRUNCATE);
    }

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);

    g_enabled    = GetPrivateProfileIntA("NameTag", "Enabled", 0, ini);
    g_probeLines = GetPrivateProfileIntA("NameTag", "ProbeLines", 12, ini);
    g_defaultOn  = GetPrivateProfileIntA("NameTag", "DefaultOn", 1, ini);
    g_cvarValue  = g_defaultOn;
}

void NameTag_Install()
{
    if (!g_enabled) return;

    if (!VerifySig(kNameTagSite, kNameTagSig, sizeof(kNameTagSig), "name tag"))
        return;
    if (!VerifySig(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr"))
        return;

    RegisterCVar();

    if (!WriteJump(kNameTagSite, (void*)&NameTagHook)) {
        Log("could not write the name tag hook - disabled");
        return;
    }
    Log("probe installed at 0x%08X (%d lines)", kNameTagSite, g_probeLines);
}
