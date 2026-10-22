// unitbars.cpp - CenturionUnitBarsScan() / CenturionUnitBar(i). See unitbars.h.
//
// Reversing (Wow.exe 3.3.5a 12340; addresses are VAs):
//
//   0x004F6D20  CGWorldFrame::WorldToScreen, thiscall(worldFrame,
//                 C3Vector const* world, C3Vector* out, uint32* flags) -> bool
//               Subtracts the camera ([wf+0x7E20]) position, transforms by the
//               view-projection at wf+0x340, and returns 0 when the point is
//               nearer than the near clip ([camera+0x38]) - behind the camera.
//               Otherwise out.x/out.y are the point in DDC (NDC scaled by the
//               viewport and by 0x0047BFF0), relative to the frame's rect, and
//               out.z the depth. Callers: the chat bubble / combat text helper
//               0x00715720, 0x007E71FE, 0x004F9C35 ...
//   [wf+0x64 .. +0x70]  the frame's rect: bottom, left, top, right (DDC). The
//               function bounds-checks out.x against right-left and out.y
//               against top-bottom, so those are the extents used here.
//   [0x00B7436C] the CGWorldFrame.
//   CGUnit_C vtable 0x00A34D90, slot 8 (0x0071FEF0): the name position,
//               thiscall(unit, C3Vector* out) -> out. The model's attachment 18
//               (29 when mounted) when the model has one, else the unit's
//               position plus its scaled height. It is where the client puts
//               the name, so the bar sits on top of the head for any model.
//   Descriptors at [unit+0x08]: UNIT_FIELD_HEALTH +0x60, MAXHEALTH +0x80,
//               UNIT_FIELD_FLAGS +0xEC (0x02000000 = not selectable).
//   [obj+0x14]  TYPEID (3 = unit, 4 = player).
//
// Lua will not call a C function outside Wow.exe's .text (gluebridge.cpp), so
// each of the two functions is registered as a 5-byte jmp in int3 padding:
//   0x0086B5FA  the rest of the run gluebridge's thunk (0x0086B5F5) starts
//   0x007E6146  the rest of the run nametag's CVar thunk (0x007E6141) starts

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "unitbars.h"

static char  g_dir[MAX_PATH] = { 0 };
static int   g_enabled = 1;          // [UnitBars] Enabled
static float g_range = 80.0f;        // [UnitBars] Range, yards from the player
static int   g_ready = 0;            // Install verified and wrote the thunks
static int   g_faults = 0;
static int   g_targetReady = 0;   // CenturionTargetGUID's thunk is written

static const DWORD kWorldToScreen = 0x004F6D20;
static const BYTE  kWorldToScreenSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x8B, 0x45, 0x08, 0xD9, 0x00 };
static const DWORD kEnumVisible = 0x004D4B30;
static const BYTE  kEnumSig[] = { 0x55, 0x8B, 0xEC, 0xA1, 0xBC, 0x39, 0xD4, 0x00 };
static const DWORD kObjectPtr = 0x004D4DB0;
static const BYTE  kObjectPtrSig[] = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00 };
static const DWORD kGetActiveGuid = 0x004D3790;
static const BYTE  kGetActiveSig[] = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };
static const DWORD kGetReaction = 0x007251C0;
static const BYTE  kGetReactionSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x57 };
static const DWORD kNamePosition = 0x0071FEF0;     // CGUnit_C vtable slot 8
static const BYTE  kNamePositionSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56, 0x57, 0x8B, 0xF1 };
static const DWORD kUnitVtable = 0x00A34D90;
#define kWorldFramePtr (*(BYTE**)0x00B7436C)

static const DWORD kScanThunk = 0x0086B5FA;
static const DWORD kBarThunk  = 0x007E6146;
static const DWORD kTargetThunk = 0x007E614B;       // the last five of nametag's run
// CGGameUI::Target(guid), cdecl (lo, hi): what TargetUnit (0x00525A30, through
// 0x005259E0) ends in once it has a GUID for its unit token.
static const DWORD kSetTarget = 0x00524BF0;
static const BYTE  kSetTargetSig[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xAC, 0x02, 0x00, 0x00 };
static const DWORD kTaint = 0x00D4139C;            // FrameScript's current execution taint
static const BYTE  kPadSig[]  = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };

