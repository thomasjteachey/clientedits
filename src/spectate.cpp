// spectate.cpp - CenturionSpectate(): follow a unit with the commentator camera.
// See spectate.h for the Lua side and docs/SPECTATE_CAMERA_RE.md for the RE.
//
// Reversing (Wow.exe 3.3.5a 12340; addresses are VAs):
//
//   0x006DE980  CGPlayer_C::IsCommentator, thiscall(player) -> bool. PLAYER_FLAGS
//               ([player+0x08] descriptors, +0x08) bit 0x00080000, and then bit
//               0x00400000 or the current map's InstanceType ([0xBD088C] row,
//               +0x08) == 4. 18 callers, all asking about the local player or a
//               unit the camera/input code already resolved to it.
//               Starts with `mov eax, [0x00BD088C]` (5 bytes) - the hook site.
//   0x004FA69B  CGWorldFrame per-frame camera block: IsCommentator(active
//               player) -> the camera's target is dropped (0x006066E0 with NULL)
//               and CGCommentator::Update (0x005689A0) places the camera; else
//               the camera keeps its target, or falls back to the active
//               player when that unit is gone. So turning the answer off hands
//               the camera straight back.
//   0x00ACE4A8  the CGCommentator singleton:
//               +0x00 yaw, +0x04 pitch (radians, the camera matrix's own Euler
//               convention, 0x004C56D0; pitch > 0 looks down)
//               +0x08 free-fly turn rate   +0x0C..+0x14 free-fly position
//               +0x18..+0x20 move input (+0x18 = W/S, set by 0x0056B5C0)
//               +0x28/+0x2C look-at guid   +0x30/+0x34 FOLLOW guid
//               +0x38 follow distance (clamped 1..75 yd each frame)
//               +0x3C current FOV, +0x40 target FOV (eased into camera+0x40)
//               +0x44 move speed (W/S change the distance by this per second)
//               +0x48 camera collision on/off (CWorld::Intersect on the eye)
//               +0x4C height offset over the unit's 0.75 * height (+0x854)
//   0x005689A0  CGCommentator::Update(dt): with the follow guid's object found,
//               eye = unit position + height - forward(yaw, pitch) * distance,
//               camera placed by 0x006038A0 and oriented by 0x00607BD0 with the
//               commentator's yaw and pitch - never the unit's facing.
//   [0x00B7436C]+0x7E20  the active CGCamera: +0x08 position, +0x14 rotation
//               matrix (row 0 = forward: yaw = atan2(m1, m0), pitch = asin(-m2),
//               0x004C5630), +0x40 FOV.
//   Mouse look (0x005FB2AE / 0x005FBEBA / 0x005FBAC7) and movement input
//   (0x005FBC02) already route to the commentator in commentator mode.
//
// The Lua function is registered as a 5-byte jmp in int3 padding inside .text
// (Lua will not call a function outside it, gluebridge.cpp): 0x00569904, in the
// 15-byte run after the commentator code at 0x00569901.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "spectate.h"

static char  g_dir[MAX_PATH] = { 0 };
static int   g_enabled = 1;          // [Spectate] Enabled
static float g_defaultDistance = 12.0f;   // [Spectate] Distance
static float g_zoomSpeed = 15.0f;         // [Spectate] ZoomSpeed, yards a second on W/S
static int   g_ready = 0;
static int   g_faults = 0;

static volatile LONG g_on = 0;       // the commentator answer is "yes"
static DWORD g_followLo = 0, g_followHi = 0;

static const DWORD kIsCommentator = 0x006DE980;
static const BYTE  kIsCommentatorSig[] = { 0xA1, 0x8C, 0x08, 0xBD, 0x00, 0x8B, 0x15, 0x70, 0x41, 0xAD, 0x00 };
static DWORD       g_isCommentatorResume = 0x006DE985;
static const DWORD kCurrentMapRow = 0x00BD088C;

static const DWORD kCommentator = 0x00ACE4A8;
// CommentatorFollowPlayer (0x00569B50) writes the follow guid here; the check
// that it is still the same build's layout.
static const DWORD kFollowWriteSite = 0x00569C9B;
static const BYTE  kFollowWriteSig[] = { 0x89, 0x0D, 0xD8, 0xE4, 0xAC, 0x00 };
static const DWORD kUpdate = 0x005689A0;
static const BYTE  kUpdateSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x50, 0x56, 0x8B, 0xF1 };

static const DWORD kGetActiveGuid = 0x004D3790;
static const BYTE  kGetActiveSig[] = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };
static const DWORD kObjectPtr = 0x004D4DB0;
static const BYTE  kObjectPtrSig[] = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00 };

static const DWORD kLuaThunk = 0x00569904;
static const BYTE  kPadSig[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };

#define kWorldFramePtr (*(BYTE**)0x00B7436C)
static const DWORD kOff_Camera     = 0x7E20;
static const DWORD kCam_Pos        = 0x08;
static const DWORD kCam_Matrix     = 0x14;
static const DWORD kCam_Fov        = 0x40;

