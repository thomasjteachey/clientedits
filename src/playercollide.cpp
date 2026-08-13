// playercollide.cpp - buff-gated client-side player-vs-player collision.
// Target: World of Warcraft 3.3.5a build 12340 (Wow.exe, imagebase 0x400000, x86)
//
// Player movement in 3.3.5 is client-authoritative: each client runs collision
// every frame and reports the result in MSG_MOVE_* heartbeats; units are never in
// the collision set. So if each client refuses to let its own player overlap a
// "solid" (buffed) player, that IS the feature - it propagates to everyone through
// the normal heartbeats, symmetric as long as both run this DLL.
//
// We do NOT touch the shared CWorld collision (camera and line-of-sight use it).
// Instead we detour the per-unit physics tick, and for the ACTIVE player only, push
// it out of any overlapping buffed player each frame (a one-frame push-out: the tick
// then integrates WASD on top, and next frame we eject any new penetration - stable
// at frame rate, and the preserved tangential motion slides you around bodies).
//
// Every address below was located in this exact binary with tools\clientre and is
// re-checked against a byte signature at load; on any mismatch the feature disables
// itself and logs, so a different Wow.exe fails safe instead of crashing.
//
// Full reverse-engineering writeup: tools\clientre\PLAYER_COLLISION_DESIGN.md

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <intrin.h>                                  // _ReturnAddress

#include "playercollide.h"

// ---------------------------------------------------------------- addresses
//
// Verified in clients\centurion\Wow.exe (3.3.5a 12340). Each Sig is the leading
// bytes we require to still be present before we trust the address.

// The per-frame local-player movement update: __thiscall(this, arg) ret 4. Its
// caller (0x4F8660) resolves the active player each frame (GetActivePlayerGuid ->
// ObjectPtr) and calls this unconditionally, so it runs EVERY frame for the
// controlled player whether moving or idle - it is the single choke point for our
// position on both. We post-hook it (run the original so the frame's movement is
// committed, then eject). Earlier attempts hooked the passive tick / object-loop
// update, which only ran the player when idle - hence "blocks standing still,
// runs through while moving." (Hooking two paths at once also split the frame dt
// between them, starving the eject - so exactly one hook here is correct.)
static const DWORD kMoveSite     = 0x006DEB30;
static const BYTE  kMoveSig[]    = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08 };
static const DWORD kMoveResume   = 0x006DEB36; // site + sizeof(sig)

// --- native swept-collision injection (the real wall) ---------------------
//
// The client already has everything we need: a swept-collision primitive whose
// result drives its own collide-and-slide. Rather than fix the position after the
// mover has produced an illegal one (whack-a-mole against several position copies
// the client re-derives from), we make a buffed player a genuine obstacle so the
// illegal position is never produced. This is the same path that makes a
// collidable gameobject stop you mid-run.
//
// NOTE: an earlier attempt injected into 0x4F9930 (a "sweep" reached via 0x4FA040).
// Measurement disproved it - that pair only ever runs a long world/picking query,
// never the per-frame player step - so it was removed. The real movement clip is
// 0x762E00, hooked below.


static const DWORD kGetActiveGuid = 0x004D3790; // cdecl () -> u64 (edx:eax)
static const BYTE  kGetActiveSig[] = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };

static const DWORD kObjectPtr = 0x004D4DB0;     // cdecl (guidLo, guidHi, typeMask) -> obj*
static const BYTE  kObjectPtrSig[] = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C };

static const DWORD kEnumVisible = 0x004D4B30;   // cdecl (cb, arg)
static const BYTE  kEnumSig[] = { 0x55, 0x8B, 0xEC, 0xA1, 0xBC, 0x39, 0xD4, 0x00 };

static const DWORD kGetAuraCount = 0x004F8850;  // thiscall (unit) -> int
static const BYTE  kAuraCountSig[] = { 0x8B, 0x81, 0xD0, 0x0D, 0x00, 0x00, 0x83, 0xF8, 0xFF };

static const DWORD kGetAuraInfo = 0x004F8870;   // thiscall (unit, index) -> spellId
static const BYTE  kAuraInfoSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xB9, 0xD0, 0x0D, 0x00, 0x00, 0xFF };

// ---- stand-on-players (experimental, install-gated by StandOnPlayers) -------
//
// CGUnit_C ground query: __thiscall(this), no stack args, returns the unit's
// HEIGHT ABOVE GROUND as a float in st0 (it casts a downward ray, mask 0x100111,
// and returns posZ - hitZ). Its caller in the physics tick compares the result
// against 1.5 to decide whether the unit counts as grounded.
//
// The idea: when a blocking player's head is between our feet and the terrain,
// report THAT as the ground. The client's own gravity, landing and standing
// logic then does the rest - the same "feed the client different geometry rather
// than reimplement its physics" approach that made the movement clip work.
//
// Honest caveat: this function returns a distance that feeds a grounded-state
// decision. It may not be what actually snaps Z. If so, the log will show the
// override taking effect with no visible standing, and the real snap point is
// elsewhere (most likely inside the mover 0x7317A0). Measure, do not assume.
static const DWORD kGroundSite   = 0x00714B60;
static const BYTE  kGroundSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28 };
static const DWORD kGroundResume = 0x00714B66;

// CGUnit_C::GetReaction - thiscall(this, otherUnit) -> reaction rank. Returns 4
// for self. The client's own Script_UnitIsEnemy (0x60D330) calls this and treats
// `reaction <= 1` as hostile, so we use the same test rather than inventing one.
static const DWORD kGetReaction = 0x007251C0;
static const BYTE  kGetReactionSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x57 };

// GetCurrentSpeed - thiscall on the CMovement object (unit+0x788), one flag arg.
// Returns the unit's current move speed in yards/sec, or 0 when it isn't moving
// (the `test [ecx+0x44], 0xC0000F` / fldz path). Speed includes mount/sprint/buffs.
static const DWORD kGetSpeed = 0x00987570;
static const BYTE  kGetSpeedSig[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x51, 0x44, 0xF7, 0xC2 };

// CGObject_C / CGUnit_C field offsets (12340), all verified.
static const DWORD kOff_GuidLow = 0x30;
static const DWORD kOff_GuidHigh = 0x34;
static const DWORD kOff_PosX = 0x798;   // float; y=+0x79C, z=+0x7A0, facing=+0x7A4
static const DWORD kOff_PosY = 0x79C;
static const DWORD kOff_PosZ = 0x7A0;
static const DWORD kOff_Facing = 0x7A4; // float radians; 0 = +x (east), CCW
static const DWORD kOff_Movement = 0x788; // embedded CMovement (GetCurrentSpeed's this)

// Object type mask (TYPEMASK). Unit 0x08 | Player 0x10 -> accept both.
static const DWORD kTypeMaskUnitOrPlayer = 0x18;

// ------------------------------------------------------------- client ABI
typedef unsigned __int64 (__cdecl *GetActiveGuid_t)();
typedef void* (__cdecl *ObjectPtr_t)(DWORD guidLo, DWORD guidHi, DWORD typeMask);
typedef int   (__cdecl *EnumCb_t)(DWORD guidLo, DWORD guidHi, void* arg);
typedef int   (__cdecl *EnumVisible_t)(EnumCb_t cb, void* arg);
typedef int   (__thiscall *GetAuraCount_t)(void* unit);
typedef int   (__thiscall *GetAuraInfo_t)(void* unit, int index);
typedef float (__thiscall *GetSpeed_t)(void* cmovement, int flag);
typedef int   (__thiscall *GetReaction_t)(void* unit, void* other);

static GetActiveGuid_t pGetActiveGuid;
static ObjectPtr_t     pObjectPtr;
static EnumVisible_t   pEnumVisible;
static GetAuraCount_t  pGetAuraCount;
static GetAuraInfo_t   pGetAuraInfo;
static GetSpeed_t      pGetSpeed;
static GetReaction_t   pGetReaction;

// ------------------------------------------------------------- settings/state
static int    g_enabled = 1;          // shipped ON: inert until someone carries an aura
static int    g_spellId = 0;          // legacy single-spell mode; off
static float  g_radius = 0.75f;       // per-player collision radius (yards)
static int    g_bothNeed = 1;         // 1 = both must have the buff; 0 = the other only
static float  g_pushMargin = 6.0f;    // legacy/inert (kept for the selftest export)
static float  g_separateSpeed = 3.0f; // yards/sec, ONLY for overlap we did not cause
static float  g_slideFriction = 0.5f; // 0 = stop dead on contact, 1 = frictionless
static float  g_contactBand = 0.0f;   // yards past the radius still counted as touching
// Vertical extent of the body. Without this the octagon is an infinite prism: you
// would collide with someone on the floor above you or flying overhead. Collision
// applies only while |dz| < this. A player is ~2 yд tall and jumps ~1.6 yд, so the
// 2.0 default means you cannot jump over someone; set it below ~1.5 to allow that.
// 1.4 is deliberately BELOW a player's ~1.6 yd jump apex, so you can jump over
// someone. Raise above ~1.6 to make bodies unjumpable.
static float  g_height = 1.4f;
static int    g_native = 1;           // 1 = inject into the client's own sweep (real
                                      // wall + native sliding); 0 = legacy position
                                      // rewriting, kept only for comparison
static int    g_debug = 0;            // 1 = sampled diagnostics, 2 = EVERY frame
static unsigned g_frame = 0;          // counts hook invocations (exposes skipped frames)
// probe counters (declared early so the reporter can see them)
static unsigned g_cA = 0, g_cB = 0, g_cC = 0;
static unsigned g_cCollide = 0, g_cCollideMine = 0;
static unsigned g_cClip = 0, g_cClipMine = 0, g_cClipCut = 0;
static float  g_lastApproach = 0.0f;  // diagnostics: our own approach last correction
static float  g_prevX = 0, g_prevY = 0; // final position we wrote last contact frame
static int    g_prevValid = 0;
static float  g_postX = 0, g_postY = 0; // ditto, but only while actually in contact
static int    g_postValid = 0;          // (drives the pre-move re-assert)
static int    g_showErrors = 0;
static char   g_dir[MAX_PATH];
static int    g_installed = 0;
static int    g_sawActiveTick = 0;    // logs once when the active player first ticks

// Frame delta from the game's own clock, so the eject speed cap is
// framerate-independent. Sampled once per active-player tick.
static double    g_qpcFreq = 0.0;
static long long g_lastQpc = 0;
static float NextFrameDt()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_qpcFreq <= 0.0) {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        g_qpcFreq = (double)f.QuadPart;
    }
    float dt = 0.0f;
    if (g_lastQpc != 0) {
        dt = (float)((double)(now.QuadPart - g_lastQpc) / g_qpcFreq);
        if (dt < 0.0f)  dt = 0.0f;
        if (dt > 0.05f) dt = 0.05f;   // clamp a hitch so it can't fling you
    }
    g_lastQpc = now.QuadPart;
    return dt;
}

