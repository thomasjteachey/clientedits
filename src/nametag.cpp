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
#include <stdlib.h>
#include <string.h>

#include "nametag.h"
#include "unitbars.h"

static char g_dir[MAX_PATH] = { 0 };
static int  g_enabled = 1;          // [NameTag] Enabled
static int  g_probeLines = 0;       // [NameTag] ProbeLines, 0 = no probe logging
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

// CVar lookup by name, the one the Lua GetCVar uses: returns the CVar, or null
// when there is none (or it is not visible to scripts).
static const DWORD kCVarLookup   = 0x00767460;
static const BYTE  kCVarLookupSig[] = {
    0x55,                                   // push ebp
    0x8B, 0xEC,                             // mov ebp, esp
    0x8B, 0x45, 0x08,                       // mov eax, [ebp+8]
    0x85, 0xC0                              // test eax, eax
};
typedef void* (__cdecl* CVarLookup_t)(const char* name);
static int g_lookupOk = 0;

// WHERE the CVar is registered. The name tag alone is not enough: it only runs
// once some name is on screen, and with nobody's name showing (own name is off
// by default) the CVar never existed and the Interface checkbox stayed grey.
// So it is also registered when the world UI's Lua functions are - the loop at
// 0x00512109 walks the 310-entry table at 0x00AC80B0 (GetCVar among them), and
// ends with this call, which runs on every world load and every /reload,
// before FrameXML is read:
//     0x00512131  call 0x0055DC90        E8 5A BB 04 00
// It is redirected through a stub that registers the CVar and then tail-jumps
// to the original callee, so the return address is still 0x00512136.
static const DWORD kWorldLuaSite   = 0x00512131;
static const BYTE  kWorldLuaSig[]  = { 0xE8, 0x5A, 0xBB, 0x04, 0x00 };
static DWORD       g_worldLuaNext  = 0x0055DC90;

// STAGE 2 - the marker line. Inside 0x007E5640's dirty block:
//
//     0x007E5754  call edx              ; [unitVtable+0xCC](mask, buf, 0x400)
//     0x007E5756  test eax, eax         ; eax = number of lines written
//     0x007E5758  mov [ebp-0x10], eax
//     0x007E575B  fild dword [ebp-0x10] ; -> the tag's height
//
// The five bytes at 0x007E5756 are relocation-free. The hook prepends a line
// to the buffer (ebp-0x4EC) and returns the new count, then replays them. The
// jge at 0x007E575E reads the flags of that replayed `test`; fild leaves the
// flags alone.
static const DWORD kMarkerSite   = 0x007E5756;
static const BYTE  kMarkerSig[]  = { 0x85, 0xC0, 0x89, 0x45, 0xF0, 0xDB, 0x45, 0xF0 };
static const size_t kMarkerStolen = 5;
static const DWORD kMarkerResume = 0x007E575B;

// The unit's auras, read exactly the way the client's own name builder
// (0x0072D4F0) and its count accessor (0x004F8850) read them:
//   count = [unit+0xDD0]; if that is -1, count = [unit+0xC54] and the entries
//   live at [unit+0xC58], otherwise inline from unit+0xC50. 0x18 bytes each,
//   spell id at +0x08.
static const DWORD kAuraInlineCount = 0xDD0;
static const DWORD kAuraHeapCount   = 0xC54;
static const DWORD kAuraHeapPtr     = 0xC58;
static const DWORD kAuraInline      = 0xC50;
static const DWORD kAuraStride      = 0x18;
static const DWORD kAuraSpell       = 0x08;

enum Marker { MARK_NONE = 0, MARK_WORLD, MARK_TOURNAMENT, MARK_BOT };

static int  g_testMarker = MARK_NONE;   // [NameTag] TestMarker: every player gets it
// The auras the server hands out (custom_name_markers.cpp on the realm):
// 92010 World Character, 92011 Tournament Character, 92012 Playerbot.
// [NameTag] WorldAura / TournamentAura / BotAura override them.
static const DWORD kDefaultAura[4] = { 0, 92010, 92011, 92012 };
static DWORD g_auraFor[4] = { 0, 92010, 92011, 92012 };

// Which marker each tag's text was last BUILT with. A tag is only rebuilt when
// the client marks it dirty, and nothing the client does marks it dirty when an
// aura arrives - so a marker aura that lands after the name was first drawn, or
// a flip of the CVar, would never show. The pre-hook compares this with what
// the tag should carry now and sets the tag's dirty bits (text 1, colour 2 at
// tag+0x18, see 0x007E56C0 / 0x007E56F8) when they differ.
struct BuiltMark { void* tag; int mark; int tint; };
static BuiltMark g_built[1024];