static const DWORD kOff_Type   = 0x14;
static const DWORD kOff_Desc   = 0x08;
static const DWORD kOff_PosX   = 0x798;
static const DWORD kDesc_Health    = 0x60;
static const DWORD kDesc_MaxHealth = 0x80;
static const DWORD kDesc_Flags     = 0xEC;
static const DWORD kDesc_NpcFlags  = 0x148;        // UNIT_NPC_FLAGS (0x52): a vendor, trainer, ...
static const DWORD kFlagNotSelectable = 0x02000000;
static const DWORD kTypeMaskUnit = 0x08;
static const DWORD kTypeIdUnit   = 3;
static const DWORD kTypeIdPlayer = 4;

typedef void        (__cdecl* RegisterFunction_t)(const char*, void*);
typedef void        (__cdecl* LuaPushNumber_t)(void*, double);
typedef double      (__cdecl* LuaToNumber_t)(void*, int);
typedef void        (__cdecl* LuaPushString_t)(void*, const char*);
typedef const char* (__cdecl* LuaToLString_t)(void*, int, size_t*);
typedef void        (__cdecl* SetTarget_t)(DWORD guidLo, DWORD guidHi);
static const RegisterFunction_t pRegisterFunction = (RegisterFunction_t)0x00817F90;
static const LuaPushNumber_t    pLuaPushNumber    = (LuaPushNumber_t)   0x0084E2A0;
static const LuaToNumber_t      pLuaToNumber      = (LuaToNumber_t)     0x0084E030;
static const LuaPushString_t    pLuaPushString    = (LuaPushString_t)   0x0084E350;
static const LuaToLString_t     pLuaToLString     = (LuaToLString_t)    0x0084E0E0;

struct C3Vector { float x, y, z; };
typedef bool  (__thiscall* WorldToScreen_t)(void* worldFrame, C3Vector const* world, C3Vector* out, DWORD* flags);
typedef void* (__cdecl*    ObjectPtr_t)(DWORD guidLo, DWORD guidHi, DWORD typeMask, const char* file, int line);
typedef int   (__cdecl*    EnumCb_t)(DWORD guidLo, DWORD guidHi, void* arg);
typedef int   (__cdecl*    EnumVisible_t)(EnumCb_t cb, void* arg);
typedef unsigned __int64 (__cdecl* GetActiveGuid_t)();
typedef int   (__thiscall* GetReaction_t)(void* unit, void* other);
typedef C3Vector* (__thiscall* NamePosition_t)(void* unit, C3Vector* out);

struct Bar { float fx, fy, health, scale; int reaction; DWORD guidLo, guidHi; bool isPlayer; };
static const int kMaxBars = 96;
static Bar g_bars[kMaxBars];
static int g_count = 0;

static void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sAnimSpeedFix.log", g_dir);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME t; GetLocalTime(&t);
    char msg[768];
    va_list ap; va_start(ap, fmt);
    int n = _snprintf_s(msg, sizeof(msg), _TRUNCATE, "[%02d:%02d:%02d pid%lu] unitbars: ",
                        t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    int m = _vsnprintf_s(msg + n, sizeof(msg) - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0 && m >= 0) {
        int len = n + m;
        if (len < (int)sizeof(msg) - 2) { msg[len] = '\r'; msg[len + 1] = '\n'; len += 2; }
        DWORD w = 0; WriteFile(h, msg, (DWORD)len, &w, NULL);
    }
    CloseHandle(h);
}