static const DWORD kCm_Yaw         = 0x00;
static const DWORD kCm_Pitch       = 0x04;
static const DWORD kCm_TurnRate    = 0x08;
static const DWORD kCm_Pos         = 0x0C;
static const DWORD kCm_Move        = 0x18;
static const DWORD kCm_LookAt      = 0x28;
static const DWORD kCm_Follow      = 0x30;
static const DWORD kCm_Distance    = 0x38;
static const DWORD kCm_Fov         = 0x3C;
static const DWORD kCm_FovTarget   = 0x40;
static const DWORD kCm_Speed       = 0x44;
static const DWORD kCm_Collision   = 0x48;
static const DWORD kCm_Height      = 0x4C;

static const DWORD kOff_Type   = 0x14;       // TYPEID: 3 unit, 4 player
static const DWORD kOff_PosX   = 0x798;
static const DWORD kOff_Height = 0x854;      // what Update multiplies by 0.75
static const DWORD kTypeMaskUnit = 0x08;

static const float kMinDistance = 1.0f;
static const float kMaxDistance = 75.0f;     // Update's own clamp (0x00A0FF58)

typedef void        (__cdecl* RegisterFunction_t)(const char*, void*);
typedef void        (__cdecl* LuaPushNumber_t)(void*, double);
typedef double      (__cdecl* LuaToNumber_t)(void*, int);
typedef void        (__cdecl* LuaPushString_t)(void*, const char*);
typedef void        (__cdecl* LuaPushNil_t)(void*);
typedef const char* (__cdecl* LuaToLString_t)(void*, int, size_t*);
typedef void* (__cdecl* ObjectPtr_t)(DWORD guidLo, DWORD guidHi, DWORD typeMask, const char* file, int line);
static const RegisterFunction_t pRegisterFunction = (RegisterFunction_t)0x00817F90;
static const LuaPushNumber_t    pLuaPushNumber    = (LuaPushNumber_t)   0x0084E2A0;
static const LuaToNumber_t      pLuaToNumber      = (LuaToNumber_t)     0x0084E030;
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

// ------------------------------------------------------------ the hook
//
// IsCommentator(ecx = player): while a follow is on, "yes" for the local player
// (its descriptors' guid is the active player's); everyone else, and everyone
// while no follow is on, gets the client's own answer.
__declspec(naked) static void IsCommentatorHook()
{
    __asm {
        cmp  dword ptr [g_on], 0
        je   original
        push ecx
        mov  eax, kGetActiveGuid
        call eax                        // edx:eax = active player guid
        pop  ecx
        push esi
        mov  esi, dword ptr [ecx + 8]   // descriptors: OBJECT_FIELD_GUID first
        test esi, esi
        je   notActive
        cmp  eax, dword ptr [esi]
        jne  notActive
        cmp  edx, dword ptr [esi + 4]
        jne  notActive
        pop  esi
        mov  eax, 1
        ret
    notActive:
        pop  esi
    original:
        mov  eax, dword ptr ds:[0x00BD088C]   // the 5 bytes the jmp replaced
        jmp  dword ptr [g_isCommentatorResume]
    }
}

// ------------------------------------------------------------ the follow
static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Where Update aims for this unit: its position, 0.75 of its height up (the
// commentator's own height offset is set to 0).
static bool FollowPoint(DWORD lo, DWORD hi, float out[3])
{
    BYTE* const o = (BYTE*)((ObjectPtr_t)kObjectPtr)(lo, hi, kTypeMaskUnit, "spectate.cpp", __LINE__);
    if (!o)
        return false;
    DWORD const type = *(DWORD*)(o + kOff_Type);
    if (type != 3 && type != 4)
        return false;
    float const* pos = (float const*)(o + kOff_PosX);
    float height = *(float*)(o + kOff_Height);
    if (!(height > 0.0f && height < 30.0f))
        height = 2.0f;
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2] + height * 0.75f;
    return true;
}

