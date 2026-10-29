// spectate.cpp - CenturionSpectate(): the normal camera, centred on another unit,
// that does not swing round when that unit turns. See spectate.h for the Lua
// side and docs/SPECTATE_CAMERA_RE.md for the RE.
//
// Reversing (Wow.exe 3.3.5a 12340; addresses are VAs):
//
//   0x004F6F50  CGWorldFrame::SetCameraTarget, thiscall(worldFrame, CGObject*,
//               int flag), ret 8 - what the client itself calls to put the
//               camera on a farsight object (0x006E2A01) or back on the player
//               (0x005186DB). Camera target guid at camera+0x88.
//   0x004FA69B  the world frame's per-frame camera block: keeps the camera's
//               target while that object exists, else falls back to the active
//               player. It starts with IsCommentator(active player)
//               (0x006DE980), which this module hooks only to get a call every
//               frame at that point (return address 0x004FA6A2); the answer is
//               always the client's own - the commentator camera is not used.
//   camera yaw  0x00604500..: world yaw = relative yaw + camera+0x12C + the
//               TARGET'S FACING (+0x1D4) - unless camera+0xAC > 0, a lock count:
//   0x006019B0  CGCamera yaw lock begin, thiscall(camera), ret: ++[+0xAC]; on
//               0 -> 1 it adds the target's facing into the yaw so the view
//               does not jump (0x005FE5F0). Mouse look uses it.
//   0x00601A70  yaw lock end, thiscall(camera), ret: --[+0xAC]; on 1 -> 0 it
//               takes the facing back out.
//               So with the camera on the unit and one lock held, the camera
//               follows the unit's position only; zoom, left/right drag and the
//               rest are the normal camera's own controls.
//   0x005FBBC0  thiscall(input control, time, start): a movement key lands here
//               (MoveForwardStart 0x005FC200 -> 0x005FA170 sets bit 0x10 ->
//               here); its IsCommentator call returns to 0x005FBC07. The control
//               bits are at [[0x00C24954]+4]: forward 0x10, back 0x20, strafe
//               0x40/0x80, turn 0x100/0x200, autorun 0x1000, both mouse 0x3.
//   0x005277B0  interact(guidLo, guidHi), cdecl: a right-click on a unit in the
//               world (0x004F78EA; a left-click goes to 0x005274F0, target) and
//               InteractUnit(). InteractHook takes armed right-clicks on players.
//   0x0051FF20  SetFocus(WGUID*), cdecl - what ClearFocus (0x005225E0) and
//               FocusUnit call: moves the focus flag 0x2000 between the units,
//               stores the guid at 0x00BD07D0 and fires PLAYER_FOCUS_CHANGED
//               (event 0x9E). FocusUnit is protected in Lua; the replay's
//               spectate puts the watched player there so the player frame can
//               show them ("focus" gets every UNIT_* event).
//
// The Lua function is registered as a 5-byte jmp in int3 padding inside .text
// (Lua will not call a function outside it, gluebridge.cpp): 0x00569904, in the
// 15-byte run after the commentator code at 0x00569901.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "spectate.h"

static char  g_dir[MAX_PATH] = { 0 };
static int   g_enabled = 1;          // [Spectate] Enabled
static int   g_ready = 0;            // 1 camera + Lua, 2 right-click too, 4 focus
static int   g_faults = 0;

static volatile LONG g_on = 0;       // a unit to keep the camera on
static volatile LONG g_tick = 0;     // the per-frame call has work (a follow, or a restore)
static volatile LONG g_needRestore = 0;
static volatile LONG g_stoppedByMove = 0;
static volatile LONG g_landing = 0;  // a movement key ended the follow: camera stays until the server lands us there
static DWORD g_landStart = 0;
static DWORD g_followLo = 0, g_followHi = 0;
static BYTE* g_lockCamera = NULL;    // the camera holding our yaw lock

static const DWORD kIsCommentator = 0x006DE980;
static const BYTE  kIsCommentatorSig[] = { 0xA1, 0x8C, 0x08, 0xBD, 0x00, 0x8B, 0x15, 0x70, 0x41, 0xAD, 0x00 };
static DWORD       g_isCommentatorResume = 0x006DE985;