static void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sAnimSpeedFix.log", g_dir);
    // Two test clients share one log file, so allow write sharing (an exclusive
    // open silently DROPS the other client's line) and tag every line with the pid
    // so the two interleaved streams can be told apart.
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 10; ++attempt) {
        h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
            break;
        Sleep(1);
    }
    if (h == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME t; GetLocalTime(&t);
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    int n = _snprintf_s(msg, sizeof(msg), _TRUNCATE, "[%02d:%02d:%02d pid%lu] ",
                        t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    int m = _vsnprintf_s(msg + n, sizeof(msg) - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0 && m >= 0) {
        int len = n + m;
        if (len < (int)sizeof(msg) - 2) { msg[len] = '\r'; msg[len+1] = '\n'; len += 2; }
        DWORD w = 0; WriteFile(h, msg, (DWORD)len, &w, NULL);
    }
    CloseHandle(h);
}

// ------------------------------------------------------------- buff gate
static bool UnitHasAura(void* unit, int spellId)
{
    if (!unit || spellId == 0)
        return false;
    // The aura count can transiently read as a sentinel/garbage value (the client
    // uses -1 in this struct family) - clamping to a sane scan length rather than
    // bailing keeps the gate from flickering off for a frame, which would open a
    // one-frame hole in the wall and let a player slip through.
    int n = pGetAuraCount(unit);
    if (n < 0 || n > 256)
        n = 64;                          // client's visible aura cap; scan that far
    for (int i = 0; i < n; ++i) {
        if (pGetAuraInfo(unit, i) == spellId)
            return true;
    }
    return false;
}

// ---------------------------------------------------- per-frame collision work
//
// Collected during one EnumVisibleObjects pass, then resolved against the active
// player. Small fixed cap keeps the callback allocation-free and bounded.
struct Neighbor { float x, y; };
static const int kMaxNeighbors = 64;

struct CollectCtx {
    void* self;          // active player object (skip it)
    float sx, sy;        // active player position
    float range2;        // (radius + radius + margin)^2 broad-phase cutoff
    Neighbor list[kMaxNeighbors];
    int count;           // players in range that passed the aura gate (=> walls)
    int nInRange;        // players in range BEFORE the aura gate (diagnostics)
};

// Recently-seen blockers, kept for a grace period so a one-frame dropout in the
// visible list (or a flickering aura read) cannot open a hole in the wall.
struct Sticky { DWORD guidLo, guidHi, lastSeen; float x, y; };
static const int kMaxSticky = 8;
static const DWORD kStickyMs = 400;
static Sticky g_sticky[kMaxSticky];
static int    g_stickyCount = 0;

static void StickyRemember(DWORD guidLo, DWORD guidHi, float x, float y)
{
    DWORD now = GetTickCount();
    for (int i = 0; i < g_stickyCount; ++i) {
        if (g_sticky[i].guidLo == guidLo && g_sticky[i].guidHi == guidHi) {
            g_sticky[i].lastSeen = now;
            g_sticky[i].x = x; g_sticky[i].y = y;
            return;
        }
    }
    if (g_stickyCount < kMaxSticky) {
        Sticky* s = &g_sticky[g_stickyCount++];
        s->guidLo = guidLo; s->guidHi = guidHi;
        s->lastSeen = now; s->x = x; s->y = y;
    }
}

static void StickyExpire()
{
    DWORD now = GetTickCount();
    int w = 0;
    for (int i = 0; i < g_stickyCount; ++i) {
        if (now - g_sticky[i].lastSeen <= kStickyMs)
            g_sticky[w++] = g_sticky[i];
    }
    g_stickyCount = w;
}

static int __cdecl CollectCb(DWORD guidLo, DWORD guidHi, void* arg)
{
    CollectCtx* c = (CollectCtx*)arg;
    if (c->count >= kMaxNeighbors)
        return 0;                                    // full: stop enumerating
    void* o = pObjectPtr(guidLo, guidHi, kTypeMaskUnitOrPlayer);
    if (!o || o == c->self)
        return 1;
    float ox = *(float*)((BYTE*)o + kOff_PosX);
    float oy = *(float*)((BYTE*)o + kOff_PosY);
    float dx = ox - c->sx, dy = oy - c->sy;
    if (dx*dx + dy*dy > c->range2)                   // broad-phase reject
        return 1;
    c->nInRange++;                                   // a unit/player is in contact range
    if (!UnitHasAura(o, g_spellId))                  // gate: the other must be buffed
        return 1;
    c->list[c->count].x = ox;
    c->list[c->count].y = oy;
    c->count++;
    StickyRemember(guidLo, guidHi, ox, oy);          // survive a one-frame dropout
    return 1;
}

// Raw, uncapped ejection vector that separates a point at (sx,sy) from the given
// bodies (others: x,y interleaved). Resolves sequentially, matching the original.
// When a body is ~coincident the radial normal is meaningless and noisy, so we
// eject backward along `facing` instead - a stable direction (you back out the
// way you ran in) rather than a random fling. Pure, so the selftest can drive it.
static void ComputeEjection(float sx, float sy, const float* others, int n,
                            float facing, float minDist, float* exOut, float* eyOut)
{
    float nx = sx, ny = sy;
    const float minDist2 = minDist * minDist;
    for (int i = 0; i < n; ++i) {
        float dx = nx - others[i*2], dy = ny - others[i*2+1];
        float d2 = dx*dx + dy*dy;
        if (d2 >= minDist2)
            continue;                                // not overlapping
        float ux, uy, push;
        if (d2 > 0.0025f) {                          // > 0.05 yd apart: use the real normal
            float d = sqrtf(d2);
            ux = dx / d; uy = dy / d;
            push = minDist - d;
        } else {                                     // ~coincident: back out along -facing
            ux = -cosf(facing); uy = -sinf(facing);
            push = minDist;
        }
        nx += ux * push;
        ny += uy * push;
    }
    *exOut = nx - sx;
    *eyOut = ny - sy;
}

// Clamp an ejection vector's length to maxStep, in place. This is what turns a
// "snap fully out this frame" (which teleports you across a deep overlap) into a
// smooth push that resolves over a few frames.
static void CapStep(float* ex, float* ey, float maxStep)
{
    float len2 = (*ex)*(*ex) + (*ey)*(*ey);
    if (len2 > maxStep*maxStep && len2 > 1e-12f) {
        float s = maxStep / sqrtf(len2);
        *ex *= s; *ey *= s;
    }
}

static bool Readable(const void* p, size_t n);   // defined in the install section

// Correcting only [unit+0x798] holds when idle but is IGNORED while self-moving:
// the input mover keeps its OWN authoritative copy of the position and re-derives
// +0x798 from it every frame, so our write is discarded (proven by the debug log:
// stationary drift=0, moving drift~=our eject with the player marching to centre).
//
// Rather than hardcode that copy's offset, find it: a float triple in the unit
// struct that holds the live position is a copy of it, so rewrite each one's x/y.
// World coordinates are large arbitrary values, so an unrelated triple landing
// near ours in ALL THREE components is not a practical concern. Z is left as-is.
//
// The copy is NOT bit-identical to +0x798 every frame - it can lag by up to a
// frame of movement. The original 0.02 tolerance therefore found it only
// intermittently, and a frame that missed it was a frame the correction was
// discarded: the debug log showed copies=1 frames penetrating without limit
// (pen 0.21 -> 0.93 -> 1.88 -> 2.68) while copies=2 frames held at pen<0.25.
// So: a movement-sized tolerance, plus offsets are REMEMBERED once learned and
// corrected every frame thereafter even when momentarily out of sync.
static DWORD g_posOff[12];
static int   g_posOffHits[12];
static int   g_posOffCount = 0;

// Some position copies do not live in the unit at all - they sit in objects the
// unit points to ([unit+0xB8]+0x6C and [unit+0xD8]+0x10 on 12340). Missing those
// is what let the mover keep its own idea of where we are and walk through the
// wall. Discovered by scanning rather than hardcoded, then corrected every frame.
struct PtrSlot { DWORD ptrOff, inner; };
static const int kMaxPtrSlots = 24;
static PtrSlot g_ptrSlot[kMaxPtrSlots];
static int     g_ptrSlotCount = 0;
static DWORD   g_lastPtrScan = 0;

static void ScanPointerSlots(void* unit, float x, float y, float z)
{
    g_ptrSlotCount = 0;
    BYTE* base = (BYTE*)unit;
    if (!Readable(base, 0x800))
        return;
    // Tolerance is generous and the inner range wide: at any given instant some
    // copies are a frame out of sync, and a scan that misses one leaves exactly
    // the hole the mover walks through.
    for (DWORD off = 0; off + 4 <= 0x800 && g_ptrSlotCount < kMaxPtrSlots; off += 4) {
        DWORD p = *(DWORD*)(base + off);
        if (p < 0x10000 || (p & 3))
            continue;
        // Skip pointers back into the unit: those just lead to the copies the
        // direct scan already covers (every such hit summed to +0x798).
        if (p >= (DWORD)base && p < (DWORD)base + 0x2000)
            continue;
        if (!Readable((void*)p, 0x400))
            continue;
        for (DWORD o2 = 0; o2 + 12 <= 0x400 && g_ptrSlotCount < kMaxPtrSlots; o2 += 4) {
            float* f = (float*)(p + o2);
            if (fabsf(f[0]-x) < 1.5f && fabsf(f[1]-y) < 1.5f && fabsf(f[2]-z) < 1.5f) {
                g_ptrSlot[g_ptrSlotCount].ptrOff = off;
                g_ptrSlot[g_ptrSlotCount].inner  = o2;
                ++g_ptrSlotCount;
            }
        }
    }
}

static void LearnPosOffset(DWORD off)
{
    for (int i = 0; i < g_posOffCount; ++i) {
        if (g_posOff[i] == off) {
            if (g_posOffHits[i] < 1000) ++g_posOffHits[i];
            return;
        }
    }
    if (g_posOffCount < 12) {
        g_posOff[g_posOffCount] = off;
        g_posOffHits[g_posOffCount] = 1;
        ++g_posOffCount;
    }
}

// Diagnostic: report EVERY float triple that currently holds our position, both
// directly in the unit and one pointer level out. Used to find the copy the mover
// actually reads when our correction stops taking effect. Debug only, throttled.
static void DeepScanReport(void* unit, float x, float y, float z)
{
    BYTE* base = (BYTE*)unit;
    char buf[900];
    int n = 0;
    buf[0] = 0;

    const DWORD kDirect = 0x2000;
    if (Readable(base, kDirect)) {
        for (DWORD off = 0; off + 12 <= kDirect && n < 700; off += 4) {
            float* f = (float*)(base + off);
            if (fabsf(f[0]-x) < 1.0f && fabsf(f[1]-y) < 1.0f && fabsf(f[2]-z) < 1.0f)
                n += _snprintf_s(buf+n, sizeof(buf)-n, _TRUNCATE, " +0x%X", off);
        }
    }
    // one pointer level: fields in the unit that point at another struct holding it
    if (Readable(base, 0x1000)) {
        for (DWORD off = 0; off + 4 <= 0x1000 && n < 700; off += 4) {
            DWORD p = *(DWORD*)(base + off);
            if (p < 0x10000 || (p & 3) || !Readable((void*)p, 0x200))
                continue;
            for (DWORD o2 = 0; o2 + 12 <= 0x200 && n < 700; o2 += 4) {
                float* f = (float*)(p + o2);
                if (fabsf(f[0]-x) < 1.0f && fabsf(f[1]-y) < 1.0f && fabsf(f[2]-z) < 1.0f)
                    n += _snprintf_s(buf+n, sizeof(buf)-n, _TRUNCATE,
                                     " [+0x%X]+0x%X", off, o2);
            }
        }
    }
    char known[128];
    int k = _snprintf_s(known, sizeof(known), _TRUNCATE, "writing:");
    for (int i = 0; i < g_posOffCount && k > 0 && k < 100; ++i)
        k += _snprintf_s(known+k, sizeof(known)-k, _TRUNCATE, " +0x%X", g_posOff[i]);
    Log("deepscan holds:%s  (%s)", buf[0] ? buf : " none", known);
}

// Returns the number of copies corrected (for the debug log).
static int CorrectPositionCopies(void* unit, float oldX, float oldY, float oldZ,
                                 float newX, float newY, const CollectCtx* c)
{
    // The unit object comfortably spans past +0xF60; scan a bounded window and
    // bail if it isn't one readable region (guards a wrong/smaller object).
    const DWORD lo = 0x780, hi = 0x1000;
    BYTE* base = (BYTE*)unit;
    if (!Readable(base + lo, hi - lo))
        return 0;

    const float kMatch = 1.0f;   // covers a frame of movement at any speed
    const float kKnown = 5.0f;   // looser bound for an already-learned offset
    int hits = 0;

    for (DWORD off = lo; off + 12 <= hi; off += 4) {
        float* f = (float*)(base + off);
        float dx = f[0] - oldX, dy = f[1] - oldY, dz = f[2] - oldZ;

        bool fresh = fabsf(dx) < kMatch && fabsf(dy) < kMatch && fabsf(dz) < kMatch;
        bool known = false;
        if (!fresh && fabsf(dx) < kKnown && fabsf(dy) < kKnown && fabsf(dz) < kKnown) {
            for (int i = 0; i < g_posOffCount; ++i) {
                if (g_posOff[i] == off && g_posOffHits[i] >= 3) { known = true; break; }
            }
        }
        if (!fresh && !known)
            continue;

        // Never rewrite a stored copy of somebody ELSE's position: if this triple
        // sits closer to a neighbour than to us, it is theirs, not ours. Matters
        // during a deep overlap, where a neighbour is within the match radius.
        float ours = dx*dx + dy*dy;
        bool theirs = false;
        for (int i = 0; i < c->count; ++i) {
            float nx = f[0] - c->list[i].x, ny = f[1] - c->list[i].y;
            if (nx*nx + ny*ny < ours) { theirs = true; break; }
        }
        if (theirs)
            continue;

        f[0] = newX;
        f[1] = newY;                                 // f[2] (z) left as-is
        if (fresh)
            LearnPosOffset(off);
        ++hits;
    }

    // ...and the copies that live in other objects the unit points at.
    for (int i = 0; i < g_ptrSlotCount; ++i) {
        DWORD p = *(DWORD*)(base + g_ptrSlot[i].ptrOff);
        if (p < 0x10000 || !Readable((void*)(p + g_ptrSlot[i].inner), 12))
            continue;
        float* f = (float*)(p + g_ptrSlot[i].inner);
        if (fabsf(f[0]-oldX) < kKnown && fabsf(f[1]-oldY) < kKnown &&
            fabsf(f[2]-oldZ) < kKnown) {
            f[0] = newX;
            f[1] = newY;
            ++hits;
        }
    }
    return hits;
}

// Runs once for the active player each frame. Reads the position the mover just
// committed and projects it back onto the contact surface of any buffed player it
// overlaps - a HARD WALL: no per-frame cap, so the player is placed exactly on the
// surface every frame (zero visible penetration, no rubber-band, and no creeping
// through). A capped push-out was what made it spongy and let you force through.
static void ResolveActive(void* unit)
{
    float dt = NextFrameDt();                        // once per frame, before any exit
    unsigned frame = ++g_frame;                      // every call, to expose gaps

    float* px = (float*)((BYTE*)unit + kOff_PosX);
    float* py = (float*)((BYTE*)unit + kOff_PosY);
    float sx = *px, sy = *py;

    int selfAura = UnitHasAura(unit, g_spellId) ? 1 : 0;

    CollectCtx c;
    c.self = unit;
    c.sx = sx;
    c.sy = sy;
    const float reach = 2.0f * g_radius;             // both radii = contact distance
    c.range2 = (reach + 1.0f) * (reach + 1.0f);      // + margin for the broad phase
    c.count = 0;
    c.nInRange = 0;
    pEnumVisible(CollectCb, &c);

    StickyExpire();

    // A blocker can vanish from the visible list (or its aura read can flicker) for
    // a frame; trusting a single frame's enumeration puts a one-frame hole in the
    // wall, which is exactly enough to walk through. So remember recent blockers by
    // GUID and keep applying them for a short grace period, refreshing their
    // position from the object manager - dropping one only when it is genuinely
    // gone or out of range.
    for (int i = 0; i < g_stickyCount; ++i) {
        Sticky* s = &g_sticky[i];
        bool present = false;
        for (int j = 0; j < c.count; ++j) {          // already collected this frame?
            if (fabsf(c.list[j].x - s->x) < 0.001f && fabsf(c.list[j].y - s->y) < 0.001f) {
                present = true;
                break;
            }
        }
        if (present)
            continue;
        void* o = pObjectPtr(s->guidLo, s->guidHi, kTypeMaskUnitOrPlayer);
        if (!o)
            continue;                                // gone entirely; ages out below
        float ox = *(float*)((BYTE*)o + kOff_PosX);
        float oy = *(float*)((BYTE*)o + kOff_PosY);
        float dx = ox - sx, dy = oy - sy;
        if (dx*dx + dy*dy > c.range2)
            continue;                                // genuinely out of range
        if (c.count < kMaxNeighbors) {               // re-add: still blocking us
            c.list[c.count].x = ox;
            c.list[c.count].y = oy;
            c.count++;
        }
    }

    // Distance to the nearest blocker, for diagnostics.
    float nearest = 1e9f;
    for (int i = 0; i < c.count; ++i) {
        float dx = sx - c.list[i].x, dy = sy - c.list[i].y;
        float d = sqrtf(dx*dx + dy*dy);
        if (d < nearest) nearest = d;
    }

    // Debug=2 logs EVERY frame we are anywhere near someone, including frames where
    // we do nothing and why. The frame counter exposes gaps: if `f` jumps while we
    // are penetrating, the hook is not running every frame (a different problem than
    // the correction not sticking).
    if (g_debug >= 2 && (c.nInRange > 0 || c.count > 0)) {
        Log("f=%u dt=%.4f inRange=%d walls=%d self=%d near=%.2f pen=%.2f pos=(%.2f,%.2f)",
            frame, dt, c.nInRange, c.count, selfAura,
            (c.count ? nearest : -1.0f), (c.count ? reach - nearest : 0.0f), sx, sy);
    }

    if (g_bothNeed && !selfAura) {
        g_prevX = sx; g_prevY = sy; g_prevValid = 1; g_postValid = 0;
        return;                                      // both-need mode: we must be buffed too
    }
    if (c.count == 0) {
        g_prevX = sx; g_prevY = sy; g_prevValid = 1; g_postValid = 0;
        return;
    }

    float facing = *(float*)((BYTE*)unit + kOff_Facing);

    // Project to the surface, iterated so being pushed off one body into another
    // settles (corners). No cap: exact surface placement every frame is the wall.
    float cx = sx, cy = sy;
    for (int iter = 0; iter < 4; ++iter) {
        float ex, ey;
        ComputeEjection(cx, cy, (const float*)c.list, c.count, facing, reach, &ex, &ey);
        if (ex == 0.0f && ey == 0.0f)
            break;
        cx += ex; cy += ey;
    }
    if (cx == sx && cy == sy) {                      // outside everything
        g_prevX = sx; g_prevY = sy; g_prevValid = 1; g_postValid = 0;
        return;
    }

    // Budget the correction. Two very different situations produce an overlap and
    // they must NOT be corrected the same way:
    //
    //   I moved into them   -> undo exactly my own approach this frame. That is a
    //                          hard wall: my step is cancelled the moment I take
    //                          it, so I never visibly penetrate and never zip.
    //   THEY moved into me   -> (or their network position snapped, which is how
    //                          the log caught us 1.8 yd deep) my own approach is
    //                          ~0, so we only separate at SeparateSpeed. Smooth,
    //                          no yank - the previous code slammed the whole
    //                          overlap out at once, which was the zip.
    float mvx = cx - sx, mvy = cy - sy;
    const float mv = sqrtf(mvx*mvx + mvy*mvy);       // wanted correction (pre-budget)
    if (mv > 1e-6f) {
        float ux = mvx / mv, uy = mvy / mv;          // ejection direction
        // How far the mover carried me AGAINST that direction this frame.
        float approach = 0.0f;
        if (g_prevValid) {
            float myx = sx - g_prevX, myy = sy - g_prevY;
            approach = -(myx * ux + myy * uy);
            if (approach < 0.0f) approach = 0.0f;
        }
        g_lastApproach = approach;                   // diagnostics
        float allowed = approach + g_separateSpeed * dt;
        if (mv > allowed) {
            cx = sx + ux * allowed;
            cy = sy + uy * allowed;
        }
    }

    float cz = *(float*)((BYTE*)unit + kOff_PosZ);

    // Dump every field holding our position BEFORE we overwrite them (afterwards
    // they hold the corrected value and no longer match what we search for). Fires
    // only while penetration is bad, at most once a second.
    if (g_debug >= 2 && (reach - nearest) > 0.25f) {
        static DWORD s_deep = 0;
        DWORD tnow = GetTickCount();
        if (tnow - s_deep >= 1000) {
            s_deep = tnow;
            DeepScanReport(unit, sx, sy, cz);
        }
    }

    // (Re)discover the out-of-object position copies. Cheap to keep current: only
    // runs while we are actually in contact, and at most twice a second.
    DWORD tnow = GetTickCount();
    if (g_ptrSlotCount == 0 || tnow - g_lastPtrScan >= 500) {
        g_lastPtrScan = tnow;
        ScanPointerSlots(unit, sx, sy, cz);
    }

    // Correct every copy of the position (the mover's authoritative one included),
    // not just +0x798, or the correction is discarded while moving.
    int copies = CorrectPositionCopies(unit, sx, sy, cz, cx, cy, &c);

    if (g_debug) {
        float applied = sqrtf((cx-sx)*(cx-sx) + (cy-sy)*(cy-sy));
        static DWORD s_lastLog = 0;
        DWORD now = GetTickCount();
        if (g_debug >= 2 || now - s_lastLog >= 100) {
            s_lastLog = now;
            Log("   fix f=%u pen=%.2f want=%.3f applied=%.3f appr=%.3f copies=%d ptr=%d",
                frame, reach - nearest, mv, applied, g_lastApproach, copies, g_ptrSlotCount);
        }
    }

    g_prevX = cx; g_prevY = cy; g_prevValid = 1;
    g_postX = cx; g_postY = cy; g_postValid = 1;
}

// Runs BEFORE the mover. Between our correction last frame and this call nothing
// should have moved us, so if the position has drifted from where we left it, some
// other stage (interpolation/smoothing) put it back - undo that, so the mover reads
// the corrected position rather than the illegal one. Bounded, and only while we
// were in contact, so it can never fight a teleport, knockback or server correction.
extern "C" void __cdecl PlayerCollide_PreMove(void* unit)
{
    if (!g_enabled || g_spellId == 0 || !unit || !g_postValid)
        return;
    unsigned __int64 guid = pGetActiveGuid();
    if (guid == 0)
        return;
    void* active = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnitOrPlayer);
    if (!active || active != unit)
        return;

    float* px = (float*)((BYTE*)unit + kOff_PosX);
    float* py = (float*)((BYTE*)unit + kOff_PosY);
    float dx = *px - g_postX, dy = *py - g_postY;
    float d2 = dx*dx + dy*dy;
    if (d2 < 1e-8f || d2 > 4.0f)                     // unchanged, or a real teleport
        return;
    float cz = *(float*)((BYTE*)unit + kOff_PosZ);
    CollectCtx empty;
    empty.count = 0;
    CorrectPositionCopies(unit, *px, *py, cz, g_postX, g_postY, &empty);
}