static BuiltMark* BuiltSlot(void* tag, bool claim)
{
    unsigned h = ((DWORD)tag >> 3) & 1023;
    for (int i = 0; i < 4; ++i) {
        BuiltMark* b = &g_built[(h + i) & 1023];
        if (b->tag == tag)
            return b;
    }
    if (!claim)
        return NULL;
    // Not there: take the first slot of the run (stale tags are harmless -
    // at worst a tag is rebuilt once more than it needed to be).
    for (int i = 0; i < 4; ++i) {
        BuiltMark* b = &g_built[(h + i) & 1023];
        if (!b->tag) { b->tag = tag; return b; }
    }
    BuiltMark* b = &g_built[h];
    b->tag = tag;
    return b;
}
static int  g_markerHooked = 0;
static int  g_markerLogged = 0;
static DWORD g_markerResume = kMarkerResume;

// THE SQUAD TINT. In a multi-team arena (Baradin Hold: Four Corners) every
// player carries their squad's flag, and those four flags are handed out only
// there - the Gold and Green squads get their own copies of the stock pair - so
// an enemy's name leans towards its squad's colour without the two-sided
// arenas changing at all (your own squad keeps the normal friendly colour). It is a lean, not a recolour: hostile names stay reddish,
// friendly ones stay friendly, and none of them becomes the plain blue a
// friendly player's name already means. The World / Tournament / Bot word
// above the name is the one thing drawn in the squad's full colour there
// (MarkerText).
//
// The name colour is one ARGB dword that [unitVtable+0x78] writes to tag+0x0C,
// in the dirty block, before either use of it:
//
//     0x007E56D5  call edx              ; [vtable+0x78](&tag+0x0C)
//     0x007E56D7  mov eax, [esi+8]      ; the tag's font string
//     0x007E56DA  test eax, eax
//     0x007E56DC  je 0x007E56EE
//
// After it the colour either recolours the existing string (0x006BD070, when
// only the colour is dirty) or goes into a new one (0x006BE2B0 takes &tag+0x0C),
// so blending it in place right here covers both. The five bytes at 0x007E56D7
// are relocation-free; the je reads the flags of the replayed test.
static const DWORD kColourSite   = 0x007E56D7;
static const BYTE  kColourSig[]  = { 0x8B, 0x46, 0x08, 0x85, 0xC0, 0x74, 0x10 };
static const size_t kColourStolen = 5;
static DWORD g_colourResume = 0x007E56DC;
static int   g_colourHooked = 0;
static int   g_colourLogged = 0;

struct SquadTint { DWORD aura; DWORD rgb; };
static const SquadTint kDefaultSquadTint[4] = {
    { 92322, 0xFFC800 },    // Gold Team
    { 92323, 0x33E14D },    // Green Team
    { 92320, 0x2EA8FF },    // Blue Team
    { 92321, 0xB84DFF },    // Purple Team
};
static SquadTint g_squadTint[4];
// How far towards the squad colour, in percent: 40 read as too subtle in game.
static const int kDefaultTintPercent = 65;
static int g_tintPercent = kDefaultTintPercent;   // [NameTag] SquadTint, 0 = off

// THE LOBBY TINT. In a custom game lobby every member wears one of three auras
// (custom_game_lobby.cpp on the realm): the Blue team's flag, the Red team's,
// or - on neither team, so spectating once the match starts - a hidden marker.
// Those names are drawn in that colour outright, not leaned: blue, red and
// grey, the same for everyone looking, yourself included. The flags are worn
// nowhere but the lobby, so nothing outside it changes. Tint keys 5..7.
static const SquadTint kDefaultLobbyTint[3] = {
    { 32609, 0x3399FF },    // Blue team (Alliance Flag Visual)
    { 32610, 0xFF3333 },    // Red team (Horde Flag Visual)
    { 92324, 0x9D9D9D },    // spectating (Lobby Spectator)
};
static SquadTint g_lobbyTint[3];
static int g_lobbyTintOn = 1;                     // [NameTag] LobbyTint, 0 = off
static const int kLobbyKey = 5;                   // the first lobby tint key

// ALLIES IN AN ARENA are drawn green outright - the client's own friendly
// reaction green (00FF00, the table at 0x00BD0CA4) - so a team-mate never
// reads as the Blue squad's colour, nor as the plain blue the client gives a
// friendly player. An ally is who the client itself counts as one: the
// player's arena-faction byte (PLAYER_BYTES_3 byte 3, descriptor block
// [unit+0x1008] +0x1F) equal to yours from the battlefield status
// [0x00BEA54A], while the battlefield map is an arena ([0x00BEA570] == 4) -
// the same comparison that keeps them out of arena1-5 (docs/ARENA_UNITS_RE.md).
// In a multi-team arena the server sends every viewer their own squad on
// their side and everyone else on the other, so it means your squad there.
static const DWORD kArenaMapType    = 0x00BEA570;
static const DWORD kMyArenaFaction  = 0x00BEA54A;
static const DWORD kPlayerDesc      = 0x1008;
static const DWORD kArenaFactionOff = 0x1F;
static int   g_allyGreenOn = 1;                   // [NameTag] ArenaAllyGreen, 0 = off
static DWORD g_allyColour  = 0x00FF00;            // [NameTag] ArenaAllyColour, RRGGBB
static int   g_arenaGlobalsOk = 0;
static const int kAllyKey = 8;                    // the ally tint key, after the lobby's