static const DWORD kSetCameraTarget = 0x004F6F50;
static const BYTE  kSetCameraTargetSig[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x8B, 0x89, 0x20, 0x7E, 0x00, 0x00 };
static const DWORD kYawLockBegin = 0x006019B0;
static const BYTE  kYawLockBeginSig[] = { 0x55, 0x8B, 0xEC, 0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xAC, 0x00, 0x00, 0x00 };
static const DWORD kYawLockEnd = 0x00601A70;
static const BYTE  kYawLockEndSig[] = { 0x55, 0x8B, 0xEC, 0x51, 0x56, 0x8B, 0xF1, 0x83, 0x86, 0xAC, 0x00, 0x00, 0x00, 0xFF };

static const DWORD kGetActiveGuid = 0x004D3790;
static const BYTE  kGetActiveSig[] = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };
static const DWORD kObjectPtr = 0x004D4DB0;
static const BYTE  kObjectPtrSig[] = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00 };

static const DWORD kSetFocus = 0x0051FF20;
static const BYTE  kSetFocusSig[] = { 0x55, 0x8B, 0xEC, 0xA1, 0xD0, 0x07, 0xBD, 0x00, 0x56, 0x8B, 0x75, 0x08 };
typedef void (__cdecl* SetFocus_t)(DWORD* guid);
#define kFocusGuid ((DWORD*)0x00BD07D0)
static int   g_focusBorrowed = 0;
static DWORD g_savedFocus[2] = { 0, 0 };

static const DWORD kLuaThunk = 0x00569904;
static const BYTE  kPadSig[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };

#define kWorldFramePtr (*(BYTE**)0x00B7436C)
static const DWORD kOff_Camera     = 0x7E20;
static const DWORD kCam_Target     = 0x88;
static const DWORD kCam_YawLock    = 0xAC;
static const DWORD kOff_Type       = 0x14;   // TYPEID: 3 unit, 4 player
static const DWORD kOff_PosX       = 0x798;  // float x, y +0x79C, z +0x7A0 (playercollide.cpp)
static const DWORD kLandTimeoutMs  = 1500;
static const float kLandNear       = 4.0f;
static const DWORD kTypeMaskUnit   = 0x08;

static const DWORD kRetCameraBlock = 0x004FA6A2;
static const DWORD kRetMoveInput   = 0x005FBC07;
static const DWORD kMoveBits       = 0x000013F0;
static const DWORD kBothMouseBits  = 0x00000003;