// Called from the wrapper AFTER the tick has integrated this frame's movement, so
// `unit`'s position is final and our eject is the last word on it. Cheap for
// non-active units: one active-guid fetch + pointer compare.
extern "C" void __cdecl PlayerCollide_OnTick(void* unit)
{
    if (!g_enabled || g_spellId == 0 || !unit)
        return;
    unsigned __int64 guid = pGetActiveGuid();
    if (guid == 0)
        return;
    void* active = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnitOrPlayer);
    if (!active || active != unit)
        return;
    if (!g_sawActiveTick) {                          // one-time: confirms the hook fires for us
        g_sawActiveTick = 1;
        Log("active player first tick seen (hook is on the right path)");
    }
    ResolveActive(unit);
}

// ============ probe: which Intersect call site is player movement? ==========
//
// 0x77F310 is only a thunk into 0x7A3B70. Hooking the real function and recording
// the RETURN ADDRESS of every call tells us exactly which of the ~21 call sites is
// the per-frame player movement clip: it is the one whose segment is short (about
// one frame of travel) and starts at our position. Guessing the enclosing function
// from static reads already cost us one wrong target, so measure instead.
static const DWORD kIntersectSite   = 0x007A3B70;
static const BYTE  kIntersectSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18 };
static const DWORD kIntersectResume = 0x007A3B76;

typedef int (__cdecl *IntersectFn)(const float* start, const float* end, void* a3,
                                   float* frac, DWORD mask, DWORD a6);
static BYTE*       g_trampIsect = NULL;
static IntersectFn g_origIsect  = NULL;
static int         g_probe = 0;

struct ProbeSite { DWORD ret; unsigned n; float len, dist, frac; DWORD mask; };
static ProbeSite g_probeSite[24];
static int       g_probeCount = 0;

static void ProbeRecord(DWORD ret, float len, float dist, float frac, DWORD mask)
{
    for (int i = 0; i < g_probeCount; ++i) {
        if (g_probeSite[i].ret == ret) {
            ProbeSite* s = &g_probeSite[i];
            ++s->n; s->len = len; s->dist = dist; s->frac = frac; s->mask = mask;
            return;
        }
    }
    if (g_probeCount < 24) {
        ProbeSite* s = &g_probeSite[g_probeCount++];
        s->ret = ret; s->n = 1; s->len = len; s->dist = dist; s->frac = frac;
        s->mask = mask;
    }
}

static void ProbeReport()
{
    static DWORD s_last = 0;
    DWORD now = GetTickCount();
    if (now - s_last < 2000)
        return;
    s_last = now;
    for (int i = 0; i < g_probeCount; ++i) {
        ProbeSite* s = &g_probeSite[i];
        Log("probe ret=0x%08X n=%u len=%.3f distFromPlayer=%.2f frac=%.3f mask=0x%X",
            s->ret, s->n, s->len, s->dist, s->frac, s->mask);
    }
    Log("collideAPI 75F0A0=%u 75FF90=%u 760720=%u 7139E0=%u | clip=%u mine=%u cut=%u",
        g_cA, g_cB, g_cC, g_cCollide, g_cClip, g_cClipMine, g_cClipCut);
    g_probeCount = 0;                                // fresh window each report
}

static int __cdecl IntersectWrapper(const float* start, const float* end, void* a3,
                                    float* frac, DWORD mask, DWORD a6)
{
    DWORD ret = (DWORD)_ReturnAddress();
    int r = g_origIsect(start, end, a3, frac, mask, a6);
    if (g_probe && start && end) {
        float px = 0, py = 0;
        unsigned __int64 guid = pGetActiveGuid();
        if (guid) {
            void* me = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnitOrPlayer);
            if (me) {
                px = *(float*)((BYTE*)me + kOff_PosX);
                py = *(float*)((BYTE*)me + kOff_PosY);
            }
        }
        float dx = end[0]-start[0], dy = end[1]-start[1], dz = end[2]-start[2];
        float ox = start[0]-px, oy = start[1]-py;
        ProbeRecord(ret, sqrtf(dx*dx+dy*dy+dz*dz), sqrtf(ox*ox+oy*oy),
                    frac ? *frac : -1.0f, mask);
        ProbeReport();
    }
    return r;
}

// ================= THE MOVEMENT CLIP (native prevention) ====================
//
// Found by measurement after CWorld::Intersect was ruled out entirely (its call
// sites are only camera and terrain picking, even when walking into a building).
// Player movement collision runs through the Collide.cpp system instead:
//
//   CMovement code at 0x6E9ECC..0x6E9F00 computes  delta = target - position
//   (position is CMovement+0x10, i.e. unit+0x798) and calls
//   0x762E00(this=CMovement, a1, a2, float dx, float dy, float dz)   [ret 0x14]
//   which drives 0x75FF90 (per-frame, ~2.8 calls/frame) and 0x75F0A0 (inner test).
//
// Because the movement delta is an ARGUMENT, we can clip it before the client
// ever moves: the illegal position is never produced, so nothing has to be
// corrected afterwards. We also slide along the contact, so it behaves like a
// wall rather than a stop.
static const DWORD kClipSite   = 0x00762E00;
static const BYTE  kClipSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C };
static const DWORD kClipResume = 0x00762E06;
static const DWORD kOff_CMovementPos = 0x10;    // CMovement+0x10 == unit+0x798
// The predicted position the movement target is built from (0x6E9E20 computes
// target = [CMovement+0x4C] + delta). Distinct from the real position at +0x10.
static const DWORD kOff_Predicted = 0x4C;

typedef int (__thiscall *ClipFn)(void* self, void* a1, void* a2,
                                 float dx, float dy, float dz);
static BYTE*  g_trampClip = NULL;
static ClipFn g_origClip  = NULL;

// ------------------------------------------------------------- the rule set
//
// Four auras, spanning two independent axes - WHO is affected (enemies only vs
// everyone) and DIRECTION (one-way: others are blocked by me but I pass through
// them; vs mutual). Everything is decided from OUR client's point of view, since
// each client only ever clips its own movement:
//
//   BlockEnemies   on X : X's enemies cannot pass through X (X still passes them)
//   BlockAll       on X : nobody can pass through X          (X still passes them)
//   CollideEnemies on X : mutual collision between X and X's enemies
//   CollideAll     on X : mutual collision between X and everyone
//
// So "am I blocked by X" is:
//     X has BlockAll
//  or X has BlockEnemies   and we are hostile
//  or (I or X) has CollideAll
//  or (I or X) has CollideEnemies and we are hostile
//
// The one-way auras are deliberately only checked on X: that is what makes them
// one-way. The mutual ones are checked on both, so either party carrying it is
// enough - which is what makes X's aura also stop me.
static int g_spellBlockEnemies   = 90210;   // Obstruction
static int g_spellBlockAll       = 90211;   // Immovable
static int g_spellCollideEnemies = 90212;   // Bodycheck
static int g_spellCollideAll     = 90213;   // Solid Form