// The tint marks the OTHER squads: your own keeps the friendly green every
// ally's name has elsewhere. Who you are comes from the active player's guid,
// cdecl () -> u64 in edx:eax, the call playercollide.cpp makes every frame.
static const DWORD kGetActiveGuid  = 0x004D3790;
static const BYTE  kGetActiveSig[] = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };
typedef unsigned __int64 (__cdecl* GetActiveGuid_t)();
static int g_activeOk = 0;

// THE COLOUR. The tag builds its font string with flags 0xC8, pushed as an
// immediate at 0x007E57A6 (`push 0xC8`, 68 C8 00 00 00). Bit 0x08 makes the
// layout drop the colour a |c code sets (0x006C715B: `test [str+0x5C], 8`
// skips the store) and take the one colour at str+0x2C for every glyph - the
// shortcut that lets a name change colour through 0x006C6C30 without being
// laid out again. So a tag carrying a marker is built with 0xC0 instead, and
// the immediate goes back to 0xC8 for every other tag. The byte is only read
// by this same thread, a few instructions after the hook, so rewriting it per
// tag is safe.
static const DWORD kFlagsImm    = 0x007E57A7;
static const BYTE  kFlagsSig[]  = { 0x68, 0xC8, 0x00, 0x00, 0x00 };
static const BYTE  kFlagsNormal = 0xC8;
static const BYTE  kFlagsCoded  = 0xC0;
static int         g_flagsWritable = 0;

static void SetTagFlags(BYTE flags)
{
    if (g_flagsWritable && *(volatile BYTE*)kFlagsImm != flags)
        *(volatile BYTE*)kFlagsImm = flags;
}