typedef void        (__cdecl* RegisterFunction_t)(const char*, void*);
typedef void        (__cdecl* LuaPushNumber_t)(void*, double);
typedef void        (__cdecl* LuaPushString_t)(void*, const char*);
typedef void        (__cdecl* LuaPushNil_t)(void*);
typedef const char* (__cdecl* LuaToLString_t)(void*, int, size_t*);
typedef void*       (__cdecl* ObjectPtr_t)(DWORD guidLo, DWORD guidHi, DWORD typeMask, const char* file, int line);
typedef unsigned __int64 (__cdecl* GetActiveGuid_t)();
typedef void        (__thiscall* SetCameraTarget_t)(void* worldFrame, void* object, int flag);
typedef void        (__thiscall* YawLock_t)(void* camera);
static const RegisterFunction_t pRegisterFunction = (RegisterFunction_t)0x00817F90;
static const LuaPushNumber_t    pLuaPushNumber    = (LuaPushNumber_t)   0x0084E2A0;
static const LuaPushString_t    pLuaPushString    = (LuaPushString_t)   0x0084E350;
static const LuaPushNil_t       pLuaPushNil       = (LuaPushNil_t)      0x0084E280;
static const LuaToLString_t     pLuaToLString     = (LuaToLString_t)    0x0084E0E0;

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
    int n = _snprintf_s(msg, sizeof(msg), _TRUNCATE, "[%02d:%02d:%02d pid%lu] spectate: ",
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

// ------------------------------------------------------------ the camera
static BYTE* Camera()
{
    BYTE* const wf = kWorldFramePtr;
    return wf ? *(BYTE**)(wf + kOff_Camera) : NULL;
}

static void* UnitObject(DWORD lo, DWORD hi)
{
    BYTE* const o = (BYTE*)((ObjectPtr_t)kObjectPtr)(lo, hi, kTypeMaskUnit, "spectate.cpp", __LINE__);
    if (!o)
        return NULL;
    DWORD const type = *(DWORD*)(o + kOff_Type);
    return (type == 3 || type == 4) ? o : NULL;
}

// The camera back on the player, and our yaw lock let go of.
static void RestoreCamera()
{
    BYTE* const wf = kWorldFramePtr;
    BYTE* const cam = Camera();
    if (!wf || !cam)
    {
        g_lockCamera = NULL;
        return;
    }

    // While the camera is still on the unit, so the lock takes the unit's
    // facing back out of the yaw it put in.
    if (g_lockCamera == cam && *(int*)(cam + kCam_YawLock) > 0)
        ((YawLock_t)kYawLockEnd)(cam);
    g_lockCamera = NULL;

    DWORD const targetLo = *(DWORD*)(cam + kCam_Target);
    DWORD const targetHi = *(DWORD*)(cam + kCam_Target + 4);
    unsigned __int64 const active = ((GetActiveGuid_t)kGetActiveGuid)();
    if (targetLo == DWORD(active) && targetHi == DWORD(active >> 32))
        return;
    if (void* player = UnitObject(DWORD(active), DWORD(active >> 32)))
        ((SetCameraTarget_t)kSetCameraTarget)(wf, player, 0);
}

// After a movement key: the server puts the player where the unit stands
// (BGReplay.cpp, SPECTATE OFF LAND). The camera stays on the unit until then,
// so it does not swing back to where the player was for a moment first.
static bool Landed()
{
    if (GetTickCount() - g_landStart > kLandTimeoutMs)
        return true;
    BYTE* const unit = (BYTE*)UnitObject(g_followLo, g_followHi);
    unsigned __int64 const active = ((GetActiveGuid_t)kGetActiveGuid)();
    BYTE* const player = (BYTE*)UnitObject(DWORD(active), DWORD(active >> 32));
    if (!unit || !player)
        return true;
    float const* const u = (float const*)(unit + kOff_PosX);
    float const* const p = (float const*)(player + kOff_PosX);
    float const dx = u[0] - p[0], dy = u[1] - p[1], dz = u[2] - p[2];
    return dx * dx + dy * dy + dz * dz <= kLandNear * kLandNear;
}

// Every frame while there is work: keep the camera on the unit (put it back
// on after a replay restart re-creates the unit), or hand it back.
static void CameraTick()
{
    if (!g_on)
    {
        if (g_landing)
        {
            if (!Landed())
                return;
            InterlockedExchange(&g_landing, 0);
            RestoreCamera();
        }
        if (g_needRestore)
        {
            InterlockedExchange(&g_needRestore, 0);
            RestoreCamera();
        }
        InterlockedExchange(&g_tick, 0);
        return;
    }

    BYTE* const wf = kWorldFramePtr;
    BYTE* const cam = Camera();
    if (!wf || !cam)
        return;

    if (*(DWORD*)(cam + kCam_Target) != g_followLo || *(DWORD*)(cam + kCam_Target + 4) != g_followHi)
    {
        void* const unit = UnitObject(g_followLo, g_followHi);
        if (!unit)
            return;   // not here (yet): the client keeps the camera on the player
        ((SetCameraTarget_t)kSetCameraTarget)(wf, unit, 0);
    }

    // One lock while the camera is on the unit: its turning stays out of the
    // yaw. Taken again if a view change (0x00606970) zeroed the count.
    if (g_lockCamera != cam || *(int*)(cam + kCam_YawLock) <= 0)
    {
        ((YawLock_t)kYawLockBegin)(cam);
        g_lockCamera = cam;
    }
}

static void __stdcall CameraTickFromHook()
{
    __try {
        CameraTick();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_on, 0);
        InterlockedExchange(&g_tick, 0);
        InterlockedExchange(&g_landing, 0);
        g_lockCamera = NULL;
        if (g_faults++ < 5)
            Log("camera tick faulted (0x%08lX) - follow off", GetExceptionCode());
    }
}