// Do the mover's own mutual auras (Bodycheck/Solid Form) recruit NON-player
// blockers - creatures, pets, totems? Sound for your OWN movement (your client
// stops you and your reported position is the authority, so everyone agrees),
// and between DLL'd players it stays consistent because every client stops at
// the same bodies. The cost is rendering units whose stream does NOT stop
// there - playerbots, DLL-less clients - which lean through the NPC up to
// RemoteDebtCap and converge, instead of hard-blocking. An NPC deliberately
// GIVEN a collision aura blocks regardless of this switch.
static int g_npcBlockers = 1;

static bool IsHostileTo(void* me, void* other)
{
    if (!pGetReaction || !me || !other)
        return false;
    return pGetReaction(me, other) <= 1;             // same test the client uses
}

// Creature-family HighGuids (creatures 0xF130, pets 0xF140, vehicles 0xF150,
// GOs 0xF110...) all start with an F nibble; player guids are plain counters
// with a zero high dword. Everything reaching this helper came through the
// unit-or-player object mask, so those are the only two families possible
// (MO transports, 0x1FC0, would classify as non-player anyway). Cheap, and
// needs no object-manager round trip.
static bool IsPlayerObject(void* unit)
{
    DWORD hi = *(DWORD*)((BYTE*)unit + kOff_GuidHigh);
    return (hi & 0xF0000000u) == 0;
}

// Does `other` block `me`? `me` is our unit, `other` a candidate blocker.
static bool BlocksMe(void* me, void* other)
{
    if (!me || !other)
        return false;

    // Cheapest first: the unconditional ones need no reaction lookup.
    if (g_spellBlockAll && UnitHasAura(other, g_spellBlockAll))
        return true;
    // The mover's own mutual aura recruits blockers. NpcBlockers (default ON)
    // decides whether that includes creatures, pets and totems: solid crowds,
    // at the cost that a mover whose stream does not stop at an NPC (playerbot,
    // deleted DLL) leans through it on the tether instead of hard-stopping.
    // With it off, only players are recruited. An NPC deliberately GIVEN a
    // collision aura blocks via the other-side checks regardless - aura state
    // is replicated, every client agrees about it.
    if (g_spellCollideAll &&
        (UnitHasAura(other, g_spellCollideAll) ||
         (UnitHasAura(me, g_spellCollideAll) &&
          (g_npcBlockers || IsPlayerObject(other)))))
        return true;

    const bool wantHostile =
        (g_spellBlockEnemies   && UnitHasAura(other, g_spellBlockEnemies)) ||
        (g_spellCollideEnemies && (UnitHasAura(other, g_spellCollideEnemies) ||
                                   (UnitHasAura(me, g_spellCollideEnemies) &&
                                    (g_npcBlockers || IsPlayerObject(other)))));
    if (wantHostile && IsHostileTo(me, other))
        return true;

    // Legacy single-spell mode (the original SpellId/BothNeedBuff behaviour),
    // kept so existing configs keep working.
    if (g_spellId && UnitHasAura(other, g_spellId) &&
        (!g_bothNeed || UnitHasAura(me, g_spellId)))
        return true;

    return false;
}

// Any collision configured at all?
static bool AnyCollisionSpell()
{
    return g_spellId || g_spellBlockEnemies || g_spellBlockAll ||
           g_spellCollideEnemies || g_spellCollideAll;
}

// Earliest t in [0,1] where the moving point (start + t*delta) comes within
// `radius` of (cx,cy). Standard quadratic; 2-D because this is a body, not a
// floor. Used by the circle shape (Sides = 0).
static bool SegmentHitsCircle(float sx, float sy, float dx, float dy,
                              float cx, float cy, float radius, float* tOut)
{
    float fx = sx - cx, fy = sy - cy;
    float a = dx*dx + dy*dy;
    if (a < 1e-12f)
        return false;                                // not moving
    float b = 2.0f * (fx*dx + fy*dy);
    float c = fx*fx + fy*fy - radius*radius;
    if (c <= 0.0f) {                                 // already inside: block now
        *tOut = 0.0f;
        return true;
    }
    float disc = b*b - 4.0f*a*c;
    if (disc < 0.0f)
        return false;                                // passes wide
    float t = (-b - sqrtf(disc)) / (2.0f * a);       // first (entering) root
    if (t < 0.0f || t > 1.0f)
        return false;                                // hit is outside this step
    *tOut = t;
    return true;
}

// ---------------------------------------------------------- collision shape
//
// An axis-aligned OCTAGON, not a circle. A circle's normal turns continuously, so
// sliding along one smoothly re-aims your velocity - you orbit the body - and the
// slide also pushes you off the surface, which is why a "contact band" was needed
// to keep friction applying at all. An octagon has flat faces: sliding along a
// face keeps the normal constant, so you move in a straight line, stay ON the
// face, and the next face stops you at the corner. Getting around someone then
// requires actually steering, instead of being slung.
//
// The octagon does not rotate with either character - it is fixed to the world
// axes, so contact does not change as anyone turns.
// Sides: 0 = circle, 4 = axis-aligned SQUARE (only the +-x/+-y normals),
// 8 = octagon. The polygons are fixed to the world axes and never rotate with
// either character. The shape is chosen inside OctagonDist/SegmentHitsOctagon so
// every call site (swept clip, contact state, depenetration) follows it.
static int g_sides = 0;               // 0 = circle
static int g_syncPredicted = 1;       // compensate [CMovement+0x4C] drift

// Remote-unit clipping. ClipRemotes = 0 is the hands-off switch: remote units
// are never touched and render exactly the raw network stream.
//
// With it on, remote PLAYERS are PINNED at blockers, released per unit only on
// PROOF the server disagrees. Three designs taught us why this is the shape:
//
//  v1.00007 pinned unconditionally. Solid walls - and units whose packets do
//  not stop (playerbots, rim-grazers whose own client let them pass) snapped
//  a heartbeat's worth of distance forward on every packet: the "porting
//  around" reports.
//  v1.00008/9 bounded the refused amount (ledger, then tether). Both bounded
//  against the WRONG signal: the delta this hook refuses is divergence from
//  the dead-reckoned GHOST (base + movement-flag extrapolation), not from the
//  server. A player pressing W against a wall generates unbounded ghost
//  motion while their packets sit still at the wall - refusing it forever is
//  CORRECT. Any cap eventually yields to the ghost and drags the unit through
//  the body until the next heartbeat snaps it back: the "running through me,
//  teleporting back, over and over" report, i.e. the exact artefact remote
//  clipping exists to remove.
//
// The only signal that distinguishes the cases is the packet stream itself,
// so that is what v3 uses. While pinning a unit we know exactly where we put
// it; if its position then JUMPS (> kRemoteSnapYd) between calls, a packet
// overruled our pin. Where it landed disambiguates further (v4, measured):
// jumps that land OUTSIDE the body are their client sliding them around the
// rim faster than our stale-facing simulation - accept the correction and
// keep pinning; only a jump landing INSIDE the body proves the authority is
// walking them through, and only then is the unit released for
// kRemoteSuppressMs to render raw. Wall-pressed players never trigger any of
// it (their packets agree with the pin): solid, permanent collision.
//
// Creature movers are never clipped: no server stream stops an NPC or pet at a
// player body, so pinning one here only makes it stutter against everyone
// carrying the global aura.
static int   g_clipRemotes = 1;
static const float kRemoteSnapYd    = 0.6f;   // observed jump that counts as a packet overrule
static const DWORD kRemoteSuppressMs = 2000;  // how long a disproven pin stays released

// How deep inside the body a packet must land to count as a REAL pass-through.
// Measured (2026-08-13, two-client orbit test): a legal graze rides at
// face-depth -0.03..-0.19 - their client's depenetration deadband alone allows
// 0.12, and near an octagon corner a point at Euclidean ~1.97 measures ~0.19
// deep in face metric. An earlier threshold of 0.15 sat INSIDE that band, so
// legitimate orbit packets flickered between verdicts and every false INSIDE
// bought 2s of raw ghost - the in/out artefact. A genuine transit passes
// through depth ~2.0, so 0.6 splits the populations with a wide margin.
static const float kInsideDepthYd   = 0.6f;

// Stand-on-players. OFF by default and INSTALL-gated: with it off the ground
// hook is never applied, so the DLL behaves exactly as it did before the feature
// existed. Nothing about normal collision changes either way.
static int   g_standOnPlayers = 0;
// Stage counters for the stand-on-players experiment. Which one stops climbing
// tells us whether the ground query is even on the local player's path.
static unsigned g_cGround = 0;      // hook fired at all
static unsigned g_cGroundMine = 0;  // ...for the local player
static unsigned g_cGroundCand = 0;  // ...and a body surface was found
// Height of a body's walkable top surface above its feet. MUST stay under a
// player's jump apex (~1.6 yд) or the surface is literally unreachable and
// nothing ever lands on it - 2.0 failed for exactly that reason, by ~0.05.
static float g_standHeight = 1.2f;
#define SHAPE_STEP (g_sides == 4 ? 2 : 1)

static const float kOctN[8][2] = {
    {  1.000000f,  0.000000f }, {  0.707107f,  0.707107f },
    {  0.000000f,  1.000000f }, { -0.707107f,  0.707107f },
    { -1.000000f,  0.000000f }, { -0.707107f, -0.707107f },
    {  0.000000f, -1.000000f }, {  0.707107f, -0.707107f },
};

// Signed distance from a point to the octagon boundary (positive = outside),
// plus the normal of the face that governs it.
static float OctagonDist(float px, float py, float cx, float cy, float r,
                         float* nxOut, float* nyOut)
{
    float rx = px - cx, ry = py - cy;
    if (g_sides == 0) {                              // circle
        float d = sqrtf(rx*rx + ry*ry);
        if (d > 1e-6f) {
            if (nxOut) *nxOut = rx / d;
            if (nyOut) *nyOut = ry / d;
        } else {                                     // concentric: pick any normal
            if (nxOut) *nxOut = 1.0f;
            if (nyOut) *nyOut = 0.0f;
        }
        return d - r;
    }
    float best = -1e9f;
    int bi = 0;
    const int step = SHAPE_STEP;
    for (int i = 0; i < 8; i += step) {
        float d = kOctN[i][0]*rx + kOctN[i][1]*ry - r;
        if (d > best) { best = d; bi = i; }
    }
    if (nxOut) *nxOut = kOctN[bi][0];
    if (nyOut) *nyOut = kOctN[bi][1];
    return best;
}

// Segment vs octagon by half-plane clipping. Returns the entry fraction and the
// face normal there (which is what gives us flat-face sliding).
static bool SegmentHitsOctagon(float sx, float sy, float dx, float dy,
                               float cx, float cy, float r,
                               float* tOut, float* nxOut, float* nyOut)
{
    if (g_sides == 0) {                              // circle
        float t;
        if (!SegmentHitsCircle(sx, sy, dx, dy, cx, cy, r, &t))
            return false;
        // contact normal is radial at the point we touch
        float hx = sx + dx * t - cx, hy = sy + dy * t - cy;
        float hl = sqrtf(hx*hx + hy*hy);
        if (hl > 1e-6f) { *nxOut = hx / hl; *nyOut = hy / hl; }
        else            { *nxOut = 1.0f;    *nyOut = 0.0f;    }
        *tOut = t;
        return true;
    }

    float tEnter = 0.0f, tExit = 1.0f;
    int ni = -1;
    float rx = sx - cx, ry = sy - cy;
    const int step = SHAPE_STEP;
    for (int i = 0; i < 8; i += step) {
        float n0 = kOctN[i][0], n1 = kOctN[i][1];
        float dist  = n0*rx + n1*ry - r;             // >0 = outside this face
        float denom = n0*dx + n1*dy;
        if (denom > -1e-9f && denom < 1e-9f) {
            if (dist > 0.0f)
                return false;                        // parallel to it, and outside
            continue;
        }
        float t = -dist / denom;
        if (denom < 0.0f) {                          // crossing inward
            if (t > tEnter) { tEnter = t; ni = i; }
        } else {                                     // crossing outward
            if (t < tExit) tExit = t;
        }
        if (tEnter > tExit)
            return false;
    }
    if (tEnter > 1.0f)
        return false;
    if (ni < 0) {                                    // started inside it
        OctagonDist(sx, sy, cx, cy, r, nxOut, nyOut);
        *tOut = 0.0f;
        return true;
    }
    *tOut = tEnter;
    *nxOut = kOctN[ni][0];
    *nyOut = kOctN[ni][1];
    return true;
}

struct ClipCtx {
    void* self;
    float sz;              // our z, for the vertical extent test
    float cx, cy;          // current point of the walk
    float rx, ry;          // remaining delta
    float radius;
    float bestT;
    float hx, hy;          // centre of the blocker we hit
    float nx, ny;          // face normal at the contact (flat, from the octagon)
    int   hit;
};