// The CVar callback cannot be a pointer into this DLL. The client checks every
// function pointer it is about to call against Wow.exe's own .text and treats
// anything else as fatal error #134 "Invalid function pointer" - the same check
// gluebridge.cpp meets for Lua (0x0086B5A0). The first build registered
// CVarChanged directly; the game called it on registration and died on the
// spot with its address in the message.
//
// So the callback that gets registered is a 5-byte jmp written into int3
// padding inside .text, which then jumps on to the DLL. The padding used is the
// run right after the client's OWN UnitName* callback (0x007E60E0 ... ret at
// 0x007E6140), fifteen bytes of CC that nothing else in this DLL claims.
static const DWORD kCVarThunkSite  = 0x007E6141;
static const BYTE  kCVarThunkSig[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
static int         g_thunkReady = 0;

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

// Returns true like the client's own callback (0x007E613D: mov al, 1 / ret),
// which reads as "accept the new value" - a void callback would leave whatever
// happened to be in al, and the CVar could randomly refuse to change.
static bool __cdecl CVarChanged(void* cvar, void* a, const char* newValue, int userArg)
{
    (void)cvar; (void)a; (void)userArg;
    if (newValue && *newValue)
        g_cvarValue = (newValue[0] != '0');
    g_cvarSpoke = 1;
    Log("centurionNameTags -> %d", g_cvarValue);

    // Remember the choice. The CVar is registered after Config.wtf has been
    // read, so the ini is what carries it to the next session: its DefaultOn
    // becomes the CVar's default at the next registration.
    if (g_cvarValue != g_defaultOn) {
        char ini[MAX_PATH];
        _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
        WritePrivateProfileStringA("NameTag", "DefaultOn", g_cvarValue ? "1" : "0", ini);
        g_defaultOn = g_cvarValue;
    }
    return true;
}

typedef void* (__cdecl* CVarRegister_t)(const char* name, int unknown, int flags,
                                        const char* defaultValue, void* callback,
                                        int category, int unk0, int userArg, int unk1);

static int g_cvarRegistered = 0;
static DWORD g_cvarChecked = 0;

static void RegisterCVar();

// Make sure the CVar exists NOW, not just that it was registered once.
//
// It does not survive everything: registered from the character select screen,
// it was gone by the time the world loaded - its memory had been reused for
// `synchronizeConfig` - while this file still believed it was there. The marker
// kept drawing off the remembered value, but /console and the Interface
// checkbox found nothing (the checkbox greyed itself out on every client). So
// this runs from the in-world name tag, looks the CVar up by name at most once
// a second, and registers it again whenever it is missing.
static void EnsureCVar()
{
    DWORD const now = GetTickCount();
    if (g_cvarRegistered && now - g_cvarChecked < 1000)
        return;
    g_cvarChecked = now;

    if (g_cvarRegistered && g_lookupOk && ((CVarLookup_t)kCVarLookup)("centurionNameTags"))
        return;
    if (g_cvarRegistered)
        Log("centurionNameTags went missing - registering it again");
    g_cvarRegistered = 0;
    RegisterCVar();
}

static void RegisterCVar()
{
    if (g_cvarRegistered)
        return;
    g_cvarRegistered = 1;

    if (!VerifySig(kCVarRegister, kCVarRegSig, sizeof(kCVarRegSig), "CVar::Register"))
        return;

    // Without the in-.text thunk there is no callback the client will accept,
    // and registering with a DLL pointer is a guaranteed fatal error.
    if (!g_thunkReady) {
        Log("no callback thunk - centurionNameTags not registered");
        return;
    }

    // Every argument exactly as the client passes its own UnitName* CVars
    // (0x007E6150), confirmed against the file rather than guessed: arg4 is the
    // default value - 0x009E14A0 is "0" and 0x009E1464 is "1", the real 3.3.5
    // defaults for Own/NPC and Guild/PVPTitle. arg2's meaning is unknown and the
    // client always passes 0 there, so this does too.
    CVarRegister_t reg = (CVarRegister_t)kCVarRegister;
    void* cvar = reg("centurionNameTags",
                     0,
                     0x10,
                     g_defaultOn ? "1" : "0",
                     (void*)kCVarThunkSite,     // -> CVarChanged, see kCVarThunkSite
                     4, 0, 1, 0);
    Log("registered centurionNameTags (cvar=%p, default %d)", cvar, g_defaultOn);
}

// The world UI's Lua is being built: (re)register the CVar now, so it is
// there before FrameXML - and the options checkbox - looks for it.
extern "C" void __cdecl NameTagWorldLuaReady()
{
    g_cvarRegistered = 0;
    RegisterCVar();
    UnitBars_WorldLuaReady();      // its Lua functions ride on this hook (unitbars.h)
    if (g_lookupOk)
        Log("world Lua ready: centurionNameTags %s", ((CVarLookup_t)kCVarLookup)("centurionNameTags") ? "present" : "MISSING");
}

__declspec(naked) static void WorldLuaStub()
{
    __asm {
        pushad
        pushfd
        call NameTagWorldLuaReady
        popfd
        popad
        jmp  [g_worldLuaNext]
    }
}

bool NameTag_Enabled()
{
    if (!g_enabled) return false;
    return g_cvarSpoke ? (g_cvarValue != 0) : (g_defaultOn != 0);
}

// ----------------------------------------------------------------- the probe
//
// Called with the tag record before the original body runs. Read-only, apart
// from registering the CVar the first time through.
//
// The CVar is registered HERE and not in NameTag_Install, and that is not a
// style choice. Install runs from DllMain, during process start and under the
// loader lock, before Wow.exe has initialised anything - its CVar manager and
// its allocator included. Every other module only patches bytes there; calling
// into the game from there failed the whole process with 0xC0000142
// (STATUS_DLL_INIT_FAILED) before a window ever opened. By the time a name tag
// is drawn the game is fully up and this is its own main thread.
// Defined with the marker and the tint, further down.
static int WantedMarker(void* tag, void* unit);
static int WantedTint(void* tag, void* unit);

extern "C" void __cdecl NameTagProbe(void* tag)
{
    EnsureCVar();

    if (!Readable(tag, 0x20)) return;

    if (g_markerHooked || g_colourHooked) {
        DWORD lo = *(DWORD*)((BYTE*)tag + 0x10);
        DWORD hi = *(DWORD*)((BYTE*)tag + 0x14);
        if ((lo || hi) && !(hi & 0xFFFF0000)) {
            void* u = ((ObjectPtr_t)kObjectPtr)(lo, hi, 1, "nametag.cpp", 0);
            BuiltMark* b = BuiltSlot(tag, false);
            // Both bits for a tint too, not just colour: a tag built with
            // colour codes on (a marker) is not one the client recolours
            // without laying it out again.
            if ((g_markerHooked && (b ? b->mark : MARK_NONE) != WantedMarker(tag, u)) ||
                (g_colourHooked && (b ? b->tint : 0) != WantedTint(tag, u)))
                *(DWORD*)((BYTE*)tag + 0x18) |= 3;
        }
    }

    if (g_probed >= g_probeLines) return;

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

    // One line per unit: the first run spent all twelve on the same player.
    static DWORD seen[64];
    for (int i = 0; i < g_probed && i < 64; ++i)
        if (seen[i] == guidLo) return;
    if (g_probed < 64) seen[g_probed] = guidLo;

    // The first few aura spell ids, read the way the marker reads them, so the
    // log shows whether that reader lines up with the buffs actually on the unit.
    char auras[160] = { 0 };
    if (unit) {
        BYTE* u = (BYTE*)unit;
        if (Readable(u + kAuraInline, kAuraInlineCount - kAuraInline + 4)) {
            DWORD count = *(DWORD*)(u + kAuraInlineCount);
            BYTE* entries = u + kAuraInline;
            if (count == 0xFFFFFFFF) {
                count = *(DWORD*)(u + kAuraHeapCount);
                entries = *(BYTE**)(u + kAuraHeapPtr);
            }
            size_t at = _snprintf_s(auras, sizeof(auras), _TRUNCATE, "%u:", count);
            for (DWORD i = 0; i < count && i < 12 && entries && Readable(entries + i * kAuraStride, kAuraStride); ++i) {
                int n = _snprintf_s(auras + at, sizeof(auras) - at, _TRUNCATE, " %u",
                                    *(DWORD*)(entries + i * kAuraStride + kAuraSpell));
                if (n < 0) break;
                at += n;
            }
        }
    }

    ++g_probed;
    Log("tag=%p guid=%08X%08X flags=0x%X unit=%p vtable=%p auras=[%s] mask=0x%X",
        tag, guidHi, guidLo, flags, unit, vtable, auras, *(DWORD*)kNameFlags);
    (void)nameFn;

    if (g_probed == g_probeLines)
        Log("probe budget spent");
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

// ---------------------------------------------------------------- the marker

// Whether the unit carries the aura spellId.
static bool AnyAura(void* unit, DWORD spellId)
{
    BYTE* u = (BYTE*)unit;
    if (!spellId || !Readable(u + kAuraInline, kAuraInlineCount - kAuraInline + 4))
        return false;

    DWORD count = *(DWORD*)(u + kAuraInlineCount);
    BYTE* entries = u + kAuraInline;
    if (count == 0xFFFFFFFF) {
        count = *(DWORD*)(u + kAuraHeapCount);
        entries = *(BYTE**)(u + kAuraHeapPtr);
    }
    if (!entries || count > 255 || !Readable(entries, count * kAuraStride))
        return false;

    for (DWORD i = 0; i < count; ++i)
        if (*(DWORD*)(entries + i * kAuraStride + kAuraSpell) == spellId)
            return true;
    return false;
}

static int MarkerFor(void* tag, void* unit)
{
    // Players only: a player guid's high word is zero (HighGuid::Player).
    DWORD guidHi = *(DWORD*)((BYTE*)tag + 0x14);
    if (guidHi & 0xFFFF0000)
        return MARK_NONE;

    for (int m = MARK_WORLD; m <= MARK_BOT; ++m)
        if (AnyAura(unit, g_auraFor[m]))
            return m;
    return g_testMarker;
}

static int SquadOf(void* unit);

// A marker key: the marker in the low four bits and, in a multi-team arena,
// the squad (1..4) above them - the word is drawn in the squad's own colour
// there, so a squad flag arriving has to rebuild the tag just as a new marker
// does, and keeping both in the one key the tag was built with does that.
static int MarkerOf(int key) { return key & 0xF; }
static int SquadOfKey(int key) { return key >> 4; }

// |c codes: parsed by the font's tokenizer (0x006BD64F), and honoured once the
// tag's string is built without flag 0x08 - see kFlagsImm.
static const char* const kMarkerWord[]   = { NULL, "World", "Tournament", "Bot" };
static const DWORD       kMarkerColour[] = { 0, 0x73BFFF, 0xFF9933, 0xB266FF };

// Writes the marker line for `key` into out; returns its length, 0 for none.
static int MarkerText(int key, char* out, size_t size)
{
    int const mark = MarkerOf(key);
    int const squad = SquadOfKey(key);
    if (mark < MARK_WORLD || mark > MARK_BOT)
        return 0;
    DWORD const rgb = squad >= 1 && squad <= 4 ? g_squadTint[squad - 1].rgb : kMarkerColour[mark];
    int const n = _snprintf_s(out, size, _TRUNCATE, "|cff%06x%s|r\n", rgb & 0xFFFFFF, kMarkerWord[mark]);
    return n > 0 ? n : 0;
}

static int WantedMarker(void* tag, void* unit)
{
    if (!NameTag_Enabled() || !unit || !Readable(tag, 0x20))
        return MARK_NONE;
    int const mark = MarkerFor(tag, unit);
    return mark != MARK_NONE ? mark | (SquadOf(unit) << 4) : MARK_NONE;
}

extern "C" int __cdecl NameTagMarker(void* tag, void* unit, char* buf, int lines)
{
    SetTagFlags(kFlagsNormal);
    int const mark = lines > 0 && buf ? WantedMarker(tag, unit) : MARK_NONE;
    if (BuiltMark* b = BuiltSlot(tag, true))
        b->mark = mark;

    char text[64];
    if (!MarkerText(mark, text, sizeof(text)))
        return lines;

    size_t have = strnlen(buf, 0x400);
    size_t add  = strlen(text);
    if (have + add >= 0x400)
        return lines;

    memmove(buf + add, buf, have + 1);
    memcpy(buf, text, add);
    SetTagFlags(kFlagsCoded);

    if (g_markerLogged < 4) {
        ++g_markerLogged;
        Log("marker: lines %d -> %d, text \"%s\"", lines, lines + 1, buf);
    }
    return lines + 1;
}

// ------------------------------------------------------------------ the tint

// 1..4 = the squad whose flag the unit carries, 0 = none. One pass over the
// auras for all four flags.
static int SquadOf(void* unit)
{
    BYTE* u = (BYTE*)unit;
    if (!Readable(u + kAuraInline, kAuraInlineCount - kAuraInline + 4))
        return 0;

    DWORD count = *(DWORD*)(u + kAuraInlineCount);
    BYTE* entries = u + kAuraInline;
    if (count == 0xFFFFFFFF) {
        count = *(DWORD*)(u + kAuraHeapCount);
        entries = *(BYTE**)(u + kAuraHeapPtr);
    }
    if (!entries || count > 255 || !Readable(entries, count * kAuraStride))
        return 0;

    for (DWORD i = 0; i < count; ++i) {
        DWORD const spell = *(DWORD*)(entries + i * kAuraStride + kAuraSpell);
        for (int s = 0; s < 4; ++s)
            if (spell && spell == g_squadTint[s].aura)
                return s + 1;
    }
    return 0;
}

// The squad the player at the keyboard carries the flag of, 0 if none.
static int MySquad()
{
    if (!g_activeOk)
        return 0;
    unsigned __int64 const guid = ((GetActiveGuid_t)kGetActiveGuid)();
    if (!guid)
        return 0;
    void* me = ((ObjectPtr_t)kObjectPtr)((DWORD)guid, (DWORD)(guid >> 32), 1, "nametag.cpp", 0);
    return me ? SquadOf(me) : 0;
}

// 1..3 = the lobby aura the unit wears (Blue, Red, spectating), 0 = none.
static int LobbyOf(void* unit)
{
    for (int l = 0; l < 3; ++l)
        if (AnyAura(unit, g_lobbyTint[l].aura))
            return l + 1;
    return 0;
}

// Whether `unit` (a player) is on your side of the arena you are in.
static bool IsArenaAlly(void* unit)
{
    if (!g_arenaGlobalsOk || *(volatile DWORD*)kArenaMapType != 4)
        return false;
    BYTE* u = (BYTE*)unit;
    if (!Readable(u + kPlayerDesc, 4))
        return false;
    BYTE* desc = *(BYTE**)(u + kPlayerDesc);
    if (!desc || !Readable(desc + kArenaFactionOff, 1))
        return false;
    return desc[kArenaFactionOff] == *(volatile BYTE*)kMyArenaFaction;
}

static int WantedTint(void* tag, void* unit)
{
    if (!unit || !Readable(tag, 0x20))
        return 0;
    // Players only, as for the marker.
    if (*(DWORD*)((BYTE*)tag + 0x14) & 0xFFFF0000)
        return 0;
    // In an arena an ally is green before anything else is considered.
    if (g_allyGreenOn && IsArenaAlly(unit))
        return kAllyKey;
    if (g_tintPercent > 0) {
        // Allies are not tinted. Until your own flag lands MySquad() is 0 and
        // they are; the pre-hook rebuilds them once it does, as for any tint
        // change.
        int const squad = SquadOf(unit);
        if (squad)
            return squad != MySquad() ? squad : 0;
    }
    if (g_lobbyTintOn) {
        int const lobby = LobbyOf(unit);
        if (lobby)
            return kLobbyKey + lobby - 1;
    }
    return 0;
}

// Each of R, G and B moved g_tintPercent of the way to the squad's; alpha kept.
static DWORD Tinted(DWORD argb, DWORD rgb)
{
    DWORD out = argb & 0xFF000000;
    for (int shift = 0; shift <= 16; shift += 8) {
        int const from = (argb >> shift) & 0xFF;
        int const to   = (rgb >> shift) & 0xFF;
        out |= (DWORD)(from + (to - from) * g_tintPercent / 100) << shift;
    }
    return out;
}

extern "C" void __cdecl NameTagColour(void* tag, void* unit)
{
    if (!Readable(tag, 0x20))
        return;
    int const tint = WantedTint(tag, unit);
    if (BuiltMark* b = BuiltSlot(tag, true))
        b->tint = tint;
    if (!tint)
        return;

    DWORD* colour = (DWORD*)((BYTE*)tag + 0x0C);
    DWORD const was = *colour;
    if (tint == kAllyKey)
        *colour = (was & 0xFF000000) | (g_allyColour & 0xFFFFFF);
    else if (tint >= kLobbyKey)
        *colour = (was & 0xFF000000) | (g_lobbyTint[tint - kLobbyKey].rgb & 0xFFFFFF);
    else
        *colour = Tinted(was, g_squadTint[tint - 1].rgb);
    if (g_colourLogged < 4) {
        ++g_colourLogged;
        Log("tint: %s %d, colour %08X -> %08X",
            tint == kAllyKey ? "arena ally" : tint >= kLobbyKey ? "lobby" : "squad",
            tint == kAllyKey ? 0 : tint >= kLobbyKey ? tint - kLobbyKey + 1 : tint, was, *colour);
    }
}

__declspec(naked) static void ColourHook()
{
    __asm {
        pushad
        pushfd
        push ebx                    // the unit
        push esi                    // the tag
        call NameTagColour
        add  esp, 8
        popfd
        popad

        mov  eax, [esi + 8]         // the stolen bytes, replayed
        test eax, eax
        jmp  [g_colourResume]
    }
}

__declspec(naked) static void MarkerHook()
{
    __asm {
        pushad
        push eax                    // lines written
        lea  edx, [ebp - 0x4EC]
        push edx                    // the text buffer
        push ebx                    // the unit
        push esi                    // the tag
        call NameTagMarker
        add  esp, 16
        mov  [esp + 0x1C], eax      // pushad's eax slot
        popad

        test eax, eax               // the stolen bytes, replayed
        mov  [ebp - 0x10], eax
        jmp  [g_markerResume]
    }
}

// ------------------------------------------------------------------ install
// A jmp at `at`; bytes after it up to `span` become nops, so a debugger reading
// a hooked site does not show half an instruction. span = 5 writes the jmp alone.
static bool WriteJump(DWORD at, void* to, size_t span)
{
    DWORD old = 0;
    if (!VirtualProtect((void*)at, span, PAGE_EXECUTE_READWRITE, &old)) return false;
    BYTE* p = (BYTE*)at;
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)to - (at + 5);
    for (size_t i = 5; i < span; ++i) p[i] = 0x90;
    DWORD ignored = 0;
    VirtualProtect((void*)at, span, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, span);
    return true;
}

void NameTag_LoadSettings(const char* dir)
{
    if (dir) {
        strncpy_s(g_dir, sizeof(g_dir), dir, _TRUNCATE);
    }

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);

    // ON by default: players get no ini (package.ps1 ships only the DLL), and the
    // marker is theirs to switch off in Interface > AddOns > Centurion.
    g_enabled    = GetPrivateProfileIntA("NameTag", "Enabled", 1, ini);
    g_probeLines = GetPrivateProfileIntA("NameTag", "ProbeLines", 0, ini);
    g_defaultOn  = GetPrivateProfileIntA("NameTag", "DefaultOn", 1, ini);
    g_cvarValue  = g_defaultOn;

    char mark[32] = { 0 };
    GetPrivateProfileStringA("NameTag", "TestMarker", "", mark, sizeof(mark), ini);
    g_testMarker = !_stricmp(mark, "World")      ? MARK_WORLD
                 : !_stricmp(mark, "Tournament") ? MARK_TOURNAMENT
                 : !_stricmp(mark, "Bot")        ? MARK_BOT
                 : MARK_NONE;

    g_auraFor[MARK_WORLD]      = GetPrivateProfileIntA("NameTag", "WorldAura", kDefaultAura[MARK_WORLD], ini);
    g_auraFor[MARK_TOURNAMENT] = GetPrivateProfileIntA("NameTag", "TournamentAura", kDefaultAura[MARK_TOURNAMENT], ini);
    g_auraFor[MARK_BOT]        = GetPrivateProfileIntA("NameTag", "BotAura", kDefaultAura[MARK_BOT], ini);

    // The squad tint. SquadAuras and SquadColours are four comma-separated
    // values each, in squad order (Gold, Green, Blue, Purple); the colours are
    // RRGGBB hex. Anything left out keeps its default.
    g_tintPercent = GetPrivateProfileIntA("NameTag", "SquadTint", kDefaultTintPercent, ini);
    if (g_tintPercent < 0)   g_tintPercent = 0;
    if (g_tintPercent > 100) g_tintPercent = 100;
    memcpy(g_squadTint, kDefaultSquadTint, sizeof(g_squadTint));

    char list[160] = { 0 };
    GetPrivateProfileStringA("NameTag", "SquadAuras", "", list, sizeof(list), ini);
    char* p = list;
    for (int s = 0; s < 4 && *p; ++s) {
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end != p) g_squadTint[s].aura = (DWORD)v;
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }
    GetPrivateProfileStringA("NameTag", "SquadColours", "", list, sizeof(list), ini);
    p = list;
    for (int s = 0; s < 4 && *p; ++s) {
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 16);
        if (end != p) g_squadTint[s].rgb = (DWORD)v & 0xFFFFFF;
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }

    // The lobby tint: LobbyAuras / LobbyColours are three values each, in the
    // order Blue, Red, spectating, read the same way.
    g_lobbyTintOn = GetPrivateProfileIntA("NameTag", "LobbyTint", 1, ini);
    g_allyGreenOn = GetPrivateProfileIntA("NameTag", "ArenaAllyGreen", 1, ini);
    {
        char colourText[16] = { 0 };
        GetPrivateProfileStringA("NameTag", "ArenaAllyColour", "", colourText, sizeof(colourText), ini);
        char* end = NULL;
        unsigned long v = strtoul(colourText, &end, 16);
        if (end != colourText)
            g_allyColour = (DWORD)v & 0xFFFFFF;
    }
    memcpy(g_lobbyTint, kDefaultLobbyTint, sizeof(g_lobbyTint));
    GetPrivateProfileStringA("NameTag", "LobbyAuras", "", list, sizeof(list), ini);
    p = list;
    for (int l = 0; l < 3 && *p; ++l) {
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end != p) g_lobbyTint[l].aura = (DWORD)v;
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }
    GetPrivateProfileStringA("NameTag", "LobbyColours", "", list, sizeof(list), ini);
    p = list;
    for (int l = 0; l < 3 && *p; ++l) {
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 16);
        if (end != p) g_lobbyTint[l].rgb = (DWORD)v & 0xFFFFFF;
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }
}