// A movement key while following: the follow ends, the camera goes back once
// the player has landed on the unit (Landed), and the addon hears why on its
// next CenturionSpectate("?").
static void __stdcall StopFollowFromHook()
{
    if (!g_on)
        return;
    g_landStart = GetTickCount();
    InterlockedExchange(&g_landing, 1);
    InterlockedExchange(&g_on, 0);
    InterlockedExchange(&g_stoppedByMove, 1);
}

// ------------------------------------------------------------ the hooks
//
// IsCommentator(ecx = player) always answers as the client does; two of its
// callers are used as hook points, told apart by return address:
//   0x004FA6A2  the world frame's camera block, every frame: CameraTick
//   0x005FBC07  movement input: a key going down ([ebp+0Ch] = 0x005FBBC0's
//               "start") with a movement bit set ends the follow
__declspec(naked) static void IsCommentatorHook()
{
    __asm {
        mov  eax, dword ptr [esp]
        cmp  eax, kRetCameraBlock
        jne  notCamera
        cmp  dword ptr [g_tick], 0
        je   original
        push ecx
        call CameraTickFromHook            // stdcall, no arguments
        pop  ecx
        jmp  original

    notCamera:
        cmp  eax, kRetMoveInput
        jne  original
        cmp  dword ptr [g_on], 0
        je   original
        cmp  dword ptr [ebp + 0Ch], 0
        je   original
        mov  eax, dword ptr ds:[0x00C24954]
        test eax, eax
        je   original
        mov  eax, dword ptr [eax + 4]
        test eax, kMoveBits
        jne  stopFollow
        and  eax, kBothMouseBits
        cmp  eax, kBothMouseBits
        jne  original
    stopFollow:
        push ecx
        call StopFollowFromHook
        pop  ecx

    original:
        mov  eax, dword ptr ds:[0x00BD088C]   // the 5 bytes the jmp replaced
        jmp  dword ptr [g_isCommentatorResume]
    }
}

// 0x005277B0 interact(guidLo, guidHi): while armed (CenturionSpectate("arm"), in
// a replay), a right-click on another player is "spectate them" - kept for
// CenturionSpectate("click") and not acted on (no swing at a replay actor).
static const DWORD kInteract = 0x005277B0;
static const BYTE  kInteractSig[] = { 0x55, 0x8B, 0xEC, 0xE8, 0xD8, 0xBF, 0xFA, 0xFF };
static DWORD       g_interactResume = 0x005277B8;
static volatile LONG g_armed = 0;
static DWORD g_clickLo = 0, g_clickHi = 0;
static volatile LONG g_clickSeq = 0, g_clickRead = 0;

static int __cdecl TakeRightClick(DWORD lo, DWORD hi)
{
    if (!g_armed || !lo || hi)           // players only: high guid 0
        return 0;
    unsigned __int64 const active = ((GetActiveGuid_t)kGetActiveGuid)();
    if (DWORD(active) == lo && DWORD(active >> 32) == hi)
        return 0;
    g_clickLo = lo;
    g_clickHi = hi;
    InterlockedIncrement(&g_clickSeq);
    return 1;
}

__declspec(naked) static void InteractHook()
{
    __asm {
        push dword ptr [esp + 8]           // guid hi
        push dword ptr [esp + 8]           // guid lo (shifted by the push)
        call TakeRightClick
        add  esp, 8
        test eax, eax
        jne  taken
        push ebp                           // the 8 bytes the jmp replaced:
        mov  ebp, esp                      //   push ebp / mov ebp, esp /
        mov  eax, kGetActiveGuid           //   call 0x004D3790
        call eax
        jmp  dword ptr [g_interactResume]
    taken:
        mov  eax, 1
        ret
    }
}