static int __cdecl ClipCollectCb(DWORD guidLo, DWORD guidHi, void* arg)
{
    ClipCtx* c = (ClipCtx*)arg;
    void* o = pObjectPtr(guidLo, guidHi, kTypeMaskUnitOrPlayer);
    if (!o || o == c->self)
        return 1;
    float ox = *(float*)((BYTE*)o + kOff_PosX);
    float oy = *(float*)((BYTE*)o + kOff_PosY);
    float oz = *(float*)((BYTE*)o + kOff_PosZ);
    if (fabsf(oz - c->sz) >= g_height)                // different floor / overhead
        return 1;
    // Standing ON them, not walking INTO them: a cylinder's side must not block
    // you while you are above its top face, or the wall clip shoves you off the
    // edge and you can never walk around up there.
    if (g_standOnPlayers && c->sz >= oz + g_standHeight - 0.15f)
        return 1;
    float dx = ox - c->cx, dy = oy - c->cy;
    float reach = c->radius + sqrtf(c->rx*c->rx + c->ry*c->ry) + 1.0f;
    if (dx*dx + dy*dy > reach*reach)
        return 1;
    if (!BlocksMe(c->self, o))
        return 1;
    float t, nx, ny;
    if (SegmentHitsOctagon(c->cx, c->cy, c->rx, c->ry, ox, oy, c->radius,
                           &t, &nx, &ny) && t < c->bestT) {
        c->bestT = t; c->hx = ox; c->hy = oy; c->nx = nx; c->ny = ny; c->hit = 1;
    }
    return 1;
}

// Walk the requested delta, stopping at each buffed player we would enter and
// sliding the remainder along the contact. Returns the delta actually allowed.
// Nearest buffed player overlapping us right now (pure distance query - the swept
// test cannot answer this when the step is zero, e.g. standing still).
struct NearCtx { void* self; float px, py, pz, radius; float best2, hx, hy; int hit; };

static int __cdecl NearestCb(DWORD guidLo, DWORD guidHi, void* arg)
{
    NearCtx* c = (NearCtx*)arg;
    void* o = pObjectPtr(guidLo, guidHi, kTypeMaskUnitOrPlayer);
    if (!o || o == c->self)
        return 1;
    float ox = *(float*)((BYTE*)o + kOff_PosX);
    float oy = *(float*)((BYTE*)o + kOff_PosY);
    float oz = *(float*)((BYTE*)o + kOff_PosZ);
    if (fabsf(oz - c->pz) >= g_height)                // different floor / overhead
        return 1;
    if (g_standOnPlayers && c->pz >= oz + g_standHeight - 0.15f)
        return 1;                                    // we are on top of them
    float dx = ox - c->px, dy = oy - c->py;
    float d2 = dx*dx + dy*dy;
    if (d2 >= c->radius * c->radius || d2 >= c->best2)
        return 1;
    if (!BlocksMe(c->self, o))
        return 1;
    c->best2 = d2; c->hx = ox; c->hy = oy; c->hit = 1;
    return 1;
}

static int FindNearestBlocker(void* self, float px, float py, float pz, float radius,
                              float* best2, float* hx, float* hy)
{
    NearCtx c;
    c.self = self; c.px = px; c.py = py; c.pz = pz; c.radius = radius;
    c.best2 = 1e18f; c.hx = c.hy = 0.0f; c.hit = 0;
    pEnumVisible(NearestCb, &c);
    if (!c.hit)
        return 0;
    *best2 = c.best2; *hx = c.hx; *hy = c.hy;
    return 1;
}

// `unitSelf` must be the UNIT pointer, not the CMovement: the object enumeration
// yields unit pointers, so comparing against CMovement (unit+0x788) never matches
// and we collide with OURSELVES at distance 0 - which trips the concentric guard
// and silently leaves the delta unclipped. That bug made this look like the hook
// was doing nothing at all.
static void ClipDelta(void* unitSelf, float px, float py, float pz,
                      bool isLocal, float* dx, float* dy)
{
    void* self = unitSelf;
    float curx = px, cury = py;
    float remx = *dx, remy = *dy;
    float totx = 0.0f, toty = 0.0f;

    // CONTACT STATE. The swept test only fires when the step actually enters the
    // circle - so once we are resting just outside the surface and moving along
    // it, no hit is ever reported and friction never gets a chance to apply. That
    // is why you could still glide around the body at full speed with friction 0.
    // While we are touching, damp the tangential motion every frame regardless of
    // whether a hit is detected.
    {
        // The band must be wide enough to hold us through a hug. Measured: while
        // being slung around a body the distance sits at 2.1-2.4 yd against a 2.0
        // yd radius, so a 0.15 band left us "not touching" on almost every frame
        // and friction never ran - which is why SlideFriction appeared to do
        // nothing at any value. Sliding pushes you outward past a narrow band.
        const float kContactEps = g_contactBand;
        float best2 = 1e18f, hx = 0, hy = 0;
        if (FindNearestBlocker(unitSelf, px, py, pz,
                               2.0f * g_radius + kContactEps + 1.0f,
                               &best2, &hx, &hy)) {
            float nx, ny;
            float sd = OctagonDist(px, py, hx, hy, 2.0f * g_radius, &nx, &ny);
            if (sd <= kContactEps) {                 // touching this face
                // `into` < 0 means we are pressing INTO the body; > 0 means we are
                // leaving it. Only the pressing case is clipped and damped -
                // damping unconditionally also froze movement AWAY from the body,
                // which locked players in place at friction 0.
                float into = remx*nx + remy*ny;
                if (into < 0.0f) {
                    remx -= nx * into;               // cancel motion into the body
                    remy -= ny * into;
                    // Friction applies to REMOTES too - not for feel, for
                    // FIDELITY. Their own client damps their slide by exactly
                    // this factor, so their packets trace the damped arc; an
                    // undamped render runs ahead of the packets along the rim,
                    // reads as a packet overrule, and drops the unit to raw -
                    // which is the in/out ghost cycling on grazing contact.
                    // (Gating this to isLocal was tried in v1.00009: that was
                    // the observed result.)
                    remx *= g_slideFriction;         // damp the tangential remainder
                    remy *= g_slideFriction;
                }
            }
        }
    }

    for (int iter = 0; iter < 3; ++iter) {
        if (remx*remx + remy*remy < 1e-10f)
            break;
        ClipCtx c;
        c.self = self; c.cx = curx; c.cy = cury; c.rx = remx; c.ry = remy;
        c.radius = 2.0f * g_radius;
        c.bestT = 1.0f; c.hit = 0; c.hx = c.hy = 0.0f;
        pEnumVisible(ClipCollectCb, &c);
        if (!c.hit) {
            totx += remx; toty += remy;
            remx = remy = 0.0f;
            break;
        }
        ++g_cClipCut;
        // advance to the contact point
        float ax = remx * c.bestT, ay = remy * c.bestT;
        curx += ax; cury += ay;
        totx += ax; toty += ay;
        remx -= ax; remy -= ay;
        // Slide along the FACE we hit. The face normal is constant along that
        // face, so the slide is a straight line that stays on the surface -
        // unlike a circle, where the normal turns under you and carries you
        // around the body.
        float nx = c.nx, ny = c.ny;
        float into = remx*nx + remy*ny;
        if (into < 0.0f) {                           // heading inward -> remove it
            remx -= nx * into;
            remy -= ny * into;
            // Friction on what is left. Removing only the inward component is the
            // geometrically "correct" frictionless slide, but against a body-sized
            // cylinder it preserves almost all of your speed on a glancing hit and
            // slings you around them. Damping the tangential part is what makes
            // contact read as bumping into a person rather than a greased pole.
            // Applied to remotes too: their own client damps THEM by this factor,
            // so damping our render is what keeps it tracking their packets (see
            // the fidelity note in the contact block above).
            remx *= g_slideFriction;
            remy *= g_slideFriction;
        }
    }
    float outx = totx + remx, outy = toty + remy;

    // Safety: never hand back a longer SLIDE than we were given. The projection
    // math cannot lengthen it, but clamping makes "the collision made me faster"
    // - the zip - structurally impossible rather than merely unlikely. Done before
    // depenetration so the (separate, deliberate) outward push is not scaled away.
    float inLen2  = (*dx)*(*dx) + (*dy)*(*dy);
    float outLen2 = outx*outx + outy*outy;
    if (outLen2 > inLen2 && outLen2 > 1e-12f) {
        float s = sqrtf(inLen2 / outLen2);
        outx *= s; outy *= s;
    }

    // Depenetration is for OUR OWN movement only. A remote unit's true position
    // comes from the server; nudging it outward fights the next authoritative
    // update and reads as jitter. For remote units we only stop the predicted
    // step from entering a body - we never argue with where it actually is.
    if (isLocal)
    // Depenetration. Resting exactly on (or just inside) the surface makes every
    // later frame take the "already inside" path, which only re-projects - so you
    // orbit the body at full tangential speed instead of being held off it. A
    // small outward bias keeps us outside, which is what stops a grazing/tangent
    // approach from slinging you around the bubble. Independent of the step, so it
    // also works while standing still.
    {
        float nearest2 = 1e18f, hx = 0, hy = 0;
        if (FindNearestBlocker(unitSelf, px, py, pz, 2.0f * g_radius + 1.0f,
                               &nearest2, &hx, &hy)) {
            float nx, ny;
            float sd = OctagonDist(px, py, hx, hy, 2.0f * g_radius, &nx, &ny);
            // Deadband. Parking exactly on the surface reads as "very slightly
            // inside" every frame thanks to float noise, and pushing out on that
            // fights the player's own forward input - measured as a 0.04 yd/frame
            // shove backwards, which is the contact vibration. Only genuine
            // penetration (buff popped while stacked, a blink-in) is corrected.
            const float kPenetrationDeadband = 0.12f;
            if (sd < -kPenetrationDeadband) {        // meaningfully inside
                const float kMaxPush = 0.04f;        // per frame; gentle, never a snap
                float push = (-sd) > kMaxPush ? kMaxPush : (-sd);
                outx += nx * push;
                outy += ny * push;
            }
        }
    }

    // Never hand back movement that opposes what was asked for. Our job is to
    // STOP you, not to walk you backwards: a reversed delta fights the held key
    // and oscillates. If the result points against the request, drop it to zero
    // along that axis of disagreement instead.
    {
        float inLen2 = (*dx) * (*dx) + (*dy) * (*dy);
        if (inLen2 > 1e-12f) {
            float dot = outx * (*dx) + outy * (*dy);
            if (dot < 0.0f) {
                // Remove the component that runs counter to the request, keeping
                // any sideways (sliding) part.
                float ux = (*dx) / sqrtf(inLen2), uy = (*dy) / sqrtf(inLen2);
                float along = outx * ux + outy * uy;      // negative here
                outx -= ux * along;
                outy -= uy * along;
            }
        }
    }

    *dx = outx;
    *dy = outy;
}

// ============ stand-on-players: find a body's top surface beneath us =========
//
// Deliberately ignores g_height (the collision floor/ceiling gate): that exists
// so you do not collide with someone upstairs, but standing ON someone means
// being well ABOVE them, which that gate would reject.
struct StandCtx {
    void* self;
    float px, py, pz;      // our position
    float groundZ;         // the terrain height the client found
    float radius;          // xy reach of the body's top surface
    float bestTop;         // highest valid surface found
    int   hit;
};

static int __cdecl StandCb(DWORD guidLo, DWORD guidHi, void* arg)
{
    StandCtx* c = (StandCtx*)arg;
    void* o = pObjectPtr(guidLo, guidHi, kTypeMaskUnitOrPlayer);
    if (!o || o == c->self)
        return 1;

    float ox = *(float*)((BYTE*)o + kOff_PosX);
    float oy = *(float*)((BYTE*)o + kOff_PosY);
    float oz = *(float*)((BYTE*)o + kOff_PosZ);

    float dx = ox - c->px, dy = oy - c->py;
    if (dx*dx + dy*dy > c->radius * c->radius)       // not over their body
        return 1;

    // You can only stand on someone you would collide with, so the whole
    // feature stays governed by the same auras.
    if (!BlocksMe(c->self, o))
        return 1;

    float top = oz + g_standHeight;
    if (top > c->pz + 0.35f)                         // their head is above us
        return 1;
    if (top <= c->groundZ)                           // terrain is higher anyway
        return 1;
    if (top > c->bestTop) {
        c->bestTop = top;
        c->hit = 1;
    }
    return 1;
}

// ===== diagnostic: what Z does THIS client hold for the bodies next to us? ====
//
// Standing on someone works on the stander's own screen but the observer sees
// them on the floor. Two very different causes produce exactly that, and they
// need fixes in opposite places, so measure instead of guessing:
//
//   observer logs the stander's z ~= +StandHeight -> the stander DID report the
//       elevated Z and the observer is re-snapping it to its own terrain; the
//       fix belongs in the remote-unit path, and making the local client
//       "believe" harder would change nothing.
//   observer logs the stander's z ~= ground       -> the stander never reported
//       it. Our support is applied after the client has already decided where
//       it is, so the fix is to make the client genuinely resolve against the
//       body (inject into the sweep) so its own packets carry the height.
//
// Run with Debug = 1 on BOTH clients, stand on the other character, and compare.
struct ZProbeCtx {
    void* self;
    float px, py, pz;
    int   n;
    int   used;
    char  buf[256];
};

static int __cdecl ZProbeCb(DWORD guidLo, DWORD guidHi, void* arg)
{
    ZProbeCtx* c = (ZProbeCtx*)arg;
    void* o = pObjectPtr(guidLo, guidHi, kTypeMaskUnitOrPlayer);
    if (!o || o == c->self)
        return 1;

    float ox = *(float*)((BYTE*)o + kOff_PosX);
    float oy = *(float*)((BYTE*)o + kOff_PosY);
    float oz = *(float*)((BYTE*)o + kOff_PosZ);
    float dx = ox - c->px, dy = oy - c->py;
    float d2 = dx * dx + dy * dy;
    if (d2 > 9.0f)                        // only bodies we could plausibly be on
        return 1;

    ++c->n;
    if (c->n <= 3)
        c->used += _snprintf_s(c->buf + c->used, sizeof(c->buf) - c->used, _TRUNCATE,
                               " [%08X d=%.2f z=%.3f dz=%+.3f]",
                               guidLo, sqrtf(d2), oz, oz - c->pz);
    return 1;
}