static bool IsReadable(const void* addr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return false;
    return (const BYTE*)addr + len <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

static bool SigOk(DWORD site, const BYTE* sig, size_t len, const char* name)
{
    if (IsReadable((void*)site, len) && memcmp((void*)site, sig, len) == 0)
        return true;
    Log("0x%08lX (%s) signature mismatch - not applied", site, name);
    return false;
}

static bool JumpTo(DWORD site, void* target)
{
    DWORD old = 0;
    BYTE* p = (BYTE*)site;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)target - (site + 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

// ---------------------------------------------------------------- the scan
struct ScanCtx
{
    void* player;
    float px, py, pz;
    float range2;
    BYTE* worldFrame;
    float width, height;     // the frame's DDC extents
};

static int __cdecl ScanCb(DWORD guidLo, DWORD guidHi, void* arg)
{
    ScanCtx* c = (ScanCtx*)arg;
    if (g_count >= kMaxBars)
        return 0;
    BYTE* o = (BYTE*)((ObjectPtr_t)kObjectPtr)(guidLo, guidHi, kTypeMaskUnit, 0, 0);
    // Units and other players (never the player itself): every champion gets a bar too.
    DWORD const typeId = o ? *(DWORD*)(o + kOff_Type) : 0;
    if (!o || o == c->player || (typeId != kTypeIdUnit && typeId != kTypeIdPlayer))
        return 1;
    BYTE* d = *(BYTE**)(o + kOff_Desc);
    if (!d)
        return 1;
    DWORD const health = *(DWORD*)(d + kDesc_Health);
    DWORD const maxHealth = *(DWORD*)(d + kDesc_MaxHealth);
    if (!health || !maxHealth || (*(DWORD*)(d + kDesc_Flags) & kFlagNotSelectable))
        return 1;
    // A shopkeeper, a trainer - anything with an NPC function - is not something
    // to fight, and gets no bar.
    if (*(DWORD*)(d + kDesc_NpcFlags))
        return 1;

    float const* pos = (float const*)(o + kOff_PosX);
    float const dx = pos[0] - c->px, dy = pos[1] - c->py, dz = pos[2] - c->pz;
    if (dx * dx + dy * dy + dz * dz > c->range2)
        return 1;

    // The name position through the unit's own vtable (a player's or a
    // creature's override, whichever it is).
    C3Vector head;
    NamePosition_t namePos = (NamePosition_t)(*(DWORD**)o)[8];
    namePos(o, &head);

    C3Vector screen = { 0, 0, 0 };
    DWORD flags = 0;
    if (!((WorldToScreen_t)kWorldToScreen)(c->worldFrame, &head, &screen, &flags))
        return 1;                                    // behind the camera
    float const fx = screen.x / c->width, fy = screen.y / c->height;
    if (fx < -0.05f || fx > 1.05f || fy < -0.05f || fy > 1.05f)
        return 1;

    // How big a yard is on screen here, as a fraction of the frame's height: a
    // point a yard above the head, projected the same way. The addon sizes the
    // bar by it, so it shrinks with distance the way the name over it does.
    C3Vector above = head;
    above.z += 1.0f;
    C3Vector screenAbove = { 0, 0, 0 };
    DWORD flagsAbove = 0;
    float scale = 0.0f;
    if (((WorldToScreen_t)kWorldToScreen)(c->worldFrame, &above, &screenAbove, &flagsAbove))
        scale = (screenAbove.y - screen.y) / c->height;
    if (scale < 0.0f) scale = 0.0f;

    Bar& b = g_bars[g_count++];
    b.fx = fx;
    b.fy = fy;
    b.scale = scale;
    b.guidLo = guidLo;
    b.guidHi = guidHi;
    b.isPlayer = typeId == kTypeIdPlayer;
    b.health = maxHealth ? float(health) / float(maxHealth) : 0.0f;
    if (b.health > 1.0f) b.health = 1.0f;
    b.reaction = ((GetReaction_t)kGetReaction)(c->player, o) + 1;
    return 1;
}

static void Scan()
{
    g_count = 0;
    BYTE* wf = kWorldFramePtr;
    if (!wf || !*(void**)(wf + 0x7E20))
        return;                                      // no camera yet
    unsigned __int64 const guid = ((GetActiveGuid_t)kGetActiveGuid)();
    if (!guid)
        return;
    BYTE* player = (BYTE*)((ObjectPtr_t)kObjectPtr)(DWORD(guid), DWORD(guid >> 32), kTypeMaskUnit, 0, 0);
    if (!player)
        return;

    ScanCtx c;
    c.player = player;
    c.px = *(float*)(player + kOff_PosX);
    c.py = *(float*)(player + kOff_PosX + 4);
    c.pz = *(float*)(player + kOff_PosX + 8);
    c.range2 = g_range * g_range;
    c.worldFrame = wf;
    c.width = *(float*)(wf + 0x70) - *(float*)(wf + 0x68);
    c.height = *(float*)(wf + 0x6C) - *(float*)(wf + 0x64);
    if (c.width <= 0.0f || c.height <= 0.0f)
        return;
    ((EnumVisible_t)kEnumVisible)(ScanCb, &c);
}

// n = CenturionUnitBarsScan()
static int __cdecl Lua_Scan(void* L)
{
    __try {
        Scan();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_count = 0;
        if (g_faults++ < 5)
            Log("scan faulted (0x%08lX) - no bars this frame", GetExceptionCode());
    }
    pLuaPushNumber(L, double(g_count));
    return 1;
}

// The world frame and its DDC extents, as Scan reads them.
static bool WorldFrame(BYTE*& wf, float& width, float& height)
{
    wf = kWorldFramePtr;
    if (!wf || !*(void**)(wf + 0x7E20))
        return false;                                // no camera yet
    width = *(float*)(wf + 0x70) - *(float*)(wf + 0x68);
    height = *(float*)(wf + 0x6C) - *(float*)(wf + 0x64);
    return width > 0.0f && height > 0.0f;
}

// fx, fy, scale = CenturionUnitBar(0, x, y, z): a world point where the bars are
// placed (fractions of the world frame; nothing behind the camera), and how big a
// yard is there. CENTURION_Rift draws Summoner's Rift's turret range rings by it.
static int ProjectPoint(void* L)
{
    BYTE* wf = nullptr;
    float width = 0.0f, height = 0.0f;
    if (!WorldFrame(wf, width, height))
        return 0;
    C3Vector point = { float(pLuaToNumber(L, 2)), float(pLuaToNumber(L, 3)), float(pLuaToNumber(L, 4)) };
    C3Vector screen = { 0, 0, 0 };
    DWORD flags = 0;
    if (!((WorldToScreen_t)kWorldToScreen)(wf, &point, &screen, &flags))
        return 0;
    C3Vector above = point;
    above.z += 1.0f;
    C3Vector screenAbove = { 0, 0, 0 };
    DWORD flagsAbove = 0;
    float scale = 0.0f;
    if (((WorldToScreen_t)kWorldToScreen)(wf, &above, &screenAbove, &flagsAbove))
        scale = (screenAbove.y - screen.y) / height;
    pLuaPushNumber(L, screen.x / width);
    pLuaPushNumber(L, screen.y / height);
    pLuaPushNumber(L, scale < 0.0f ? 0.0f : scale);
    return 3;
}

// x, y, z = CenturionUnitBar(-1): the player's position in the world.
static int PlayerPosition(void* L)
{
    unsigned __int64 const guid = ((GetActiveGuid_t)kGetActiveGuid)();
    BYTE* player = guid ? (BYTE*)((ObjectPtr_t)kObjectPtr)(DWORD(guid), DWORD(guid >> 32), kTypeMaskUnit, 0, 0) : nullptr;
    if (!player)
        return 0;
    float const* pos = (float const*)(player + kOff_PosX);
    pLuaPushNumber(L, pos[0]);
    pLuaPushNumber(L, pos[1]);
    pLuaPushNumber(L, pos[2]);
    return 3;
}

// fx, fy, health, reaction, scale, guid, isPlayer = CenturionUnitBar(i)
// (0 and -1: ProjectPoint, PlayerPosition - no new thunk for them)
static int __cdecl Lua_Bar(void* L)
{
    int const i = int(pLuaToNumber(L, 1));
    if (i == 0 || i == -1)
    {
        int pushed = 0;
        __try {
            pushed = i == 0 ? ProjectPoint(L) : PlayerPosition(L);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            pushed = 0;
            if (g_faults++ < 5)
                Log("%s faulted (0x%08lX)", i == 0 ? "project" : "position", GetExceptionCode());
        }
        return pushed;
    }
    if (i < 1 || i > g_count)
        return 0;
    Bar const& b = g_bars[i - 1];
    pLuaPushNumber(L, b.fx);
    pLuaPushNumber(L, b.fy);
    pLuaPushNumber(L, b.health);
    pLuaPushNumber(L, double(b.reaction));
    pLuaPushNumber(L, b.scale);
    char guid[24];
    _snprintf_s(guid, sizeof(guid), _TRUNCATE, "0x%08lX%08lX", b.guidHi, b.guidLo);   // UnitGUID()'s form
    pLuaPushString(L, guid);
    pLuaPushNumber(L, b.isPlayer ? 1.0 : 0.0);
    return 7;
}

// CenturionTargetGUID("0xF130...") - targets the unit with that GUID, as a click
// on it in the world would. For the bars: TargetUnit takes a unit token or a
// name, and every minion of a side shares its name.
static int __cdecl Lua_Target(void* L)
{
    const char* text = pLuaToLString(L, 1, NULL);
    if (!text || text[0] != '0' || (text[1] != 'x' && text[1] != 'X') || strlen(text) != 18)
        return 0;
    unsigned __int64 guid = 0;
    for (const char* c = text + 2; *c; ++c)
    {
        int v = (*c >= '0' && *c <= '9') ? *c - '0' : (*c >= 'A' && *c <= 'F') ? *c - 'A' + 10
              : (*c >= 'a' && *c <= 'f') ? *c - 'a' + 10 : -1;
        if (v < 0)
            return 0;
        guid = (guid << 4) | unsigned(v);
    }
    // Run as the game's own code, not the addon's. FrameScript stamps every Lua
    // value with the current taint (the global at 0x00D4139C; lua_pushnumber
    // 0x0084E2A0 reads it), and the target change that follows - the target
    // frame's protected parts updating - inherited the addon's taint and was
    // blocked ("CENTURION_Rift has been blocked from an action only available to
    // the Blizzard UI"). A click on a unit in the world runs with none.
    DWORD* const taint = (DWORD*)kTaint;
    DWORD const saved = *taint;
    *taint = 0;
    __try {
        ((SetTarget_t)kSetTarget)(DWORD(guid), DWORD(guid >> 32));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_faults++ < 5)
            Log("target faulted (0x%08lX)", GetExceptionCode());
    }
    *taint = saved;
    return 0;
}

void UnitBars_WorldLuaReady()
{
    if (!g_ready)
        return;
    g_count = 0;
    pRegisterFunction("CenturionUnitBarsScan", (void*)kScanThunk);
    pRegisterFunction("CenturionUnitBar", (void*)kBarThunk);
    if (g_targetReady)
        pRegisterFunction("CenturionTargetGUID", (void*)kTargetThunk);
}

void UnitBars_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir);
    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;
    g_enabled = GetPrivateProfileIntA("UnitBars", "Enabled", 1, ini);
    int const range = GetPrivateProfileIntA("UnitBars", "Range", 80, ini);
    if (range >= 10 && range <= 200)
        g_range = float(range);
}