// ------------------------------------------------------------ Lua
static void StartFollow(DWORD lo, DWORD hi)
{
    g_followLo = lo;
    g_followHi = hi;
    InterlockedExchange(&g_needRestore, 0);
    InterlockedExchange(&g_landing, 0);
    InterlockedExchange(&g_on, 1);
    InterlockedExchange(&g_tick, 1);
    CameraTick();                      // now, not next frame
}

static void StopFollow()
{
    if (!g_on && !g_needRestore && !g_landing)
        return;
    InterlockedExchange(&g_on, 0);
    InterlockedExchange(&g_needRestore, 0);
    InterlockedExchange(&g_landing, 0);
    RestoreCamera();
    InterlockedExchange(&g_tick, 0);
}

static bool ParseGuid(const char* text, DWORD& lo, DWORD& hi)
{
    if (!text || text[0] != '0' || (text[1] != 'x' && text[1] != 'X') || strlen(text) != 18)
        return false;
    unsigned __int64 guid = 0;
    for (const char* ch = text + 2; *ch; ++ch)
    {
        int v = (*ch >= '0' && *ch <= '9') ? *ch - '0' : (*ch >= 'A' && *ch <= 'F') ? *ch - 'A' + 10
              : (*ch >= 'a' && *ch <= 'f') ? *ch - 'a' + 10 : -1;
        if (v < 0)
            return false;
        guid = (guid << 4) | unsigned(v);
    }
    if (!guid)
        return false;
    lo = DWORD(guid);
    hi = DWORD(guid >> 32);
    return true;
}

static void PushGuid(void* L, DWORD lo, DWORD hi)
{
    char guid[24];
    _snprintf_s(guid, sizeof(guid), _TRUNCATE, "0x%08lX%08lX", hi, lo);   // UnitGUID()'s form
    pLuaPushString(L, guid);
}

// guid, present, stoppedByMove: what is followed (nil when not), whether that
// unit is here now, and 1 once after a movement key ended the follow.
static int PushFollowed(void* L)
{
    double const moved = InterlockedExchange(&g_stoppedByMove, 0) ? 1.0 : 0.0;
    if (!g_on)
    {
        pLuaPushNil(L);
        pLuaPushNumber(L, 0.0);
        pLuaPushNumber(L, moved);
        return 3;
    }
    PushGuid(L, g_followLo, g_followHi);
    pLuaPushNumber(L, UnitObject(g_followLo, g_followHi) ? 1.0 : 0.0);
    pLuaPushNumber(L, moved);
    return 3;
}

static int Spectate(void* L)
{
    const char* arg = pLuaToLString(L, 1, NULL);

    if (arg && arg[0] == '?' && !arg[1])
        return PushFollowed(L);

    if (arg && !_stricmp(arg, "arm"))
    {
        InterlockedExchange(&g_armed, (g_ready & 2) ? 1 : 0);
        pLuaPushNumber(L, g_armed ? 1.0 : 0.0);
        return 1;
    }
    if (arg && !_stricmp(arg, "disarm"))
    {
        InterlockedExchange(&g_armed, 0);
        pLuaPushNumber(L, 0.0);
        return 1;
    }
    // 2: knows "focus" and lands the camera after a movement key.
    if (arg && !_stricmp(arg, "version"))
    {
        pLuaPushNumber(L, 2.0);
        return 1;
    }

    // "focus", "0x<guid>" | "off": the watched player as the focus (the
    // focus there before comes back on "off"). Returns the focus guid now.
    if (arg && !_stricmp(arg, "focus"))
    {
        if (!(g_ready & 4))
        {
            pLuaPushNil(L);
            return 1;
        }
        const char* what = pLuaToLString(L, 2, NULL);
        DWORD lo = 0, hi = 0;
        if (ParseGuid(what, lo, hi))
        {
            if (!g_focusBorrowed)
            {
                g_savedFocus[0] = kFocusGuid[0];
                g_savedFocus[1] = kFocusGuid[1];
                g_focusBorrowed = 1;
            }
            DWORD guid[2] = { lo, hi };
            ((SetFocus_t)kSetFocus)(guid);
        }
        else if (g_focusBorrowed)
        {
            g_focusBorrowed = 0;
            DWORD guid[2] = { g_savedFocus[0], g_savedFocus[1] };
            ((SetFocus_t)kSetFocus)(guid);
        }
        if (kFocusGuid[0] || kFocusGuid[1])
            PushGuid(L, kFocusGuid[0], kFocusGuid[1]);
        else
            pLuaPushNil(L);
        return 1;
    }
    if (arg && !_stricmp(arg, "click"))
    {
        LONG const seq = g_clickSeq;
        if (seq == g_clickRead)
        {
            pLuaPushNil(L);
            return 1;
        }
        g_clickRead = seq;
        PushGuid(L, g_clickLo, g_clickHi);
        return 1;
    }

    DWORD lo = 0, hi = 0;
    if (ParseGuid(arg, lo, hi))
        StartFollow(lo, hi);
    else
        StopFollow();               // "off", nil, anything else
    return PushFollowed(L);
}