static void ZProbe(void* self, float px, float py, float pz)
{
    static DWORD s_lastZProbe = 0;
    DWORD now = GetTickCount();
    if (now - s_lastZProbe < 250)          // 4 Hz is plenty to compare two logs
        return;
    s_lastZProbe = now;

    ZProbeCtx c;
    c.self = self; c.px = px; c.py = py; c.pz = pz;
    c.n = 0; c.used = 0; c.buf[0] = 0;
    pEnumVisible(ZProbeCb, &c);
    if (c.n > 0)
        Log("ZProbe: myZ=%.3f near=%d%s", pz, c.n, c.buf);
}

// Per-unit pin tracking: where we left each clipped remote, so the next call
// can tell "still where we put it" (packets agree with the pin) from "jumped"
// (a packet overruled it). Only units actually being clipped occupy a slot.
struct RemoteTrack {
    DWORD lo, hi;          // unit guid
    float ex, ey;          // where our clip left them - expected next call
    DWORD lastSeen;        // tick of the last clip we applied (0 = free slot)
    DWORD suppressUntil;   // while (int)(suppressUntil - now) > 0: hands off
    int   insideStreak;    // consecutive overrules that landed inside a body.
                           // One is noise (measured: a wall-pressed player whose
                           // packets never left the ring still produced isolated
                           // INSIDE verdicts); a unit the authority walks through
                           // lands deep inside on EVERY heartbeat. Suppress at 2.
};
static RemoteTrack g_track[16];

static RemoteTrack* TrackFind(DWORD lo, DWORD hi)
{
    for (int i = 0; i < 16; ++i)
        if (g_track[i].lastSeen && g_track[i].lo == lo && g_track[i].hi == hi)
            return &g_track[i];
    return NULL;
}

static RemoteTrack* TrackAlloc(DWORD lo, DWORD hi, DWORD now)
{
    RemoteTrack* best = &g_track[0];
    DWORD bestAge = now - g_track[0].lastSeen;       // wrap-safe age
    if (!g_track[0].lastSeen) bestAge = 0xFFFFFFFF;
    for (int i = 1; i < 16; ++i) {
        if (!g_track[i].lastSeen) { best = &g_track[i]; bestAge = 0xFFFFFFFF; break; }
        DWORD age = now - g_track[i].lastSeen;
        if (age > bestAge) { bestAge = age; best = &g_track[i]; }
    }
    // Never steal a slot from a unit still in contact (age under 3s): evicting
    // a live pin would zero its history and un-release a disproven one. With
    // the table full of live pins, the new unit simply goes untracked - it
    // still gets pinned, it just cannot be auto-released until a slot frees.
    if (bestAge < 3000)
        return NULL;
    best->lo = lo; best->hi = hi;
    best->suppressUntil = now;
    best->insideStreak = 0;
    return best;
}

static int __fastcall ClipWrapper(void* self, void* /*edx*/, void* a1, void* a2,
                                  float dx, float dy, float dz)
{
    ++g_cClip;
    bool clipped = false;
    bool localMover = false;
    bool  supported = false;      // resting on a body's top this frame
    float supportZ = 0.0f;
    float shortx = 0.0f, shorty = 0.0f;   // movement we refused, for the base fixup
    if (g_enabled && g_native && AnyCollisionSpell() && self) {
        unsigned __int64 guid = pGetActiveGuid();
        if (guid) {
            void* meLocal = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnitOrPlayer);
            DWORD remLo = 0, remHi = 0;
            RemoteTrack* trk = NULL;

            // Which unit owns this CMovement? Ours, or a REMOTE unit's.
            //
            // Remote PLAYERS are pinned at blockers, auto-released per unit on
            // proof the server disagrees (see the comment at g_clipRemotes).
            // Creature movers are the server's alone - hands off.
            void* mover = (BYTE*)self - kOff_Movement;
            if (mover != meLocal) {
                // Not us - prove it really is a live unit before touching it,
                // by resolving its own GUID back through the object manager.
                if (!Readable((BYTE*)mover + kOff_GuidLow, 8))
                    mover = NULL;
                else {
                    DWORD lo = *(DWORD*)((BYTE*)mover + kOff_GuidLow);
                    DWORD hi = *(DWORD*)((BYTE*)mover + kOff_GuidHigh);
                    if (pObjectPtr(lo, hi, kTypeMaskUnitOrPlayer) != mover)
                        mover = NULL;
                    else if ((hi & 0xF0000000u) != 0)
                        mover = NULL;               // creature/pet mover: server-owned
                    else if (!g_clipRemotes)
                        mover = NULL;               // hands-off switch
                    else {
                        remLo = lo; remHi = hi;
                        trk = TrackFind(lo, hi);
                        if (trk && (int)(trk->suppressUntil - GetTickCount()) > 0)
                            mover = NULL;           // pin disproven by a packet: raw
                    }
                }
            }

            if (mover) {
                if (mover == meLocal)
                    ++g_cClipMine;
                {
                    void* me = mover;                // clip in the mover's own frame
                    float px = *(float*)((BYTE*)self + kOff_CMovementPos);
                    float py = *(float*)((BYTE*)self + kOff_CMovementPos + 4);
                    float odx = dx, ody = dy;
                    float pz = *(float*)((BYTE*)self + kOff_CMovementPos + 8);
                    bool const isLocal = (mover == meLocal);

                    // Log everyone's Z as this client holds it, so two clients'
                    // logs can be compared while one stands on the other.
                    if (g_debug && isLocal)
                        ZProbe(me, px, py, pz);

                    // Vertical support: land on, and stay on, a body's top.
                    //
                    // 0x714B60 (the ground query) is never called for the local
                    // player - measured, twice, with zero probe hits - so the
                    // floor cannot be injected there. But this hook already
                    // receives dz and we were passing it straight through. If
                    // the step would cross a blocking body's top surface, stop
                    // the fall on it; if we are already resting on one, hold the
                    // descent at zero so gravity cannot pull us off.
                    if (g_standOnPlayers && isLocal && dz < 0.0f) {
                        StandCtx sc;
                        sc.self = me; sc.px = px; sc.py = py; sc.pz = pz;
                        sc.groundZ = pz + dz - 100.0f;   // no terrain filter here:
                                                         // the client's own ground
                                                         // handling still applies
                                                         // its result separately
                        sc.radius = 2.0f * g_radius;
                        sc.bestTop = -1e9f; sc.hit = 0;
                        pEnumVisible(StandCb, &sc);
                        if (sc.hit && sc.bestTop >= pz + dz && sc.bestTop <= pz + 0.05f) {
                            float allowed = sc.bestTop - pz;   // <= 0
                            if (allowed > 0.0f) allowed = 0.0f;
                            if (g_debug) {
                                static DWORD s_l = 0;
                                DWORD n = GetTickCount();
                                if (n - s_l >= 250) {
                                    s_l = n;
                                    Log("stand: pz=%.2f top=%.2f dz %.3f -> %.3f",
                                        pz, sc.bestTop, dz, allowed);
                                }
                            }
                            dz = allowed;
                            supported = true;
                            supportZ = sc.bestTop;
                        }
                    }

                    // Packet-overrule check. While we pin a unit, its position
                    // next call must be exactly where our clip left it - the
                    // only other writer is the packet path, which rebases
                    // position directly. A jump means a packet moved them off
                    // our pin. WHERE it moved them decides what it means:
                    //
                    //   OUTSIDE the body: their own client is sliding them
                    //   around the rim and our simulation of that slide fell
                    //   behind (their facing between heartbeats is stale, so
                    //   our arc stalls while their steered arc advances -
                    //   measured 0.6-3.6 yd per heartbeat). The rebase already
                    //   corrected the render; accept it and KEEP PINNING from
                    //   the new spot. Releasing to raw here was the in/out
                    //   cycling: the raw ghost dives into the body every frame
                    //   and every heartbeat yanks it back out.
                    //
                    //   INSIDE the body: the authority really is walking them
                    //   through (playerbot, no DLL) - stop arguing, render raw
                    //   for a while instead of pin/snap cycling.
                    bool doClip = true;
                    if (!isLocal && trk) {
                        DWORD now = GetTickCount();
                        if ((DWORD)(now - trk->lastSeen) < 250) {
                            float jx = px - trk->ex, jy = py - trk->ey;
                            if (jx * jx + jy * jy > kRemoteSnapYd * kRemoteSnapYd) {
                                float b2 = 1e18f, hx = 0.0f, hy = 0.0f;
                                float sd = 1e9f;
                                if (FindNearestBlocker(me, px, py, pz,
                                                       2.0f * g_radius + 0.5f,
                                                       &b2, &hx, &hy)) {
                                    float nx, ny;
                                    sd = OctagonDist(px, py, hx, hy,
                                                     2.0f * g_radius, &nx, &ny);
                                }
                                // One INSIDE landing is noise (measured: a
                                // wall-pressed player whose packets never left
                                // the ring still produced isolated verdicts);
                                // a genuine pass-through lands inside on every
                                // consecutive heartbeat. Two in a row = real.
                                if (sd < -kInsideDepthYd) {
                                    if (++trk->insideStreak >= 2) {
                                        trk->suppressUntil = now + kRemoteSuppressMs;
                                        doClip = false;
                                    }
                                } else {
                                    trk->insideStreak = 0;
                                }
                                if (g_debug)
                                    Log("remote pin overruled: guid %08X jumped %.2f depth %.2f streak %d%s",
                                        remLo, sqrtf(jx * jx + jy * jy),
                                        sd < 1e8f ? -sd : 0.0f, trk->insideStreak,
                                        doClip ? "" : " -> raw");
                            }
                        }
                    }
                    if (doClip)
                        ClipDelta(me, px, py, pz, isLocal, &dx, &dy);
                    localMover = isLocal;
                    clipped = (dx != odx) || (dy != ody);
                    shortx = odx - dx;               // what we removed
                    shorty = ody - dy;

                    // Remember where our clip left every pinned remote, so the
                    // next call can run the packet-overrule check above.
                    if (clipped && !isLocal) {
                        DWORD now = GetTickCount();
                        if (!trk)
                            trk = TrackAlloc(remLo, remHi, now);
                        if (trk) {
                            trk->ex = px + dx;
                            trk->ey = py + dy;
                            trk->lastSeen = now ? now : 1;
                        }
                    }
                    if (g_debug) {
                        static DWORD s_l = 0;
                        DWORD n = GetTickCount();
                        if (n - s_l >= 100) {
                            s_l = n;
                            // dist tells us whether we were even considered "in
                            // contact"; |in|/|out| shows whether the damping bit.
                            float b2 = 1e18f, hx = 0, hy = 0;
                            float dz = *(float*)((BYTE*)self + kOff_CMovementPos + 8);
                            int found = FindNearestBlocker(me, px, py, dz, 99.0f, &b2, &hx, &hy);
                            float dist = found ? sqrtf(b2) : -1.0f;
                            Log("clip dist=%.2f contact<=%.2f in=%.4f out=%.4f "
                                "d=(%.4f,%.4f)->(%.4f,%.4f)",
                                dist, 2.0f * g_radius + g_contactBand,
                                sqrtf(odx*odx + ody*ody), sqrtf(dx*dx + dy*dy),
                                odx, ody, dx, dy);
                        }
                    }
                }
            }
        }
    }
    int r = g_origClip(self, a1, a2, dx, dy, dz);

    // Clamping dz is necessary but not sufficient: the ground resolution INSIDE
    // that call snaps Z to the terrain, which knows nothing about a body being
    // in the way, so it drags us straight back down. Re-assert the support
    // height afterwards. Bounded to a small correction so this can never fight a
    // real teleport, a knockback or falling off the edge of the surface.
    if (supported) {
        float* pz = (float*)((BYTE*)self + kOff_CMovementPos + 8);
        float drop = supportZ - *pz;
        if (drop > 0.0f && drop < 2.0f) {
            *pz = supportZ;
            if (g_debug) {
                static DWORD s_l = 0;
                DWORD n = GetTickCount();
                if (n - s_l >= 250) {
                    s_l = n;
                    Log("stand: re-asserted z +%.3f -> %.2f (terrain snap pulled us off)",
                        drop, supportZ);
                }
            }
        }
    }

    // The movement target is [CMovement+0x4C] + delta, so holding the player back
    // without touching +0x4C lets the shortfall accumulate and discharge as a
    // teleport once the obstruction clears.
    //
    // Overwriting +0x4C with the real position made things dramatically WORSE
    // (tried and reverted) - which is itself informative: it confirms +0x4C really
    // is part of the integration, and that the write was mistimed. The position at
    // +0x10 has not been committed for this frame yet when we run, so that was
    // pinning the base to a stale position every frame.
    //
    // This instead subtracts exactly what we refused to move, which needs no
    // knowledge of when the position is committed. OFF by default: it changes
    // client state, so it stays opt-in until measured.
    // Local player only: [CMovement+0x4C] is the base our own input advances
    // from. For a remote unit that base is driven by network updates, so editing
    // it corrupts their interpolation instead of correcting it.
    if (clipped && localMover && g_syncPredicted) {
        float* pred = (float*)((BYTE*)self + kOff_Predicted);
        pred[0] -= (shortx);
        pred[1] -= (shorty);
    }
    return r;
}