void NameTag_Install()
{
    if (!g_enabled) return;

    if (!VerifySig(kNameTagSite, kNameTagSig, sizeof(kNameTagSig), "name tag"))
        return;
    if (!VerifySig(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr"))
        return;
    g_lookupOk = VerifySig(kCVarLookup, kCVarLookupSig, sizeof(kCVarLookupSig), "CVar lookup");

    // Nothing here may call into the game - see NameTagProbe. Only bytes are
    // checked and written; the CVar is registered on the first name tag.

    // The CVar's callback, in .text where the client will accept it. Without it
    // the probe still runs; only the toggle is missing.
    if (VerifySig(kCVarThunkSite, kCVarThunkSig, sizeof(kCVarThunkSig), "CVar callback padding") &&
        WriteJump(kCVarThunkSite, (void*)&CVarChanged, sizeof(kCVarThunkSig)))
        g_thunkReady = 1;

    // The world-UI registration point. A call, not a jmp: the stub returns to
    // 0x00512136 through the original callee.
    if (g_thunkReady && VerifySig(kWorldLuaSite, kWorldLuaSig, sizeof(kWorldLuaSig), "world Lua registration")) {
        DWORD old = 0;
        if (VirtualProtect((void*)kWorldLuaSite, 5, PAGE_EXECUTE_READWRITE, &old)) {
            BYTE* p = (BYTE*)kWorldLuaSite;
            *(DWORD*)(p + 1) = (DWORD)&WorldLuaStub - (kWorldLuaSite + 5);
            DWORD ignored = 0;
            VirtualProtect((void*)kWorldLuaSite, 5, old, &ignored);
            FlushInstructionCache(GetCurrentProcess(), (void*)kWorldLuaSite, 5);
            Log("CVar registration hooked at 0x%08X", kWorldLuaSite);
        }
    }

    if (!WriteJump(kNameTagSite, (void*)&NameTagHook, kStolen)) {
        Log("could not write the name tag hook - disabled");
        return;
    }
    Log("probe installed at 0x%08X (%d lines)", kNameTagSite, g_probeLines);

    // The marker is its own hook: without it the probe and the toggle still
    // work, the tag is just drawn the way the client always drew it.
    if (VerifySig(kMarkerSite, kMarkerSig, sizeof(kMarkerSig), "name tag line count") &&
        WriteJump(kMarkerSite, (void*)&MarkerHook, kMarkerStolen)) {
        g_markerHooked = 1;

        // Left writable for good: SetTagFlags rewrites it per tag.
        DWORD old = 0;
        if (VerifySig(kFlagsImm - 1, kFlagsSig, sizeof(kFlagsSig), "name tag font flags") &&
            VirtualProtect((void*)(kFlagsImm - 1), sizeof(kFlagsSig), PAGE_EXECUTE_READWRITE, &old))
            g_flagsWritable = 1;
        Log("marker installed at 0x%08X (test marker %d, auras %u/%u/%u, colour codes %s)", kMarkerSite,
            g_testMarker, g_auraFor[MARK_WORLD], g_auraFor[MARK_TOURNAMENT], g_auraFor[MARK_BOT],
            g_flagsWritable ? "on" : "OFF");
    }

    // The squad tint is its own hook too, and leans on the probe above to
    // re-dirty a tag whose squad flag arrived after it was built.
    g_activeOk = VerifySig(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActivePlayerGuid");
    g_arenaGlobalsOk = Readable((void*)kArenaMapType, 4) && Readable((void*)kMyArenaFaction, 1);
    if ((g_tintPercent > 0 || g_lobbyTintOn || g_allyGreenOn) &&
        VerifySig(kColourSite, kColourSig, sizeof(kColourSig), "name tag colour") &&
        WriteJump(kColourSite, (void*)&ColourHook, kColourStolen)) {
        g_colourHooked = 1;
        Log("squad tint installed at 0x%08X (%d%%; %u=%06X %u=%06X %u=%06X %u=%06X)", kColourSite, g_tintPercent,
            g_squadTint[0].aura, g_squadTint[0].rgb, g_squadTint[1].aura, g_squadTint[1].rgb,
            g_squadTint[2].aura, g_squadTint[2].rgb, g_squadTint[3].aura, g_squadTint[3].rgb);
        Log("arena allies %s (%06X, globals %s)", g_allyGreenOn ? "green" : "untouched",
            g_allyColour, g_arenaGlobalsOk ? "ok" : "UNREADABLE");
        Log("lobby tint %s (%u=%06X %u=%06X %u=%06X)", g_lobbyTintOn ? "on" : "off",
            g_lobbyTint[0].aura, g_lobbyTint[0].rgb, g_lobbyTint[1].aura, g_lobbyTint[1].rgb,
            g_lobbyTint[2].aura, g_lobbyTint[2].rgb);
    }
}