static int __cdecl Lua_Spectate(void* L)
{
    int pushed = 0;
    __try {
        pushed = Spectate(L);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_on, 0);
        InterlockedExchange(&g_tick, 0);
        InterlockedExchange(&g_landing, 0);
        g_lockCamera = NULL;
        pushed = 0;
        if (g_faults++ < 5)
            Log("CenturionSpectate faulted (0x%08lX) - follow off", GetExceptionCode());
    }
    return pushed;
}

void Spectate_WorldLuaReady()
{
    if (!g_ready)
        return;
    // A new world: a new camera, none of the old world's units. Nothing to
    // hand back - the client puts the camera on the player itself.
    InterlockedExchange(&g_on, 0);
    InterlockedExchange(&g_tick, 0);
    InterlockedExchange(&g_needRestore, 0);
    InterlockedExchange(&g_armed, 0);
    InterlockedExchange(&g_stoppedByMove, 0);
    InterlockedExchange(&g_landing, 0);
    g_lockCamera = NULL;
    g_focusBorrowed = 0;               // the old world's focus is gone with it
    pRegisterFunction("CenturionSpectate", (void*)kLuaThunk);
}

void Spectate_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir);
    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;
    g_enabled = GetPrivateProfileIntA("Spectate", "Enabled", 1, ini);
}

void Spectate_Install()
{
    if (!g_enabled)
        return;
    // Everything verified before anything is written.
    if (!SigOk(kIsCommentator, kIsCommentatorSig, sizeof(kIsCommentatorSig), "IsCommentator") ||
        !SigOk(kSetCameraTarget, kSetCameraTargetSig, sizeof(kSetCameraTargetSig), "SetCameraTarget") ||
        !SigOk(kYawLockBegin, kYawLockBeginSig, sizeof(kYawLockBeginSig), "camera yaw lock begin") ||
        !SigOk(kYawLockEnd, kYawLockEndSig, sizeof(kYawLockEndSig), "camera yaw lock end") ||
        !SigOk(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActivePlayerGuid") ||
        !SigOk(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr") ||
        !SigOk(kLuaThunk, kPadSig, sizeof(kPadSig), "spectate thunk padding"))
        return;
    if (!JumpTo(kLuaThunk, (void*)&Lua_Spectate) || !JumpTo(kIsCommentator, (void*)&IsCommentatorHook)) {
        Log("not applied: VirtualProtect failed");
        return;
    }
    g_ready = 1;
    // Right-click to spectate is its own piece: without it the camera still
    // follows whoever the addon names.
    if (SigOk(kInteract, kInteractSig, sizeof(kInteractSig), "CGGameUI interact") &&
        JumpTo(kInteract, (void*)&InteractHook))
        g_ready |= 2;
    // The focus is only called, never patched.
    if (SigOk(kSetFocus, kSetFocusSig, sizeof(kSetFocusSig), "SetFocus"))
        g_ready |= 4;
    Log("installed (normal camera + yaw lock, right-click %s, focus %s)",
        (g_ready & 2) ? "on" : "OFF", (g_ready & 4) ? "on" : "OFF");
}