// ---- probe 3: which Collide.cpp entry runs during movement? ----------------
//
// CWorld::Intersect is now ruled out entirely (its only call sites are camera and
// terrain picking, even when walking into terrain). Player movement must use the
// Collide.cpp system at ~0x75F000-0x761000. These stubs only COUNT calls, and are
// written naked so they work regardless of each function's calling convention -
// getting an argument count wrong here would corrupt the stack.
static BYTE* g_trampA = NULL;   // 0x75F0A0
static BYTE* g_trampB = NULL;   // 0x75FF90
static BYTE* g_trampC = NULL;   // 0x760720

static const BYTE kCollide9Sig[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC };  // + size dword

__declspec(naked) static void ProbeA() { __asm { inc dword ptr g_cA
                                                jmp dword ptr g_trampA } }
__declspec(naked) static void ProbeB() { __asm { inc dword ptr g_cB
                                                jmp dword ptr g_trampB } }
__declspec(naked) static void ProbeC() { __asm { inc dword ptr g_cC
                                                jmp dword ptr g_trampC } }

// ---- probe 2: is 0x7139E0 the player's movement collider? ------------------
//
// Collide.cpp lives at ~0x75F000-0x761000 and is a system SEPARATE from
// CWorld::Intersect (which the probe proved is only camera/picking - no per-frame
// movement segment ever goes through it). 0x7139E0 loops a candidate list calling
// the Collide.cpp primitive 0x760720 for each, which is the shape of a movement
// collider. It is virtual, so identity has to be measured: log whether its `this`
// is the active player's CMovement (unit+0x788).
static const DWORD kCollideSite   = 0x007139E0;
static const BYTE  kCollideSig[]  = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00 };
static const DWORD kCollideResume = 0x007139E9;

typedef int (__thiscall *CollideFn)(void* self, void* a1, void* a2);
static BYTE*     g_trampCollide = NULL;
static CollideFn g_origCollide  = NULL;

static int __fastcall CollideWrapper(void* self, void* /*edx*/, void* a1, void* a2)
{
    ++g_cCollide;
    void* mine = NULL;
    unsigned __int64 guid = pGetActiveGuid();
    if (guid) {
        void* me = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnitOrPlayer);
        if (me)
            mine = (BYTE*)me + kOff_Movement;         // player's CMovement
    }
    if (self == mine)
        ++g_cCollideMine;

    int r = g_origCollide(self, a1, a2);

    if (g_probe) {
        static DWORD s_last = 0;
        DWORD now = GetTickCount();
        if (now - s_last >= 1000) {
            s_last = now;
            Log("collide n=%u mine=%u this=%p playerMovement=%p",
                g_cCollide, g_cCollideMine, self, mine);
        }
    }
    return r;
}

typedef float (__thiscall *GroundFn)(void* unit);
static BYTE*    g_trampGround = NULL;
static GroundFn g_origGround  = NULL;

static float __fastcall GroundWrapper(void* self, void* /*edx*/)
{
    float d = g_origGround(self);
    ++g_cGround;

    // Report the stage counts even when we bail, so "never fired" can be told
    // apart from "fired but never for us" and "for us but found nothing".
    if (g_debug) {
        static DWORD s_rep = 0;
        DWORD now = GetTickCount();
        if (now - s_rep >= 1000) {
            s_rep = now;
            Log("ground probe: calls=%u mine=%u candidates=%u",
                g_cGround, g_cGroundMine, g_cGroundCand);
        }
    }

    if (!g_standOnPlayers || !self || !AnyCollisionSpell())
        return d;

    // Local player only for now: a remote unit's Z comes from the server, so
    // inventing a floor under it would just fight the next update.
    unsigned __int64 guid = pGetActiveGuid();
    if (!guid)
        return d;
    void* me = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnitOrPlayer);
    if (me != self)
        return d;
    ++g_cGroundMine;

    float px = *(float*)((BYTE*)self + kOff_PosX);
    float py = *(float*)((BYTE*)self + kOff_PosY);
    float pz = *(float*)((BYTE*)self + kOff_PosZ);

    StandCtx c;
    c.self = self; c.px = px; c.py = py; c.pz = pz;
    c.groundZ = pz - d;                              // absolute terrain height
    c.radius = 2.0f * g_radius;
    c.bestTop = -1e9f; c.hit = 0;
    pEnumVisible(StandCb, &c);
    if (!c.hit)
        return d;
    ++g_cGroundCand;

    float newDist = pz - c.bestTop;
    if (newDist < 0.0f)
        newDist = 0.0f;

    if (g_debug) {
        static DWORD s_last = 0;
        DWORD now = GetTickCount();
        if (now - s_last >= 250) {
            s_last = now;
            Log("stand: pz=%.2f terrain=%.2f bodyTop=%.2f dist %.2f -> %.2f",
                pz, c.groundZ, c.bestTop, d, newDist);
        }
    }
    return newDist;
}

// ------------------------------------------------------------- the detours
//
// Each wrapper runs the original function FIRST (committing that path's movement),
// THEN ejects. Running last is the whole point - a pre-commit eject gets
// overwritten by the movement step. The wrappers sit at the function call boundary
// on both sides, where the x87 stack is empty by convention, so there is no FPU
// state to hand-preserve (unlike a mid-function hook). __fastcall(self, edx, arg)
// matches the __thiscall(self, arg) ret-4 ABI: self in ecx, arg on the stack.
typedef void (__thiscall *TickFn)(void* self, int arg);
static BYTE*  g_trampMove = NULL;   static TickFn g_origMove = NULL;

// Movement physics step: __thiscall(this, float dt). Hooked purely to mark that we
// are inside movement collision, so the sweep hook leaves camera/LoS alone.
typedef int (__thiscall *PhysFn)(void* self, float dt);
static BYTE*  g_trampPhys = NULL;   static PhysFn g_origPhys = NULL;




static void __fastcall MoveWrapper(void* self, void* /*edx*/, int arg)
{
    // NOTE: do NOT re-assert our position before the mover. Other stages (notably
    // the object-loop update 0x734390) legitimately move the player between our
    // correction and this call, so undoing that drift fights real movement every
    // frame - it shows up as constant snapping. Tried, reverted.
    g_origMove(self, arg);
    PlayerCollide_OnTick(self);      // clamp only after the mover has moved us
}

// ------------------------------------------------------------- install/verify
static bool Readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
        return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & ok) || (mbi.Protect & PAGE_GUARD))
        return false;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

// ============== remote ghost clamp: the integrator's raw-commit arm ==========
//
// The per-substep position integrator (0x6E9E20) routes a mover through the
// collision clip 0x762E00 - our main hook - ONLY while [CMovement+0xBC] == 0.
// Once a remote player completes ANY spline on this client (charge, knockback,
// taxi, or the client's own movement-stop smoothing spline), the finished
// spline object is freed only for the LOCAL player (the spline-done routine
// guards the free with a guid compare at 0x6EAECB); for remotes it lingers
// until a teleport or re-create. From then on EVERY dead-reckoning, heartbeat
// chord-blend and spline substep for that unit takes the integrator's OTHER
// arm - raw stores at 0x6E9EA0/A5/AC into CMovement+0x10..0x18 - bypassing the
// clip hook entirely. That unhooked writer is what painted orbiting players
// INSIDE bodies while their own packets sat on the ring (two-client log,
// 2026-08-13), and why collision degraded per-unit per-session: the trap arms
// the first time a unit splines, and everything after that is invisible to us.
//
// This detour sits at 0x6E9E91: after the arm decision (the local player can
// never reach it - its guid compare routes it to the clip arm unconditionally),
// after the x87 stack is emptied (both inbound flows pass the fstp x3), and
// before the candidate position is loaded from the frame slots and committed.
// eax/ecx/edx/edi are dead at the site (each is rewritten before use). We
// clamp [ebp-0xC..-4] in place, so the original code commits the clamped
// values and the transport transform downstream re-derives from them.
static const DWORD kRawSite   = 0x006E9E91;
static const BYTE  kRawSig[]  = { 0x8B,0x4D,0xF4, 0x8B,0x55,0xF8 };  // mov ecx,[ebp-0xC]; mov edx,[ebp-8]
static const DWORD kRawResume = 0x006E9E97;
// Wider identity check spanning the branch, the rebase call, the site and the
// commit prologue - guards against a lookalike byte pair elsewhere.
static const DWORD kRawCtx    = 0x006E9E88;
static const BYTE  kRawCtxSig[] = { 0x75,0x07, 0x8B,0xCE, 0xE8,0xFF,0xE5,0x29,0x00,
                                    0x8B,0x4D,0xF4, 0x8B,0x55,0xF8, 0x8B,0x7D,0x0C,
                                    0x01,0x7E,0x60 };
static BYTE*    g_trampRaw = NULL;
static unsigned g_cRaw = 0, g_cRawClamped = 0;

static void __cdecl RemoteRawCommitFilter(void* mv, float* newPos)
{
    ++g_cRaw;
    if (!g_enabled || !g_native || !g_clipRemotes || !AnyCollisionSpell())
        return;

    // ACTIVE knockback arcs (parabolic/falling splines) keep the raw path,
    // mirroring the client's own 0x200 special case inside this arm. A
    // FINISHED spline (0x400) that once was a knockback keeps 0x200 set
    // forever, and exempting it would trap the unit uncollided permanently -
    // exactly the lingering-spline hole this hook exists to close.
    DWORD spline = *(DWORD*)((BYTE*)mv + 0xBC);
    if (spline && (*(DWORD*)(spline + 0x20) & 0x600) == 0x200)
        return;

    // Same mover-identity proof as ClipWrapper.
    BYTE* mover = (BYTE*)mv - kOff_Movement;
    if (!Readable(mover + kOff_GuidLow, 8))
        return;
    DWORD lo = *(DWORD*)(mover + kOff_GuidLow);
    DWORD hi = *(DWORD*)(mover + kOff_GuidHigh);
    if ((hi & 0xF0000000u) != 0)                     // creature/pet: server-owned
        return;
    if (pObjectPtr(lo, hi, kTypeMaskUnitOrPlayer) != (void*)mover)
        return;
    unsigned __int64 guid = pGetActiveGuid();
    if (guid && pObjectPtr((DWORD)guid, (DWORD)(guid >> 32),
                           kTypeMaskUnitOrPlayer) == (void*)mover)
        return;      // structurally impossible on this arm; guard regardless

    float px = *(float*)((BYTE*)mv + kOff_CMovementPos);
    float py = *(float*)((BYTE*)mv + kOff_CMovementPos + 4);
    float pz = *(float*)((BYTE*)mv + kOff_CMovementPos + 8);

    // Shared pin history with ClipWrapper - same table, same v4 semantics, so
    // a unit that acquires or finishes a spline mid-contact hands off between
    // the two arms without losing its state.
    RemoteTrack* trk = TrackFind(lo, hi);
    DWORD now = GetTickCount();
    if (trk && (int)(trk->suppressUntil - now) > 0)
        return;                                      // packet-disproven pin: raw
    if (trk && (DWORD)(now - trk->lastSeen) < 250) {
        float jx = px - trk->ex, jy = py - trk->ey;
        if (jx * jx + jy * jy > kRemoteSnapYd * kRemoteSnapYd) {
            float b2 = 1e18f, hx = 0.0f, hy = 0.0f;
            float sd = 1e9f;
            if (FindNearestBlocker(mover, px, py, pz,
                                   2.0f * g_radius + 0.5f, &b2, &hx, &hy)) {
                float nx, ny;
                sd = OctagonDist(px, py, hx, hy, 2.0f * g_radius, &nx, &ny);
            }
            // Same two-consecutive rule as ClipWrapper (shared streak state).
            bool release = false;
            if (sd < -kInsideDepthYd) {
                if (++trk->insideStreak >= 2) {
                    trk->suppressUntil = now + kRemoteSuppressMs;
                    release = true;
                }
            } else {
                trk->insideStreak = 0;
            }
            if (g_debug)
                Log("remote pin overruled (raw arm): guid %08X jumped %.2f depth %.2f streak %d%s",
                    lo, sqrtf(jx * jx + jy * jy),
                    sd < 1e8f ? -sd : 0.0f, trk->insideStreak,
                    release ? " -> raw" : "");
            if (release)
                return;
        }
    }

    // The clamp. Same delta convention as the clip arm uses: re-anchored to
    // the CURRENT position (+0x10), not the integration base.
    float dx = newPos[0] - px, dy = newPos[1] - py;
    float odx = dx, ody = dy;
    ClipDelta(mover, px, py, pz, /*isLocal=*/false, &dx, &dy);
    if (dx != odx || dy != ody) {
        newPos[0] = px + dx;
        newPos[1] = py + dy;
        ++g_cRawClamped;
        if (!trk)
            trk = TrackAlloc(lo, hi, now);
        if (g_debug) {
            static DWORD s_l = 0;
            if (now - s_l >= 250) {
                s_l = now;
                Log("rawclamp: guid %08X d=(%.3f,%.3f)->(%.3f,%.3f) raw=%u clamped=%u",
                    lo, odx, ody, dx, dy, g_cRaw, g_cRawClamped);
            }
        }
    }
    if (trk) {
        trk->ex = newPos[0];
        trk->ey = newPos[1];
        trk->lastSeen = now ? now : 1;
    }
}