void UnitBars_Install()
{
    if (!g_enabled)
        return;
    // Everything verified before anything is written.
    if (!SigOk(kWorldToScreen, kWorldToScreenSig, sizeof(kWorldToScreenSig), "WorldToScreen") ||
        !SigOk(kEnumVisible, kEnumSig, sizeof(kEnumSig), "EnumVisibleObjects") ||
        !SigOk(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr") ||
        !SigOk(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActivePlayerGuid") ||
        !SigOk(kGetReaction, kGetReactionSig, sizeof(kGetReactionSig), "GetReaction") ||
        !SigOk(kNamePosition, kNamePositionSig, sizeof(kNamePositionSig), "unit name position") ||
        !IsReadable((void*)(kUnitVtable + 8 * 4), 4) || *(DWORD*)(kUnitVtable + 8 * 4) != kNamePosition ||
        !SigOk(kScanThunk, kPadSig, sizeof(kPadSig), "scan thunk padding") ||
        !SigOk(kBarThunk, kPadSig, sizeof(kPadSig), "bar thunk padding"))
        return;
    if (!JumpTo(kScanThunk, (void*)&Lua_Scan) || !JumpTo(kBarThunk, (void*)&Lua_Bar)) {
        Log("not applied: VirtualProtect failed");
        return;
    }
    g_ready = 1;
    // Click-to-target is its own piece: without it the bars still draw.
    if (SigOk(kSetTarget, kSetTargetSig, sizeof(kSetTargetSig), "CGGameUI::Target") &&
        SigOk(kTargetThunk, kPadSig, sizeof(kPadSig), "target thunk padding") &&
        JumpTo(kTargetThunk, (void*)&Lua_Target))
        g_targetReady = 1;
    Log("installed (range %.0f yd, click-to-target %s)", g_range, g_targetReady ? "on" : "OFF");
}