static bool StartFollow(DWORD lo, DWORD hi, float distance)
{
    BYTE* const wf = kWorldFramePtr;
    BYTE* const cam = wf ? *(BYTE**)(wf + kOff_Camera) : NULL;
    if (!cam)
        return false;

    BYTE* const c = (BYTE*)kCommentator;
    float target[3];
    bool const present = FollowPoint(lo, hi, target);

    if (!g_on)
    {
        // A fresh follow starts from what the screen shows: the eye stays where
        // the camera is and turns to look at the unit, so nothing jumps.
        float const* camPos = (float const*)(cam + kCam_Pos);
        float const* m = (float const*)(cam + kCam_Matrix);
        float yaw = atan2f(m[1], m[0]);
        float pitch = asinf(Clamp(-m[2], -1.0f, 1.0f));
        float dist = distance > 0.0f ? distance : g_defaultDistance;

        if (present)
        {
            float const dx = target[0] - camPos[0], dy = target[1] - camPos[1], dz = target[2] - camPos[2];
            float const len = sqrtf(dx * dx + dy * dy + dz * dz);
            if (len > 1.5f)
            {
                yaw = atan2f(dy, dx);
                pitch = asinf(Clamp(-dz / len, -1.0f, 1.0f));
                if (distance <= 0.0f)
                    dist = len;
            }
        }

        *(float*)(c + kCm_Yaw) = yaw;
        *(float*)(c + kCm_Pitch) = Clamp(pitch, -1.45f, 1.45f);
        *(float*)(c + kCm_TurnRate) = 0.0f;
        memcpy(c + kCm_Pos, camPos, 12);
        *(float*)(c + kCm_Distance) = Clamp(dist, kMinDistance, kMaxDistance);
        float const fov = *(float*)(cam + kCam_Fov);
        *(float*)(c + kCm_Fov) = fov;
        *(float*)(c + kCm_FovTarget) = fov;
    }
    else if (distance > 0.0f)
        *(float*)(c + kCm_Distance) = Clamp(distance, kMinDistance, kMaxDistance);

    memset(c + kCm_Move, 0, 12);
    *(DWORD*)(c + kCm_LookAt) = 0;
    *(DWORD*)(c + kCm_LookAt + 4) = 0;
    *(DWORD*)(c + kCm_Follow) = lo;
    *(DWORD*)(c + kCm_Follow + 4) = hi;
    *(float*)(c + kCm_Speed) = g_zoomSpeed;
    *(DWORD*)(c + kCm_Collision) = 1;
    *(float*)(c + kCm_Height) = 0.0f;

    g_followLo = lo;
    g_followHi = hi;
    InterlockedExchange(&g_on, 1);
    return true;
}

static void StopFollow()
{
    InterlockedExchange(&g_on, 0);
    BYTE* const c = (BYTE*)kCommentator;
    *(DWORD*)(c + kCm_Follow) = 0;
    *(DWORD*)(c + kCm_Follow + 4) = 0;
    memset(c + kCm_Move, 0, 12);
    g_followLo = g_followHi = 0;
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

static int PushFollowed(void* L)
{
    if (!g_on)
    {
        pLuaPushNil(L);
        return 1;
    }
    char guid[24];
    _snprintf_s(guid, sizeof(guid), _TRUNCATE, "0x%08lX%08lX", g_followHi, g_followLo);   // UnitGUID()'s form
    pLuaPushString(L, guid);
    float point[3];
    pLuaPushNumber(L, FollowPoint(g_followLo, g_followHi, point) ? 1.0 : 0.0);
    return 2;
}

static int Spectate(void* L)
{
    const char* arg = pLuaToLString(L, 1, NULL);

    if (arg && arg[0] == '?' && !arg[1])
        return PushFollowed(L);

    if (arg && !_stricmp(arg, "zoom"))
    {
        if (g_on)
        {
            float* const dist = (float*)((BYTE*)kCommentator + kCm_Distance);
            *dist = Clamp(*dist + float(pLuaToNumber(L, 2)), kMinDistance, kMaxDistance);
        }
        return PushFollowed(L);
    }

    DWORD lo = 0, hi = 0;
    if (!ParseGuid(arg, lo, hi))
    {
        // "off", nil, anything that is not a guid
        if (g_on)
            StopFollow();
        return PushFollowed(L);
    }

    if (!StartFollow(lo, hi, float(pLuaToNumber(L, 2))))
        StopFollow();
    return PushFollowed(L);
}

static int __cdecl Lua_Spectate(void* L)
{
    int pushed = 0;
    __try {
        pushed = Spectate(L);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_on, 0);
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
    if (g_on)
        StopFollow();
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
    int const distance = GetPrivateProfileIntA("Spectate", "Distance", 12, ini);
    if (distance >= 2 && distance <= 60)
        g_defaultDistance = float(distance);
    int const speed = GetPrivateProfileIntA("Spectate", "ZoomSpeed", 15, ini);
    if (speed >= 1 && speed <= 100)
        g_zoomSpeed = float(speed);
}

void Spectate_Install()
{
    if (!g_enabled)
        return;
    // Everything verified before anything is written.
    if (!SigOk(kIsCommentator, kIsCommentatorSig, sizeof(kIsCommentatorSig), "IsCommentator") ||
        !SigOk(kFollowWriteSite, kFollowWriteSig, sizeof(kFollowWriteSig), "commentator follow guid") ||
        !SigOk(kUpdate, kUpdateSig, sizeof(kUpdateSig), "CGCommentator::Update") ||
        !SigOk(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActivePlayerGuid") ||
        !SigOk(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr") ||
        !SigOk(kLuaThunk, kPadSig, sizeof(kPadSig), "spectate thunk padding"))
        return;
    if (!JumpTo(kLuaThunk, (void*)&Lua_Spectate) || !JumpTo(kIsCommentator, (void*)&IsCommentatorHook)) {
        Log("not applied: VirtualProtect failed");
        return;
    }
    g_ready = 1;
    Log("installed (distance %.0f yd, W/S %.0f yd/s)", g_defaultDistance, g_zoomSpeed);
}