__declspec(naked) static void RawCommitStub()
{
    __asm {
        pushad                        // eax/ecx/edx/edi are dead at the site; insurance
        lea  eax, [ebp-0xC]           // candidate {x,y,z} in the integrator's frame
        push eax
        push esi                      // CMovement*
        call RemoteRawCommitFilter
        add  esp, 8
        popad
        jmp  dword ptr [g_trampRaw]   // stolen 6 bytes + jmp back to 0x6E9E97
    }
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

// Build a trampoline (stolen bytes + jmp back to `resume`) and overwrite `site`
// with a jmp to `wrapper`. Both hook sites go through here.
static bool InstallDetour(DWORD site, const BYTE* sig, size_t siglen, DWORD resume,
                          void* wrapper, BYTE** outTramp, void** outOrig, const char* name)
{
    BYTE* tramp = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        Log("trampoline VirtualAlloc failed for %s - disabled", name);
        return false;
    }
    memcpy(tramp, (void*)site, siglen);                 // stolen prologue
    tramp[siglen] = 0xE9;                               // jmp rel32 back
    *(DWORD*)(tramp + siglen + 1) = resume - ((DWORD)tramp + siglen + 5);
    FlushInstructionCache(GetCurrentProcess(), tramp, 32);
    *outTramp = tramp;
    *outOrig  = (void*)tramp;

    BYTE* p = (BYTE*)site;
    DWORD old = 0;
    if (!VirtualProtect(p, siglen, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed at 0x%08X (%s) - disabled", site, name);
        return false;
    }
    memset(p, 0x90, siglen);                            // jmp rel32 (5) + nop pad
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)wrapper - (site + 5);
    VirtualProtect(p, siglen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, siglen);
    return true;
}

void PlayerCollide_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir);

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;                                      // no ini => stays OFF (default)

    g_enabled   = GetPrivateProfileIntA("PlayerCollide", "Enabled", 1, ini);
    g_spellId   = GetPrivateProfileIntA("PlayerCollide", "SpellId", 0, ini);
    g_bothNeed  = GetPrivateProfileIntA("PlayerCollide", "BothNeedBuff", 1, ini);
    g_native    = GetPrivateProfileIntA("PlayerCollide", "Native", 1, ini);
    g_probe     = GetPrivateProfileIntA("PlayerCollide", "ProbeIntersect", 0, ini);
    g_debug     = GetPrivateProfileIntA("PlayerCollide", "Debug", 0, ini);
    g_showErrors= GetPrivateProfileIntA("PlayerCollide", "ShowErrors", 0, ini);
    char buf[64];
    GetPrivateProfileStringA("PlayerCollide", "Radius", "0.75", buf, sizeof(buf), ini);
    g_radius = (float)atof(buf);
    if (g_radius < 0.05f) g_radius = 0.05f;
    if (g_radius > 5.0f)  g_radius = 5.0f;

    GetPrivateProfileStringA("PlayerCollide", "PushMargin", "6.0", buf, sizeof(buf), ini);
    g_pushMargin = (float)atof(buf);
    if (g_pushMargin < 0.5f)  g_pushMargin = 0.5f;
    if (g_pushMargin > 50.0f) g_pushMargin = 50.0f;

    g_spellBlockEnemies   = GetPrivateProfileIntA("PlayerCollide", "SpellBlockEnemies", 90210, ini);
    g_spellBlockAll       = GetPrivateProfileIntA("PlayerCollide", "SpellBlockAll", 90211, ini);
    g_spellCollideEnemies = GetPrivateProfileIntA("PlayerCollide", "SpellCollideEnemies", 90212, ini);
    g_spellCollideAll     = GetPrivateProfileIntA("PlayerCollide", "SpellCollideAll", 90213, ini);
    g_syncPredicted = GetPrivateProfileIntA("PlayerCollide", "SyncPredicted", 1, ini);
    g_clipRemotes   = GetPrivateProfileIntA("PlayerCollide", "ClipRemotes", 1, ini);
    g_npcBlockers   = GetPrivateProfileIntA("PlayerCollide", "NpcBlockers", 1, ini);
    g_standOnPlayers = GetPrivateProfileIntA("PlayerCollide", "StandOnPlayers", 0, ini);
    GetPrivateProfileStringA("PlayerCollide", "StandHeight", "1.2", buf, sizeof(buf), ini);
    g_standHeight = (float)atof(buf);
    if (g_standHeight < 0.5f) g_standHeight = 0.5f;
    if (g_standHeight > 6.0f) g_standHeight = 6.0f;
    g_sides = GetPrivateProfileIntA("PlayerCollide", "Sides", 0, ini);
    if (g_sides != 0 && g_sides != 4)                // 0 = circle, 4 = square,
        g_sides = 8;                                 // anything else = octagon

    GetPrivateProfileStringA("PlayerCollide", "Height", "1.4", buf, sizeof(buf), ini);
    g_height = (float)atof(buf);
    if (g_height < 0.1f)  g_height = 0.1f;
    if (g_height > 20.0f) g_height = 20.0f;

    GetPrivateProfileStringA("PlayerCollide", "ContactBand", "0.0", buf, sizeof(buf), ini);
    g_contactBand = (float)atof(buf);
    if (g_contactBand < 0.0f) g_contactBand = 0.0f;
    if (g_contactBand > 3.0f) g_contactBand = 3.0f;

    GetPrivateProfileStringA("PlayerCollide", "SlideFriction", "0.5", buf, sizeof(buf), ini);
    g_slideFriction = (float)atof(buf);
    if (g_slideFriction < 0.0f) g_slideFriction = 0.0f;
    if (g_slideFriction > 1.0f) g_slideFriction = 1.0f;

    GetPrivateProfileStringA("PlayerCollide", "SeparateSpeed", "3.0", buf, sizeof(buf), ini);
    g_separateSpeed = (float)atof(buf);
    if (g_separateSpeed < 0.25f) g_separateSpeed = 0.25f;
    if (g_separateSpeed > 20.0f) g_separateSpeed = 20.0f;
}

void PlayerCollide_Install()
{
    if (!g_enabled)
        return;
    if (!AnyCollisionSpell()) {
        Log("Enabled=1 but no spell ids configured - nothing to gate on, disabled");
        g_enabled = 0;
        return;
    }

    bool ok =
        (g_native ? VerifySig(kClipSite, kClipSig, sizeof(kClipSig), "movement clip")
                  : VerifySig(kMoveSite, kMoveSig, sizeof(kMoveSig), "local-player update")) &&
        VerifySig(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActivePlayerGuid") &&
        VerifySig(kObjectPtr,     kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr") &&
        VerifySig(kEnumVisible,   kEnumSig,      sizeof(kEnumSig),      "EnumVisibleObjects") &&
        VerifySig(kGetAuraCount,  kAuraCountSig, sizeof(kAuraCountSig), "GetAuraCount") &&
        VerifySig(kGetAuraInfo,   kAuraInfoSig,  sizeof(kAuraInfoSig),  "GetAuraInfo") &&
        VerifySig(kGetSpeed,      kGetSpeedSig,  sizeof(kGetSpeedSig),  "GetCurrentSpeed") &&
        VerifySig(kGetReaction,   kGetReactionSig, sizeof(kGetReactionSig), "GetReaction");
    if (!ok) {
        g_enabled = 0;
        if (g_showErrors)
            MessageBoxA(NULL, "PlayerCollide disabled: a client signature did not match.\n"
                              "See AnimSpeedFix.log. This build targets Wow.exe 3.3.5a 12340.",
                        "AnimSpeedFix", MB_OK | MB_ICONWARNING);
        return;
    }

    pGetActiveGuid = (GetActiveGuid_t)kGetActiveGuid;
    pObjectPtr     = (ObjectPtr_t)kObjectPtr;
    pEnumVisible   = (EnumVisible_t)kEnumVisible;
    pGetAuraCount  = (GetAuraCount_t)kGetAuraCount;
    pGetAuraInfo   = (GetAuraInfo_t)kGetAuraInfo;
    pGetSpeed      = (GetSpeed_t)kGetSpeed;
    pGetReaction   = (GetReaction_t)kGetReaction;

    if (g_probe) {
        if (VerifySig(kIntersectSite, kIntersectSig, sizeof(kIntersectSig), "CWorld::Intersect") &&
            InstallDetour(kIntersectSite, kIntersectSig, sizeof(kIntersectSig),
                          kIntersectResume, IntersectWrapper, &g_trampIsect,
                          (void**)&g_origIsect, "CWorld::Intersect")) {
            Log("probe armed on CWorld::Intersect");
        } else {
            g_probe = 0;
        }
        // Count-only probes on the Collide.cpp entries (9-byte prologues verified).
        {
            struct { DWORD site; void* stub; BYTE** tramp; const char* name; } probes[] = {
                { 0x0075F0A0, (void*)ProbeA, &g_trampA, "collide 75F0A0" },
                { 0x0075FF90, (void*)ProbeB, &g_trampB, "collide 75FF90" },
                { 0x00760720, (void*)ProbeC, &g_trampC, "collide 760720" },
            };
            for (int i = 0; i < 3; ++i) {
                if (!Readable((void*)probes[i].site, 9) ||
                    memcmp((void*)probes[i].site, kCollide9Sig, sizeof(kCollide9Sig)) != 0) {
                    Log("%s: prologue mismatch, probe skipped", probes[i].name);
                    continue;
                }
                void* dummy = NULL;
                InstallDetour(probes[i].site, (const BYTE*)probes[i].site, 9,
                              probes[i].site + 9, probes[i].stub, probes[i].tramp,
                              &dummy, probes[i].name);
            }
        }

        if (VerifySig(kCollideSite, kCollideSig, sizeof(kCollideSig), "movement collider") &&
            InstallDetour(kCollideSite, kCollideSig, sizeof(kCollideSig), kCollideResume,
                          CollideWrapper, &g_trampCollide, (void**)&g_origCollide,
                          "movement collider")) {
            Log("probe armed on 0x7139E0 (candidate movement collider)");
        }
    }

    if (g_native) {
        // Native mode: clip the movement delta before the client applies it, so an
        // illegal position is never produced and nothing needs correcting after.
        if (!VerifySig(kClipSite, kClipSig, sizeof(kClipSig), "movement clip") ||
            !InstallDetour(kClipSite, kClipSig, sizeof(kClipSig), kClipResume,
                           ClipWrapper, &g_trampClip, (void**)&g_origClip,
                           "movement clip")) {
            g_enabled = 0;
            return;
        }

        // Second arm of the same integrator: the raw-commit path that bypasses
        // the clip once a remote's finished spline lingers (see kRawSite).
        // Optional - a mismatch means finished-spline remotes render raw
        // exactly as they did before this hook existed, never fatal.
        if (g_clipRemotes) {
            void* dummy = NULL;
            if (VerifySig(kRawCtx, kRawCtxSig, sizeof(kRawCtxSig),
                          "integrator raw-commit (context)") &&
                InstallDetour(kRawSite, kRawSig, sizeof(kRawSig), kRawResume,
                              RawCommitStub, &g_trampRaw, &dummy,
                              "integrator raw-commit"))
                Log("remote ghost clamp ARMED (raw-commit arm of the integrator)");
            else
                Log("remote ghost clamp OFF - finished-spline remotes render raw as before");
        }
    } else {
        // Legacy mode: correct the position after the mover has produced an
        // illegal one. Kept only for A/B comparison; see the design doc for why
        // it can never be fully reliable.
        if (!InstallDetour(kMoveSite, kMoveSig, sizeof(kMoveSig), kMoveResume,
                           MoveWrapper, &g_trampMove, (void**)&g_origMove,
                           "local-player update")) {
            g_enabled = 0;
            return;
        }
    }

    // Install-gated: with StandOnPlayers off the ground hook is never applied,
    // so this cannot affect anything for players who do not opt in.
    if (g_standOnPlayers) {
        if (VerifySig(kGroundSite, kGroundSig, sizeof(kGroundSig), "ground query") &&
            InstallDetour(kGroundSite, kGroundSig, sizeof(kGroundSig), kGroundResume,
                          GroundWrapper, &g_trampGround, (void**)&g_origGround,
                          "ground query")) {
            Log("stand-on-players ARMED (experimental), standHeight=%.2f", g_standHeight);
        } else {
            g_standOnPlayers = 0;
            Log("stand-on-players: could not hook the ground query - disabled");
        }
    }

    g_installed = 1;
    Log("installed (%s): spells legacy=%d blockEnemies=%d blockAll=%d "
        "collideEnemies=%d collideAll=%d | radius=%.2f shape=%s height=%.1f band=%.2f "
        "friction=%.2f mode=%s", g_native ? "native" : "legacy", g_spellId,
        g_spellBlockEnemies, g_spellBlockAll, g_spellCollideEnemies, g_spellCollideAll,
        g_radius,
        g_sides == 0 ? "circle" : (g_sides == 4 ? "square" : "octagon"),
        g_height, g_contactBand, g_slideFriction,
        g_bothNeed ? "both-need-buff" : "either-has-buff");
}

// Test hooks (selftest.exe): drive the collision math without the game.
extern "C" __declspec(dllexport)
void PlayerCollide_TestConfig(int spellId, float radius, int bothNeed)
{
    g_spellId = spellId; g_radius = radius; g_bothNeed = bothNeed; g_enabled = 1;
}

// Pure math check used by the selftest: eject out of the given neighbours, capped
// to maxStep, and report the resulting position. Calls the exact same helpers
// ResolveActive uses, so the test covers the shipped math (geometry + the cap).
extern "C" __declspec(dllexport)
void PlayerCollide_TestResolve(float sx, float sy, const float* others, int nOthers,
                               float facing, float maxStep, float* outX, float* outY)
{
    float ex, ey;
    ComputeEjection(sx, sy, others, nOthers, facing, 2.0f * g_radius, &ex, &ey);
    CapStep(&ex, &ey, maxStep);
    *outX = sx + ex; *outY = sy + ey;
}

extern "C" __declspec(dllexport)
int PlayerCollide_TestStatus() { return g_installed; }
