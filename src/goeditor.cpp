// goeditor.cpp - client gameobject editor, phase 1: cursor->world pick diagnostic.
// Target: Wow.exe 3.3.5a build 12340 (imagebase 0x400000, x86).
//
// Detours CWorld::Intersect (real fn 0x7A3B70; 0x77F310 is a 5-byte thunk to it)
// and, for every call, logs the CALLER return address, the ray (start/end vectors)
// and the flags word. Enable [GOEditorProbe] Enabled=1 in AnimSpeedFix.ini, get in
// game, hover the mouse slowly over flat ground while panning the camera
// separately: the cursor-pick caller is the one whose `end` vector tracks the
// mouse. The world hit point is start + t*(end-start), where t is the in/out
// fraction (arg 4) - so that one caller becomes GetCursorWorldPos.
//
// Read-only, and integer-only in the hook (pushad/pushfd do NOT save the FPU and
// the caller is x87-heavy) - vectors are logged as raw 32-bit hex and decoded
// offline: python -> struct.unpack('<f', struct.pack('<I', int(h, 16)))[0].
// Fails safe: a byte-signature mismatch on this build logs a note and installs
// nothing. OFF unless [GOEditorProbe] Enabled=1. See docs/GOEDITOR_CLIENT_RE.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <math.h>

#include "goeditor.h"

static const DWORD kSite   = 0x007A3B70;                 // CWorld::Intersect (real)
static const BYTE  kSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18 };
static const DWORD kResume = 0x007A3B76;                 // kSite + sizeof(kSig)

static int              g_enabled = 0;
static DWORD            g_throttleMs = 40;               // per-caller log rate limit
static char             g_dir[MAX_PATH];
static CRITICAL_SECTION g_lock;
static int              g_lockInit = 0;

#define GOE_BUF 0x8000
static char g_buf[GOE_BUF + 256];
static int  g_len = 0;

// Per-caller throttle so the ~21 call sites (many per frame) don't flood the log.
#define GOE_SLOTS 64
static DWORD g_caller[GOE_SLOTS];
static DWORD g_lastTick[GOE_SLOTS];

// --- optional one-shot GameObject memory dump (pin the GO position field) ------
// EnumVisibleObjects -> ObjectPtr(guid, 0x20 = GAMEOBJECT) -> hex-dump each GO.
// Runs once, a few seconds after the first world pick (so GOs are loaded),
// triggered from the Intersect hook - no extra detour (PlayerCollide already owns
// the per-frame hook). Correlate a dumped GO's float triple with its known world
// coords to find the position offset (and the nearby transform matrix). Addresses
// + signatures are the ones already verified by the collision module.
static const DWORD kEnumVisible     = 0x004D4B30;
static const BYTE  kEnumSig[]       = { 0x55, 0x8B, 0xEC, 0xA1, 0xBC, 0x39, 0xD4, 0x00 };
static const DWORD kObjectPtr       = 0x004D4DB0;
static const BYTE  kObjectPtrSig[]  = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C };
static const DWORD kTypeMaskGO      = 0x20;              // verified: GO fn calls ObjectPtr with mask 0x20

typedef int   (__cdecl* EnumCb_t)(DWORD guidLo, DWORD guidHi, void* arg);
typedef int   (__cdecl* EnumVisible_t)(EnumCb_t cb, void* arg);
typedef void* (__cdecl* ObjectPtr_t)(DWORD guidLo, DWORD guidHi, DWORD typeMask);
static EnumVisible_t pEnumVisible = 0;
static ObjectPtr_t   pObjectPtr   = 0;

static int   g_dumpGO = 0;
static DWORD g_dumpDelayMs = 5000;
static int   g_goDumped = 0;
static DWORD g_installTick = 0;
static int   g_dumpCount = 0;

// --- optional write-test: does writing obj+0xE8 move a GO's rendered model? -----
// Picks the first real-world GO (guid-high 0xF110xxxx) once, then every frame
// overwrites its position (obj+0xE8) with the camera eye (the cursor pick's ray
// start - a raw 3-dword copy, no FP). If that GO visibly snaps to / follows the
// camera, the render reads position live and the render-hijack works. If it stays
// put, the transform is cached at create and we need a matrix override instead.
// Client-side only; reverts on relog.
static const DWORD kGoPosOffset = 0xE8;                  // verified C3Vector (X/Y/Z)
static int   g_moveTest   = 0;
static DWORD g_moveGuidLo = 0, g_moveGuidHi = 0;
static int   g_moveFound  = 0;

static int g_diag = 0;   // master switch for ALL diagnostic logging (off by default)

static void Note(const char* what)
{
    if (!g_diag) return;                              // diagnostics disabled -> no log writes
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sgoeditor_probe.log", g_dir);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD wrote = 0;
    WriteFile(h, what, (DWORD)strlen(what), &wrote, NULL);
    CloseHandle(h);
}

// Append the ray-log buffer. Open/append/close every time - never hold the handle
// open, or Note()/the GO dump can't open the file for append (sharing violation).
static void FlushLocked()
{
    if (g_len == 0)
        return;
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sgoeditor_probe.log", g_dir);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD wrote = 0;
        WriteFile(h, g_buf, (DWORD)g_len, &wrote, NULL);
        CloseHandle(h);
    }
    g_len = 0;
}

// True if this caller may log now (rate-limited per caller). Integer-only.
static bool ThrottleOK(DWORD caller, DWORD now)
{
    unsigned slot = (caller >> 4) % GOE_SLOTS;
    for (unsigned i = 0; i < GOE_SLOTS; ++i)
    {
        unsigned s = (slot + i) % GOE_SLOTS;
        if (g_caller[s] == caller)
        {
            if (now - g_lastTick[s] < g_throttleMs)
                return false;
            g_lastTick[s] = now;
            return true;
        }
        if (g_caller[s] == 0)
        {
            g_caller[s] = caller;
            g_lastTick[s] = now;
            return true;
        }
    }
    return true; // table full: log rather than lose data
}

// Enumerator callback: dump up to 8 GameObjects' first 0x140 bytes as hex.
static int __cdecl GoDumpCb(DWORD guidLo, DWORD guidHi, void* /*arg*/)
{
    if (g_dumpCount >= 32)
        return 0;                                   // stop enumerating
    void* go = pObjectPtr(guidLo, guidHi, kTypeMaskGO);
    if (!go)
        return 1;                                   // not a GameObject, keep going
    const DWORD* m = (const DWORD*)go;
    char hdr[128];
    _snprintf_s(hdr, sizeof(hdr), _TRUNCATE, "GO obj=%08X guid=%08X:%08X\r\n",
                (DWORD)go, guidLo, guidHi);
    Note(hdr);
    for (int i = 0; i < 80; i += 4)                 // 0x140 bytes, 4 dwords/row
    {
        char row[96];
        _snprintf_s(row, sizeof(row), _TRUNCATE, "  +%03X: %08X %08X %08X %08X\r\n",
                    i * 4, m[i], m[i + 1], m[i + 2], m[i + 3]);
        Note(row);
    }
    ++g_dumpCount;
    return 1;
}

// One-shot, guarded, on the main thread from inside the Intersect hook. Read-only.
static void MaybeDumpGOs()
{
    if (!g_dumpGO || g_goDumped || !pEnumVisible || !pObjectPtr)
        return;
    if (GetTickCount() - g_installTick < g_dumpDelayMs)
        return;
    g_goDumped = 1;                                 // set before enumerating (no re-entry)
    Note("# GO dump: EnumVisibleObjects + ObjectPtr mask 0x20. Match a GO's float\r\n"
         "# triple against its known world coords (target it, .gps) for the pos offset.\r\n");
    pEnumVisible(GoDumpCb, 0);
    if (g_dumpCount == 0)
        Note("# GO dump: no gameobjects were visible; will not retry this session\r\n");
}

// Enumerator: latch the first real-world GO (guid-high 0xF110xxxx) to move.
static int __cdecl FindRealGoCb(DWORD guidLo, DWORD guidHi, void* /*arg*/)
{
    if (g_moveFound)
        return 0;
    if ((guidHi & 0xFFF00000) != 0xF1100000)
        return 1;                                   // not a world GO guid, keep going
    if (!pObjectPtr(guidLo, guidHi, kTypeMaskGO))
        return 1;
    g_moveGuidLo = guidLo;
    g_moveGuidHi = guidHi;
    g_moveFound = 1;
    char msg[112];
    _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                "movetest: pinning GO guid=%08X:%08X to the camera each frame\r\n", guidLo, guidHi);
    Note(msg);
    return 0;
}

// eye = the cursor pick's ray start (3 raw float dwords). Raw copy, no FP.
static void MoveTestTick(const DWORD* eye)
{
    if (!g_moveTest || !pEnumVisible || !pObjectPtr || !eye)
        return;
    if (!g_moveFound)
    {
        pEnumVisible(FindRealGoCb, 0);
        if (!g_moveFound)
            return;
    }
    void* go = pObjectPtr(g_moveGuidLo, g_moveGuidHi, kTypeMaskGO);
    if (!go)
        return;
    DWORD* pos = (DWORD*)((BYTE*)go + kGoPosOffset);
    pos[0] = eye[0];                                // X
    pos[1] = eye[1];                                // Y
    pos[2] = eye[2];                                // Z
}

// =====================================================================
// [SummonPreview] - while aiming a ground-target SUMMON spell, render a
// translucent preview of the summoned creature's model at the cursor.
// Fully client-side (nothing sent to the server until you click to cast).
// All addresses/offsets verified against Wow.exe 12340 (see the project
// memory + docs). Runs from the Intersect hook each frame; the stub now
// fnsave/frstor's the FPU around the dispatch so the client model calls
// below (which use x87) are safe.
// =====================================================================
static int   g_summonEnabled = 0;
static int   g_summonArmed   = 0;                   // sigs verified at install
static void* g_previewModel  = 0;                   // live CM2Model* (0 = none)
static int   g_previewSpell  = 0;                   // spell id the model is for
BYTE         g_fpuSave[108];                         // referenced by the naked stub

// verified callables (byte-sig checked at install)
static const DWORD kCreateModel    = 0x0081F8F0;    // __thiscall(mgr, path, 0)->CM2Model*
static const BYTE  kCreateModelSig[]= { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x85, 0xC0 };
static const DWORD kGetRecord      = 0x0067B6A0;    // __thiscall(cache,entry,buf,cb,0,0)->rec* ; cb=0 => find-only
static const BYTE  kGetRecordSig[] = { 0x55, 0x8B, 0xEC, 0x53, 0x57, 0x8B, 0x7D, 0x08 };
static const DWORD kRelease        = 0x00824ED0;    // __thiscall(model)
static const BYTE  kReleaseSig[]   = { 0x56, 0x8B, 0xF1, 0x83, 0x06, 0xFF };
static const DWORD kPlayAnim       = 0x00832AB0;    // __thiscall(m,-1,0,-1,0,1.0f,1,1)
static const DWORD kSetListed      = 0x00823F10;    // __thiscall(model, enable) - enroll in CM2 mgr pose list
static const DWORD kSetAnimCb      = 0x00823FE0;    // __thiscall(model, cb, user, x) - ORs m+0x10 |= 0x400000 (poseable)
static const DWORD kAnimNotifyCb   = 0x006F7480;    // the anim-notify callback the ground-effect spawner passes
static const DWORD kBindModel      = 0x004F20C0;    // __cdecl(model, displayInfoRec, modelDataRec)
static const BYTE  kBindModelSig[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x20, 0x01, 0x00 };
static const DWORD kBatchBuild     = 0x0083CF00;    // __thiscall(renderData) -> checks/prepares (gates [rd+8]&1)
static const BYTE  kBatchBuildSig[]= { 0x53, 0x8B, 0xD9, 0xF6, 0x43, 0x08, 0x01, 0x74 };
static const DWORD kBuildBatches   = 0x00838490;    // __thiscall(rd, arg) -> build render batches skin->geo (gates [rd+8]&2)
static const BYTE  kBuildBatchesSig[]={ 0x55, 0x8B, 0xEC, 0x57, 0x8B, 0xF9, 0xF6, 0x47 };
static const DWORD kDrain          = 0x00823ED0;    // __thiscall(m,0) -> sync-complete the model's pending async .skin load (batches then build)
static const BYTE  kDrainSig[]     = { 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x2C, 0x8B, 0x40 };
static const DWORD kCollect        = 0x00834660;    // __thiscall(model, bucketA, bucketB) -> pose(m+0xB4)+emit submeshes
static const BYTE  kCollectSig[]   = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x56, 0x8B };
static const DWORD kBuckets        = 0x00D2532C;    // scene collect bucket base (pass 0 = main view; bucketB=+0xc)
static const DWORD kCollectSite    = 0x007BBC10;    // inside the scene collect walk, right after its 0x834660 call
static const BYTE  kCollectSiteSig[]={ 0x81, 0x45, 0xF8, 0xF4, 0x00, 0x00, 0x00 };  // add dword ptr [ebp-8],0xF4 (7 bytes)
static const DWORD kCollectResume  = 0x007BBC17;    // instruction after the stolen add
static const DWORD kMainViewSite   = 0x007BD1B2;    // main COLOR view collect, right after its 0x834660 call
static const BYTE  kMainViewSig[]  = { 0x8B,0x45,0xF4, 0x8B,0x80,0xC4,0x00,0x00,0x00 }; // mov eax,[ebp-0xc]; mov eax,[eax+0xc4] (9 bytes)
static const DWORD kMainViewResume = 0x007BD1BB;    // instruction after the 9 stolen bytes
static const DWORD kLitDrawSite    = 0x007E3E03;    // main world-M2 LIT loop: call 0x829AA0 (immediate per-object body draw)
static const BYTE  kLitDrawSig[]   = { 0xE8, 0x98, 0x5C, 0x04, 0x00 };  // call 0x829AA0 (ecx = model, set at 0x7E3DFD)
// The lit BODY is drawn by an instanced loop over the model array 0xD38014 (count 0xD38054,
// max 10 raw CM2Model*). Append our preview to that list AFTER the build returns so the loop
// runs its native per-object setup (matrix-slot copy + 0x872B00/0x873900) for our model too.
static const DWORD kListAppendSite = 0x007E3AC3;    // right after `call 0x7E35F0` (list build) returns
static const BYTE  kListAppendSig[]= { 0x83, 0xC4, 0x04, 0x85, 0xC0 };  // add esp,4 ; test eax,eax (5 bytes)
static const DWORD kListEmptyJe    = 0x007E3AC8;    // native `je 0x7E3E6D` (empty early-out) - taken on the no-preview path
static const DWORD kListDrawResume = 0x007E3ACE;    // instruction AFTER the je - the draw path (append path lands here)
static const DWORD kM2List         = 0x00D38014;    // inline array of up to 10 CM2Model*
static const DWORD kM2ListCount    = 0x00D38054;    // count
// DIAGNOSTIC probe over the low-level M2 vertex-buffer bind 0x8362B0 (ecx=rd) to find the
// distinct callers that draw the workshop - i.e. the real body-draw path.
static const DWORD kDrawProbeSite  = 0x008362B0;
static const BYTE  kDrawProbeSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14 };  // push ebp; mov ebp,esp; sub esp,0x14
static const DWORD kDrawProbeResume= 0x008362B6;
// The REAL lit-body draw (empirically captured): orchestrator 0x4F8EA0 calls
// CM2Model::DrawPass 0x823CB0(model, passIndex) 4x per model; pass 0 (ret 0x4F9122) is the
// captured body path. Hook that return and re-invoke DrawPass(ourModel, 0).
static const DWORD kBodyHookSite   = 0x004F9122;
static const BYTE  kBodyHookSig[]  = { 0x83, 0x3D, 0xA4, 0x79, 0xAC, 0x00, 0x02 };  // cmp [0xac79a4],2 (7 bytes)
static const DWORD kBodyHookResume = 0x004F9129;
typedef void (__thiscall* DrawPass_t)(void* model, int passIndex);  // 0x823CB0, ret 4
// Capture the REAL create-descriptor a live creature spawn passes to the unit ctor 0x73F660,
// so we can replicate it for our local unit (instead of a zeroed buffer the render setup rejects).
static const DWORD kUnitCtorSite   = 0x0073F660;
static const BYTE  kUnitCtorSig[]  = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x83, 0xEC, 0x14 };  // push ebp;mov ebp,esp;mov eax,[ebp+0xc];sub esp,0x14
static const DWORD kUnitCtorResume = 0x0073F669;
static int         g_descDumped    = 0;
// Reticle source: inside the targeting render 0x6FD6B0, right after it calls the
// ground-target resolver 0x6FCD60 (success path, eax!=0). The resolver's DST
// out-vec (arg4) is the local [ebp-0x34]; SRC (arg3) is [ebp-0x44]. We steal 3
// clean, position-independent instrs (lea edx,[ebp-0x34]; push edx; lea eax,[ebp-0x44]).
static const DWORD kReticleSite    = 0x006FD72C;
static const BYTE  kReticleSig[]   = { 0x8D, 0x55, 0xCC, 0x52, 0x8D, 0x45, 0xBC };  // 7 bytes
static const DWORD kReticleResume  = 0x006FD733;    // kReticleSite + 7
static const DWORD kAnimate        = 0x0082F0F0;    // __thiscall(m,worldMtx,scale3,off3,a,b) CM2Model::Animate (ret 0x14)
static const BYTE  kAnimateSig[]   = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x01, 0x00 };
static const float kAnimIdent[16]  = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };  // pass identity: real xform is at m+0xB4
static const float kAnimOnes[3]    = { 1.0f, 1.0f, 1.0f };
static const float kAnimZero[3]    = { 0.0f, 0.0f, 0.0f };

// data globals (not sig-checkable)
static const DWORD kCM2MgrPtr      = 0x00CD754C;    // *(void**) = CM2 model manager
static const DWORD kCreatureCache  = 0x00C5D690;    // creature cache object (GetRecord this)
static const DWORD kSpellMgr       = 0x00AD49D0;    // Spell.dbc client DB (+0x0C max,+0x10 min,+0x20 tbl)
static const DWORD kCDImin=0x00AD34C8, kCDImax=0x00AD34C4, kCDItbl=0x00AD34D8;  // CreatureDisplayInfo
static const DWORD kCMDmin=0x00AD3510, kCMDmax=0x00AD350C, kCMDtbl=0x00AD3520;  // CreatureModelData
static const DWORD kAimCtxPtr      = 0x00D3F4E4;    // *(void**) non-null = aiming; +0x20 = spell id
static const DWORD kCursorMgrPtr   = 0x00B7436C;    // *(void**); +0x2E8 = cursor world xyz, +0x2D8 valid>=2
static const DWORD kGetActiveGuid  = 0x004D3790;    // cdecl () -> u64 active player guid (edx:eax)

// Reticle source, captured by GOEditorReticleHook right after the targeting
// render's ground-target resolver call (0x6FD6B0 -> 0x6FCD60): DST = the resolved
// world point (the same point the client sends as the cast dest, i.e. where the
// summon lands), SRC = the ray origin. Raw float bits. Written by the hook, read
// by SummonPreview_Tick. The general mouse-over (0xB7436C+0x2E8) is frozen while
// aiming, so this resolver output is the only live per-frame reticle source.
static DWORD g_reticle[3]    = {0,0,0};
static DWORD g_reticleSrc[3] = {0,0,0};
static int   g_reticleValid  = 0;
static DWORD g_reticleSeen   = 0;   // per-window capture count (diag)
static DWORD g_diagLastMs    = 0;   // throttle for the summon diag line

// CWorld::Intersect trampoline + cursor-ray hit capture. The entry hook cannot see
// the hit fraction (pFrac is written BY the call), so we WRAP the function: run the
// original, then compute the world hit = start + (*pFrac)*(end-start) for the cursor
// picks that fire while aiming. Confirmed live during aiming: 0x01020124 (the
// aiming-mode cursor->terrain pick; the ground reticle rides its hit). B/C are
// logged to confirm which flag tracks the cursor.
typedef char (__cdecl* IntersectFn_t)(const float* start, const float* end, void* hitOut,
                                      float* pFrac, int flags, int a6);
static IntersectFn_t pIntersectReal = 0;
static BYTE* g_intersectTramp = 0;
static DWORD g_hitA[3] = {0,0,0};   // flags 0x01020124 hit (placement source)
static DWORD g_hitB[3] = {0,0,0};   // flags 0x00100151 hit
static DWORD g_hitC[3] = {0,0,0};   // flags 0x00120171 hit
static int   g_hitAValid = 0;
static DWORD g_hitSeenA = 0, g_hitSeenB = 0, g_hitSeenC = 0;
static DWORD g_abMs = 0;            // throttle for the steady-state A/B matrix dump

static const DWORD ALPHA_04 = 0x3ECCCCCD;           // 0.4f bit pattern
static const DWORD ONE_F    = 0x3F800000;           // 1.0f bit pattern

typedef void* (__thiscall* CreateModel_t)(void* mgr, const char* path, int z);
// NOTE: arg4 is NOT a "requestIfMissing" bool - it is a COMPLETION-CALLBACK
// function pointer. Passing 1 made the cache store 1 as the callback and later
// `call 1` -> EIP=0x00000001 (the crash). Pass 0 for a pure find-only lookup
// (no alloc, no network, no callback): returns a record only for an
// already-cached creature, else 0.
typedef void* (__thiscall* GetRec_t)(void* cache, int entry, void* buf, void* cb, int a4, int a5);
typedef void  (__thiscall* Release_t)(void* model);
typedef void  (__thiscall* PlayAnim_t)(void* m, int a0, int seq, int a2, int a3, unsigned int spd, int a5, int a6);
typedef void  (__thiscall* SetListed_t)(void* m, int enable);
typedef void  (__thiscall* SetAnimCb_t)(void* m, void* cb, void* user, int x);
typedef void  (__cdecl*    BindModel_t)(void* model, void* displayRec, void* modelDataRec);
typedef int   (__thiscall* BatchBuild_t)(void* renderData);
typedef int   (__thiscall* BuildBatches_t)(void* renderData, void* arg);
typedef void  (__thiscall* Collect_t)(void* model, void* bucketA, void* bucketB);
typedef void  (__thiscall* Drain_t)(void* model, int zero);
typedef unsigned __int64 (__cdecl* GetActiveGuid_t)();   // 0x4D3790 active player guid
typedef void  (__cdecl*    MatMul_t)(float* dest, const float* a, const float* b);  // 0x4C1F00 dest = a*b (4x4)
typedef void  (__thiscall* MatCopy_t)(void* dst, const float* src);                 // 0x407F80 copy 16 floats
typedef void  (__thiscall* BoneCtor_t)(void* boneRec);                               // 0x82BD60 init bone-runtime rec (identity SRT + 0xFFFF)
typedef void  (__thiscall* PrepBuffers_t)(void* model);                              // 0x832EA0 build per-instance GPU vertex/index buffers

// spell id -> summoned creature_template entry (0 = not a creature summon).
static DWORD SummonEntryForSpell(int spellId)
{
    char* mgr = (char*)kSpellMgr;
    int minId = *(int*)(mgr + 0x10);
    int maxId = *(int*)(mgr + 0x0C);
    if (spellId < minId || spellId > maxId) return 0;
    void** table = *(void***)(mgr + 0x20);
    if (!table) return 0;
    char* rec = (char*)table[spellId - minId];
    if (!rec) return 0;
    for (int i = 0; i < 3; ++i)
    {
        DWORD eff = *(DWORD*)(rec + 0x11C + 4 * i);         // Effect[i]
        if (eff == 28 || eff == 56)                         // SUMMON / SUMMON_PET
            return *(DWORD*)(rec + 0x1B8 + 4 * i);          // EffectMiscValueA[i] = creature entry
    }
    return 0;
}

// creature entry -> { .mdx path, CreatureDisplayInfo rec, CreatureModelData rec }.
// Returns 0 until the creature is cached (fires one client CREATURE_QUERY once).
// The two records are exactly what BindModel (0x4f20c0) needs to attach the
// display-specific skin + geometry to a freshly created CM2Model - the step the
// first build omitted, which left an unbound model in the draw list (render AV).
static DWORD g_resolveLoggedEntry = 0;   // one breadcrumb per distinct entry (no per-frame spam)
static int   g_placeLogCount = 0;        // countdown: log the first few place attempts per model
static void* g_dispRec = 0;              // records kept for per-frame re-bind until the skin loads
static void* g_mdlRec  = 0;
static int   g_rebinds = 0;              // capped re-bind attempts
static DWORD g_lastBatch = 0xFFFFFFFF;   // last logged batch count (log on change)
static void* g_realModel = 0;            // a real, rendering model captured from the collect walk (A/B compare)
static int   g_dumped = 0;               // one-shot: dumped the preview-vs-real field comparison
static int   g_listed = 0;               // whether the preview model's one-time setup is done
static void* g_localGO = 0;              // client-only local CGUnit_C that owns the preview model
static DWORD g_workshopDisplayId = 0;    // CreatureDisplayInfo id (dispRec+0) of the CURRENTLY-aimed creature
static DWORD g_builtDisplayId    = 0;    // display id currently built into g_localGO's model (unit+0xB4)
static DWORD g_cullLogMs         = 0;    // throttle for the cull-gate diagnostic
static DWORD g_preloadedEntries[16] = {0}; // creature entries we've already issued a query for (once each)
static int   g_preloadedCount    = 0;
static void* g_lastCM2   = 0;            // last-seen CM2 manager [0xCD754C] (zone-change detection)
static void* g_lastWScene = 0;           // last-seen world scene [0xCE04A8]
static int   g_assetReresolved = 0;      // rebuilt once after the M2 asset finished streaming (submesh fix)
static float g_lastPlayerX = 0, g_lastPlayerY = 0;  // last player pos (teleport = large jump -> rebuild)
static int   g_lastPlayerValid = 0;
static DWORD g_rebuildMs   = 0;          // throttle for the submesh re-resolve retry
static int   g_rebuildTries = 0;         // cap the retries so a genuinely-bad asset can't churn forever
static DWORD g_unitCreateDesc[80] = {0}; // zeroed create-descriptor for the unit ctor (reads arg2+0x34 etc.)
static DWORD g_mtxLastFrame = 0xFFFFFFFF; // last manager frame the render matrix was written (once/frame)
static int   g_prepDumped = 0;            // one-shot: dumped GPU-buffer fields PREV vs REAL after prep
static DWORD g_preA = 0, g_preB = 0, g_postA = 0, g_postB = 0;  // bucket counts around the emit (did PREV enqueue?)
static DWORD g_mvBucketBase = 0;          // main-view hook: 0xD25320 + viewIdx*0x24 (survives PreviewPlace+emit)
static DWORD g_mvHookCount = 0;           // main-view hook firing count (does 0x7BD180 run?)
static DWORD g_mvViewIdx = 0xFFFFFFFF;    // main-view hook: ebx (the view index it saw)
static DWORD g_laFires = 0, g_laAppends = 0, g_laCount = 0xFFFFFFFF;  // list-append hook diagnostics
static DWORD g_drawTotal = 0, g_drawWorkshop = 0, g_drawCaller = 0;   // draw probe: is the workshop drawn here + from where?
static DWORD g_callers[8] = {0,0,0,0,0,0,0,0};                        // distinct callers of the probed draw fn for the workshop rd

static const char* ResolveModel(DWORD entry, void** outDisplayRec, void** outModelDataRec)
{
    if (entry == 0 || entry >= 0x01000000) return 0;        // reject 0 / absurd (belt-and-suspenders)
    if (entry != g_resolveLoggedEntry)
    {
        char nb[96];
        _snprintf_s(nb, sizeof(nb), _TRUNCATE,
                    "summonpreview: find-only GetRecord for creature entry %u\r\n", entry);
        Note(nb);
        g_resolveLoggedEntry = entry;
    }
    DWORD buf8[2] = { 0, 0 };
    // arg4 = 0 => pure find-only lookup (NO alloc, NO network query, NO callback
    // stored). Returns a record ONLY for an already-cached creature, else 0.
    // (Passing 1 here stored 1 as a completion callback -> deferred `call 1` -> crash.)
    void* rec = ((GetRec_t)kGetRecord)((void*)kCreatureCache, (int)entry, buf8, (void*)0, 0, 0);
    if (!rec) return 0;                                      // not cached yet; silent retry next frame
    DWORD displayId = *(DWORD*)((char*)rec + 0x24);
    if (!displayId) return 0;

    int did = (int)displayId;
    if (did < *(int*)kCDImin || did > *(int*)kCDImax) return 0;
    void** cdiTbl = *(void***)kCDItbl;
    if (!cdiTbl) return 0;
    void* cdi = cdiTbl[did - *(int*)kCDImin];               // CreatureDisplayInfo rec
    if (!cdi) return 0;
    int mdlId = *(int*)((char*)cdi + 4);                    // -> CreatureModelData id

    if (mdlId < *(int*)kCMDmin || mdlId > *(int*)kCMDmax) return 0;
    void** cmdTbl = *(void***)kCMDtbl;
    if (!cmdTbl) return 0;
    void* cmd = cmdTbl[mdlId - *(int*)kCMDmin];             // CreatureModelData rec
    if (!cmd) return 0;

    *outDisplayRec   = cdi;
    *outModelDataRec = cmd;
    return *(const char**)((char*)cmd + 8);                 // ModelName (absolute char*)
}

static void PreviewDestroy()
{
    if (g_previewModel)
    {
        // NOTE: we never SetListed(1) (it hangs PoseAll on our ownerless bone chain),
        // so there is nothing to unlink here - just release.
        ((Release_t)kRelease)(g_previewModel);
        g_previewModel = 0;
    }
    // HIDE the finalize-built engine model on aim-stop. We keep the fake unit persistent (a full
    // teardown of an engine-enrolled model that self-links into [manager+8]/[asset+0x14] is
    // crash-prone); instead collapse its world matrix to ZERO SCALE so the skinning palette
    // (manager recomputes it from model+0xB4 each frame) collapses every vertex to a point ->
    // renders nothing. PlaceLocalUnitModel restores identity scale on the next aim.
    if (g_localGO)
    {
        void* mdl = *(void**)((char*)g_localGO + 0xB4);
        if (mdl)
        {
            DWORD* M = (DWORD*)((char*)mdl + 0xB4);
            for (int i = 0; i < 16; i++) M[i] = 0;
            M[15] = ONE_F;                              // valid homogeneous matrix, zero scale
            DWORD* C = (DWORD*)((char*)mdl + 0xF4);     // collapse the cull matrix too
            for (int i = 0; i < 16; i++) C[i] = M[i];
            *(DWORD*)((char*)mdl + 0x10) |= 0x8000;     // placement dirty
        }
    }
    g_listed = 0;
    g_previewSpell = 0;
}

// Dump the render-relevant fields of a model (and its render-data + geo) so a
// non-rendering preview model can be A/B-compared against a real rendering one.
static void DumpModelFields(const char* label, char* m)
{
    char nb[256];
    if (!m) { _snprintf_s(nb, sizeof(nb), _TRUNCATE, "%s = NULL\r\n", label); Note(nb); return; }
    char* rd = *(char**)(m + 0x2c);
    void* geo = rd ? *(void**)(rd + 0x150) : 0;
    _snprintf_s(nb, sizeof(nb), _TRUNCATE,
        "%s m=%08X +04=%08X +10=%08X +14=%04X +64=%08X +78=%08X +1c4=%08X | rd=%08X rd+08=%08X rd+170=%08X geo=%08X geo+100=%d geo+fc=%08X\r\n",
        label, (DWORD)m,
        *(DWORD*)(m + 0x04), *(DWORD*)(m + 0x10), *(WORD*)(m + 0x14), *(DWORD*)(m + 0x64),
        *(DWORD*)(m + 0x78), *(DWORD*)(m + 0x1c4),
        (DWORD)rd, rd ? *(DWORD*)(rd + 8) : 0, rd ? *(DWORD*)(rd + 0x170) : 0,
        (DWORD)geo, geo ? *(int*)((char*)geo + 0x100) : -1, geo ? *(DWORD*)((char*)geo + 0xfc) : 0);
    Note(nb);
}

// Dump the pose (0x831990) bail-gate inputs so we can see WHY our model skips
// bone computation: gate1 bails if m+0x3c == [[m+0x28]+0x14]; gate2 if m+0x48!=0;
// gate3 if word[[m+0x2c]+0x198]!=0.
static void DumpPoseGates(const char* label, char* m)
{
    if (!m) return;
    char* mgr = *(char**)(m + 0x28);
    char* rd  = *(char**)(m + 0x2c);
    char nb[220];
    _snprintf_s(nb, sizeof(nb), _TRUNCATE,
        "%s GATES: m3c=%08X mgr14=%08X (g1bail=%d) m48=%08X (g2bail=%d) rd198=%04X (g3bail=%d)\r\n",
        label,
        *(DWORD*)(m + 0x3c), mgr ? *(DWORD*)(mgr + 0x14) : 0,
        (mgr && *(DWORD*)(m + 0x3c) == *(DWORD*)(mgr + 0x14)) ? 1 : 0,
        *(DWORD*)(m + 0x48), (*(DWORD*)(m + 0x48) != 0) ? 1 : 0,
        rd ? *(WORD*)(rd + 0x198) : 0, (rd && *(WORD*)(rd + 0x198) != 0) ? 1 : 0);
    Note(nb);
}

// Full hex dump of a model's first 0x2C0 bytes (16 dwords per line) so ours can
// be diffed against a real rendering model to find EVERY differing field.
static void DumpModelHex(const char* label, char* m)
{
    if (!m) return;
    for (DWORD base = 0; base < 0x2C0; base += 0x40)
    {
        char nb[320]; int n = 0;
        n += _snprintf_s(nb + n, sizeof(nb) - n, _TRUNCATE, "%s+%03X:", label, base);
        for (int k = 0; k < 16; k++)
            n += _snprintf_s(nb + n, sizeof(nb) - n, _TRUNCATE, " %08X", *(DWORD*)(m + base + k * 4));
        _snprintf_s(nb + n, sizeof(nb) - n, _TRUNCATE, "\r\n");
        Note(nb);
    }
}

static bool IsReadable(const void* addr, size_t len);   // fwd decl (defined later)
// Dump the exact alpha factors the draw's per-batch cull (0x8347AE) multiplies:
//   final = base(m+0x19c) * color[m+0xa0 + idx*0x20 + 0x1c] * transparency(geo+0x94...)
// esi is the model throughout, so m+0xa0 (per-instance COLOR array) is the only
// per-instance factor - geo is shared with the real workshop. If our color-alpha
// is 0 while the real one's is 1.0, the animate never populated our color output.
static void DumpAlpha(const char* label, char* m)
{
    if (!m) return;
    char* rd  = *(char**)(m + 0x2c);
    char* geo = rd ? *(char**)(rd + 0x150) : 0;
    int   colorCount = geo ? *(int*)(geo + 0x48) : -1;
    float base = *(float*)(m + 0x19c);
    char* colorPtr = *(char**)(m + 0xa0);       // per-instance color-animation output
    char* transPtr = geo ? *(char**)(geo + 0x94) : 0;
    char* skin = rd ? *(char**)(rd + 0x170) : 0;
    char nb[320];
    _snprintf_s(nb, sizeof(nb), _TRUNCATE,
        "%s DRAW: base=%.3f colorCnt=%d transPtr=%08X | bonec=%d boneRt=%08X | m2d0=%08X m10=%08X m4=%02X\r\n",
        label, base, colorCount, (DWORD)transPtr,
        *(int*)(m + 0x90), *(DWORD*)(m + 0x94),
        *(DWORD*)(m + 0x2d0), *(DWORD*)(m + 0x10), *(BYTE*)(m + 4));
    Note(nb);
    _snprintf_s(nb, sizeof(nb), _TRUNCATE,
        "%s DRAW2: rd=%08X skin=%08X skin20=%08X skin24=%d | geo=%08X geo100=%d geo48=%d geo58=%d\r\n",
        label, (DWORD)rd, (DWORD)skin,
        skin ? *(DWORD*)(skin + 0x20) : 0, skin ? *(int*)(skin + 0x24) : -1,
        (DWORD)geo, geo ? *(int*)(geo + 0x100) : -1,
        geo ? *(int*)(geo + 0x48) : -1, geo ? *(int*)(geo + 0x58) : -1);
    Note(nb);
    // Where does the skinning matrix live? Dump the render-matrix buffer (*[m+0x98]) and
    // the inline posed block (m+0x100) - diagonal + translation of the first bone matrix.
    DWORD* rb = *(DWORD**)(m + 0x98);
    DWORD* p1 = (DWORD*)(m + 0x100);
    int rbok = rb && IsReadable(rb, 0x40);
    _snprintf_s(nb, sizeof(nb), _TRUNCATE,
        "%s BONE: m98=%08X rbDiag=%08X,%08X,%08X,%08X rbT=%08X,%08X,%08X | m100Diag=%08X,%08X,%08X,%08X m100T=%08X,%08X,%08X\r\n",
        label, (DWORD)rb,
        rbok?rb[0]:0, rbok?rb[5]:0, rbok?rb[10]:0, rbok?rb[15]:0, rbok?rb[12]:0, rbok?rb[13]:0, rbok?rb[14]:0,
        p1[0],p1[5],p1[10],p1[15],p1[12],p1[13],p1[14]);
    Note(nb);
}

// Dump a 16-float matrix at m+off as one line (raw float hex). Lets us compare
// how a REAL rendering model is placed vs our ownerless one.
static void DumpMatrix(const char* label, char* m, DWORD off)
{
    if (!m) return;
    const DWORD* M = (const DWORD*)(m + off);
    char nb[240];
    _snprintf_s(nb, sizeof(nb), _TRUNCATE,
        "%s+%03X: %08X,%08X,%08X,%08X / %08X,%08X,%08X,%08X / %08X,%08X,%08X,%08X / %08X,%08X,%08X,%08X\r\n",
        label, off, M[0],M[1],M[2],M[3],M[4],M[5],M[6],M[7],M[8],M[9],M[10],M[11],M[12],M[13],M[14],M[15]);
    Note(nb);
}

static bool IsReadable(const void* addr, size_t len);   // fwd decl (defined later)
// Dump the per-instance bone-runtime array at *[m+0x94] (0xAC stride/bone). Read-only,
// so no hang risk. Lets us compare our (unposed) bones' matrices + parent links against
// a real posed model's - the parent link (boneRuntime+0x98) is what the concat walks.
static void DumpBoneRuntime(const char* label, char* m)
{
    if (!m) return;
    char* br = *(char**)(m + 0x94);
    if (!br || !IsReadable(br, 0xAC)) { char nb[64]; _snprintf_s(nb,sizeof(nb),_TRUNCATE,"%sBR: none\r\n",label); Note(nb); return; }
    for (DWORD o = 0; o < 0xAC; o += 0x40)
    {
        DWORD span = (o + 0x40 <= 0xAC) ? 16 : ((0xAC - o) / 4);
        char nb[320]; int n = 0;
        n += _snprintf_s(nb + n, sizeof(nb) - n, _TRUNCATE, "%sBR+%03X:", label, o);
        for (DWORD k = 0; k < span; k++)
            n += _snprintf_s(nb + n, sizeof(nb) - n, _TRUNCATE, " %08X", *(DWORD*)(br + o + k * 4));
        _snprintf_s(nb + n, sizeof(nb) - n, _TRUNCATE, "\r\n");
        Note(nb);
    }
}

// Resolve the local player object via the objmgr active-player GUID (container+0xC0/0xC4), so the
// preview can face the character. objmgr = fs:[0x2c][ [0xD439BC] ]; container = [objmgr+8].
// ObjectPtr 0x4D4DB0 is __cdecl(guidLo,guidHi,typemask,file,line); typemask 8 (UNIT) matches a player.
static void* GetLocalPlayer()
{
    DWORD tlsBase = 0;
    __asm {
        mov  eax, fs:[0x2c]
        mov  tlsBase, eax
    }
    if (!tlsBase) return 0;
    void* objmgr = *(void**)(tlsBase + (*(DWORD*)0x00D439BC) * 4);
    if (!objmgr) return 0;
    void* container = *(void**)((char*)objmgr + 8);
    if (!container) return 0;
    DWORD lo = *(DWORD*)((char*)container + 0xC0);
    DWORD hi = *(DWORD*)((char*)container + 0xC4);
    if (!lo && !hi) return 0;
    typedef void* (__cdecl* ObjPtr_t)(DWORD, DWORD, DWORD, DWORD, DWORD);
    return ((ObjPtr_t)0x004D4DB0)(lo, hi, 8, 0, 0);
}

// Place the finalize-built model (stored at unit+0xB4) at the ground-target cursor. The owner
// callbacks (0x73C140/0x734A40) no-op on our unresolvable guid, so nothing else drives the
// model's world matrix - we write it each frame (which also makes it follow the cursor).
// Identity rotation + g_hitA translation, same convention as PreviewPlace / a real workshop.
static void PlaceLocalUnitModel()
{
    if (!g_localGO || !g_hitAValid) return;
    void* mdl = *(void**)((char*)g_localGO + 0xB4);   // finalize stored the display model here
    if (!mdl) return;
    // Force the STATIC-bbox cull path (0x81CFF0 uses m+0xF4 * M2-local-bbox only when
    // m+0x2d4==3; otherwise it uses an animated per-bone bounds that ignores our m+0xF4 and
    // tracks stale bone data -> the model was visible/invisible purely by camera angle). The
    // workshop is a static model, so the static bbox is the correct, cursor-tracking path.
    *(int*)((char*)mdl + 0x2d4) = 3;
    // Yaw the model to face the CHARACTER (fixed as the camera orbits). Build the world matrix with
    // the local +X axis pointing horizontally from the cursor toward the player; fall back to the
    // camera ([0xCE04A8]+0x88) if the player isn't resolvable.
    float cx = *(float*)&g_hitA[0], cy = *(float*)&g_hitA[1], cz = *(float*)&g_hitA[2];
    float refx = 0.0f, refy = 0.0f, refz = 0.0f; int haveRef = 0;
    void* player = GetLocalPlayer();
    if (player)
    {
        void* pb = *(void**)((char*)player + 0xD8);      // placement block; +0x10/+0x14/+0x18 = world X/Y/Z
        if (pb) { refx = *(float*)((char*)pb + 0x10); refy = *(float*)((char*)pb + 0x14); refz = *(float*)((char*)pb + 0x18); haveRef = 1; }
    }
    if (!haveRef)
    {
        void* scene = *(void**)0x00CE04A8;
        if (scene) { refx = *(float*)((char*)scene + 0x88); refy = *(float*)((char*)scene + 0x8C); haveRef = 1; }
    }
    float dx = 1.0f, dy = 0.0f;
    if (haveRef)
    {
        float vx = refx - cx, vy = refy - cy;
        float len = sqrtf(vx * vx + vy * vy);
        if (len > 1e-4f) { dx = vx / len; dy = vy / len; }
    }
    float* M = (float*)((char*)mdl + 0xB4);
    M[0]= dx;  M[1]= dy;  M[2]=0.0f; M[3]=0.0f;   // local +X -> toward camera (facing)
    M[4]=-dy;  M[5]= dx;  M[6]=0.0f; M[7]=0.0f;   // local +Y -> left
    M[8]=0.0f; M[9]=0.0f; M[10]=1.0f;M[11]=0.0f;  // local +Z -> up
    M[12]=cx;  M[13]=cy;  M[14]=cz;  M[15]=1.0f;   // translation = cursor
    // Refresh the CULL matrix m+0xF4 = m+0xB4 * VIEW (mgr+0x84). The frustum cull (0x81CFF0)
    // transforms the M2 local bbox by m+0xF4, which is a model->VIEW matrix (the engine rebuilds it
    // as m+0xB4 x [view+0x84]); the engine only does so for scene-enrolled models, which ours isn't.
    // A plain B4->F4 copy left the sphere in WORLD space -> culled over ~half the camera arc; the
    // view multiply (same as the render palette in PreviewPlace) tracks the camera correctly.
    void* mgr = *(void**)0x00CD754C;
    if (mgr)
    {
        float scratch[16];
        ((MatMul_t)0x004C1F00)(scratch, (float*)((char*)mdl + 0xB4), (float*)((char*)mgr + 0x84));
        ((MatCopy_t)0x00407F80)((char*)mdl + 0xF4, scratch);
    }
    *(DWORD*)((char*)mdl + 0x10) |= 0x8000;           // placement dirty (mirrors 0x73D6BF)

    // THE visibility gate is the world-scene node at unit+0xB8, whose AABB is anchored at the UNIT's
    // world position and which must sit in a LOADED scene-grid cell to be traversed. Anchor it at the
    // PLAYER (always in a loaded cell + always in the camera frustum) rather than the cursor - the
    // model still DRAWS at the cursor via model+0xB4 above; the node position only gates the cull.
    // Anchoring at the cursor let the node's cell stream out when the player ran far, stranding it
    // (invisible until a full rebuild). Fall back to the cursor if the player isn't resolvable.
    float ax = haveRef ? refx : cx, ay = haveRef ? refy : cy, az = haveRef ? refz : cz;
    void* unit = g_localGO;
    *(float*)((char*)unit + 0x798) = ax;              // node anchor world pos X/Y/Z
    *(float*)((char*)unit + 0x79C) = ay;
    *(float*)((char*)unit + 0x7A0) = az;
    void* f5c = *(void**)((char*)unit + 0xF5C);       // world-matrix sub-object (if built)
    if (f5c)
    {
        *(float*)((char*)f5c + 0x40) = ax;
        *(float*)((char*)f5c + 0x44) = ay;
        *(float*)((char*)f5c + 0x48) = az;
    }
    __asm {
        mov  ecx, unit
        push 0
        mov  eax, 0x007370D0
        call eax                                      // ret 4 (self-cleans): reposition + refile node
    }

    // Keep the shared M2 SKIN resident and the render-node submesh (unit+0x8c +0x54) resolved. The
    // stock per-frame unit updater kicks the skin re-stream + re-binds each frame; our hand-rolled
    // loop omitted it, so when the streaming system unloaded the hidden/far preview's skin the
    // submesh stuck at 0xFFFF and the model went invisible (only a full rebuild recovered it). Kick
    // the async re-stream while the skin is unloaded ([asset+8] bit0 clear), then re-run the virtual
    // bind (0x744460) each frame to re-resolve the submesh once it's resident. No rebuild, no copies.
    void* asset = *(void**)((char*)mdl + 0x2c);
    if (asset && (*(BYTE*)((char*)asset + 8) & 1) == 0)
    {
        __asm {
            mov  ecx, mdl
            push 0
            mov  eax, 0x00823ED0
            call eax                                  // skin-load kick (ret 4, self-cleans)
        }
    }
    __asm {
        mov  ecx, unit
        mov  eax, 0x00744460
        call eax                                      // re-bind -> re-resolve submesh (ret 0)
    }
}

// Swap the fake unit's model when the aimed creature (display id) changes. This keeps exactly
// ONE unit and ONE model alive no matter how many different summon spells are cycled - no
// per-creature accumulation. The old model is released via the engine's refcount release
// 0x824ED0 (add [model],-1; frees + unlinks from [manager+8] at 0), then finalize 0x743760
// rebuilds unit+0xB4 from the new display id. Shared M2 assets are engine-cached (not duplicated).
static void RebuildLocalUnitModel(DWORD newDisplayId)
{
    if (!g_localGO || !newDisplayId) return;
    void* unit = g_localGO;
    void* old  = *(void**)((char*)unit + 0xB4);
    {
        char b[128];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "localunit: rebuild %08X->%08X oldModel=%08X refc=%d\r\n",
            g_builtDisplayId, newDisplayId, (DWORD)old, old ? *(int*)old : -1);
        Note(b);
    }
    if (old)
    {
        *(void**)((char*)unit + 0xB4) = 0;              // clear so finalize rebuilds instead of skipping
        __asm {                                         // 0x824ED0: refcount-- ; frees + unlinks at 0
            mov  ecx, old
            mov  eax, 0x00824ED0
            call eax
        }
    }
    void* d0 = *(void**)((char*)unit + 0xD0);
    if (d0) *(DWORD*)((char*)d0 + 0xF4) = newDisplayId;
    *(DWORD*)((char*)unit + 0x9C0) = newDisplayId;
    __asm {                                             // finalize 0x743760 -> new model at unit+0xB4
        mov  ecx, unit
        mov  eax, 0x00743760
        call eax
    }
    g_builtDisplayId = newDisplayId;
    PlaceLocalUnitModel();
}

// INCREMENT 1 of the client-only local-object approach: allocate + construct a
// CGGameObject_C (typeid 5) via the engine object pool, entirely client-side (no packet).
// Register-convention engine calls (verified): pool alloc 0x4D4930 (esi=typeid; cdecl
// (guidLo,guidHi)->eax; caller cleans 8), descriptor init 0x4D45B0 (eax=typeid, esi=obj;
// ret), per-type ctor 0x743130 (thiscall ecx=obj, arg typeid; ret 4). This step only
// allocs+constructs+GUIDs+LOGS - no display, render, or objmgr register yet, so it cannot
// touch the render walk. Each step logs first, so a crash isolates to the next call.
static void SummonPreview_CreateLocalGO()
{
    if (g_localGO) return;                       // built once; finalize builds its own model (no g_previewModel)
    DWORD gLo = 0x7F000002, gHi = 0xF1300000;   // fake local UNIT GUID (reserved range, not the player)
    void* obj = 0;
    // alloc typeid 3 (UNIT) - the workshop is a creature model, so use the verified CGUnit
    // + SetModel attach chain (§6) rather than a GameObject (whose display can't hold a creature model).
    __asm {
        push esi
        push edi
        push ebx
        mov  eax, gHi
        push eax
        mov  eax, gLo
        push eax
        mov  esi, 3                 // typeid = UNIT
        mov  eax, 0x004D4930
        call eax
        add  esp, 8
        mov  obj, eax
        pop  ebx
        pop  edi
        pop  esi
    }
    { char b[96]; _snprintf_s(b, sizeof(b), _TRUNCATE, "localunit: alloc(3) -> %08X\r\n", (DWORD)obj); Note(b); }
    if (!obj) return;
    __asm {
        push esi
        push edi
        push ebx
        mov  eax, 3
        mov  esi, obj
        mov  edx, 0x004D45B0
        call edx
        pop  ebx
        pop  edi
        pop  esi
    }
    Note("localunit: descriptor init ok\r\n");
    // Override the template's position (+0x28/2C/30 = world XYZ float) with the cursor.
    if (g_hitAValid) {
        g_unitCreateDesc[0x28/4] = g_hitA[0];
        g_unitCreateDesc[0x2C/4] = g_hitA[1];
        g_unitCreateDesc[0x30/4] = g_hitA[2];
    }
    {
        void* desc = g_unitCreateDesc;   // REAL captured template (position overridden above)
        __asm {
            mov  ecx, obj
            mov  eax, desc
            push eax                     // -> [ebp+0xc] = arg2 = zeroed create-descriptor
            push 0                       // -> [ebp+8]  = arg1 (unused by base ctor)
            mov  eax, 0x0073F660
            call eax                     // FULL CGUnit_C ctor: chains base ctors + installs vtable, ret 8
        }
    }
    {
        char b[128]; void** desc = *(void***)((char*)obj + 8);
        _snprintf_s(b, sizeof(b), _TRUNCATE, "localunit: full ctor ok type=%d vtbl=%08X mask=%08X\r\n",
            *(int*)((char*)obj + 0x14), *(DWORD*)obj, desc ? *(DWORD*)((char*)desc + 8) : 0);
        Note(b);
    }
    // Fix up the object identity the create-flow's base ctor 0x743130 would have set (our path
    // skips it): typeid=3 (UNIT), typemask=9 (OBJECT|UNIT) at [obj+8]+8, and the GUID in the
    // identity block [obj+8][0]/[4] plus the cached copies at obj+0x18/0x30/0x34.
    *(int*)((char*)obj + 0x14) = 3;
    { void** id = *(void***)((char*)obj + 8); if (id) *(DWORD*)((char*)id + 8) = 9; }
    { void* id = *(void**)((char*)obj + 8); if (id) { *(DWORD*)id = gLo; *(DWORD*)((char*)id + 4) = gHi; } }
    *(DWORD*)((char*)obj + 0x30) = gLo;
    *(DWORD*)((char*)obj + 0x34) = gHi;
    *(DWORD*)((char*)obj + 0x18) = gLo;
    // Populate the display sub-object so finalize's GetDisplayId (0x717A20 reads [unit+0xD0]+0xF4)
    // resolves the workshop model path. unit+0x9D4=0 selects the +0xF4 field.
    {
        void* d0 = *(void**)((char*)obj + 0xD0);
        if (d0) *(DWORD*)((char*)d0 + 0xF4) = g_workshopDisplayId;
        *(DWORD*)((char*)obj + 0x9C0) = g_workshopDisplayId;   // UNIT_FIELD_DISPLAYID (belt-and-suspenders)
        char b[128];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "localunit: pre-finalize d0=%08X d0.f4=%08X dispId=%08X\r\n",
            (DWORD)d0, d0 ? *(DWORD*)((char*)d0 + 0xF4) : 0, g_workshopDisplayId);
        Note(b);
    }
    // finalize 0x743760(ecx=unit, ret 0): GetDisplayId -> CreateModel(path) -> store model at
    // unit+0xB4, then insert the world cull node at unit+0xB8. This is the engine's own drawable
    // build. Log BEFORE so a crash isolates here.
    __asm {
        mov  ecx, obj
        mov  eax, 0x00743760
        call eax
    }
    {
        char b[192];
        DWORD uB4 = *(DWORD*)((char*)obj + 0xB4);
        _snprintf_s(b, sizeof(b), _TRUNCATE,
            "localunit: FINALIZE ok. uB4=%08X uB8=%08X u98c=%08X | model.2c=%08X model.10=%08X\r\n",
            uB4, *(DWORD*)((char*)obj + 0xB8), *(DWORD*)((char*)obj + 0x98C),
            uB4 ? *(DWORD*)((char*)uB4 + 0x2C) : 0, uB4 ? *(DWORD*)((char*)uB4 + 0x10) : 0);
        Note(b);
    }
    g_localGO = obj;
    g_builtDisplayId = g_workshopDisplayId;   // the model finalize just built is this creature
    PlaceLocalUnitModel();                    // initial placement at the cursor
}

// Issue the one-time creature-info query so an un-seen creature caches (entry -> displayId) and the
// preview resolves on the FIRST aim without ever summoning. ResolveModel is find-only (cb=0) and never
// caches on its own. We reuse the client's OWN completion callback 0x56D460 - the exact call the client
// makes for any uncached creature via a tooltip/nameplate (dispatcher 0x56D960 case 3), so it is
// convention-safe by construction. GetRecord 0x67B6A0(ecx=0xC5D690 cache OBJECT, entry, &buf, cb, 0, 0),
// ret 0x14 (callee-cleans). The not-found path re-sends CMSG each call, so gate to once per entry.
static void PreloadCreature(DWORD entry)
{
    if (!entry) return;
    for (int i = 0; i < g_preloadedCount; i++)
        if (g_preloadedEntries[i] == entry) return;          // already requested this session
    if (g_preloadedCount < 16) g_preloadedEntries[g_preloadedCount++] = entry;
    DWORD buf[2] = { 0, 0 };
    void* pbuf = buf;
    __asm {
        push 0                    // a6
        push 0                    // a5
        push 0x0056D460           // cb = client's own safe completion callback
        mov  eax, pbuf
        push eax                  // buf (2 zeroed dwords)
        push entry                // creature entry
        mov  ecx, 0x00C5D690      // cache OBJECT base (immediate, NOT a deref)
        mov  eax, 0x0067B6A0
        call eax                  // GetRecord: sends CMSG_CREATURE_QUERY if not cached; ret 0x14 self-cleans
    }
    { char b[80]; _snprintf_s(b, sizeof(b), _TRUNCATE, "summonpreview: preload query for entry %u\r\n", entry); Note(b); }
}

// Called every Intersect (i.e. every frame). Integer-only on our side; the
// client model calls are FPU-safe because the stub fnsave/frstor's around us.
static void SummonPreview_Tick()
{
    if (!g_summonArmed) return;

    // A zone change (.tele / loading screen) rebuilds the CM2 manager and world-scene singletons,
    // orphaning our unit + its model/world-node in the old scene (preview vanishes). Detect the
    // rebuild via those pointers and DROP the stale unit so the next aim builds a fresh one. Do NOT
    // free it - the zone wipe reclaimed its object-pool memory, so touching it would fault.
    {
        void* cm2 = *(void**)0x00CD754C;
        void* wscene = *(void**)0x00CE04A8;
        if ((g_lastCM2 && cm2 != g_lastCM2) || (g_lastWScene && wscene != g_lastWScene))
        {
            if (g_localGO) Note("summonpreview: scene rebuilt (zone change) - dropping stale local unit\r\n");
            g_localGO = 0;
            g_builtDisplayId = 0;
        }
        g_lastCM2 = cm2;
        g_lastWScene = wscene;
    }

    // A TELEPORT (cross-map or long) leaves our unit's world node stranded in the pre-teleport scene
    // grid - correctly positioned but never traversed by the new scene, so it never draws (the
    // manager-pointer check above doesn't fire; those pointers are stable). Detect the teleport as a
    // large player-position jump and drop the stale unit so the next aim rebuilds it in the live scene.
    {
        void* pl = GetLocalPlayer();
        if (pl)
        {
            void* pb = *(void**)((char*)pl + 0xD8);
            if (pb)
            {
                float px = *(float*)((char*)pb + 0x10), py = *(float*)((char*)pb + 0x14);
                if (g_lastPlayerValid)
                {
                    float ddx = px - g_lastPlayerX, ddy = py - g_lastPlayerY;
                    if (ddx * ddx + ddy * ddy > 100.0f * 100.0f)   // >100 yd in one frame = teleport
                    {
                        if (g_localGO) Note("summonpreview: teleport detected - dropping stale unit\r\n");
                        g_localGO = 0;
                        g_builtDisplayId = 0;
                    }
                }
                g_lastPlayerX = px; g_lastPlayerY = py; g_lastPlayerValid = 1;
            }
        }
    }

    void* ctx = *(void**)kAimCtxPtr;                        // aiming context (null = not aiming)
    if (!ctx) { PreviewDestroy(); return; }

    int spellId = *(int*)((char*)ctx + 0x20);
    DWORD entry = SummonEntryForSpell(spellId);
    if (!entry) { PreviewDestroy(); return; }               // aiming a non-summon spell

    // Resolve the aimed creature's display id (find-only lookup in the creature cache). If it is
    // not cached yet, kick a one-time preload and keep the preview hidden until it lands.
    void* dispRec = 0, *mdlRec = 0;
    const char* path = ResolveModel(entry, &dispRec, &mdlRec);
    if (!path || !dispRec)
    {
        PreloadCreature(entry);      // async cache fill (once per entry); find-only resolves once it lands
        PreviewDestroy();            // nothing resolvable yet -> keep the preview hidden
        return;
    }
    DWORD displayId = *(DWORD*)dispRec;                     // CreatureDisplayInfo id (dispRec+0)

    // Build the ONE fake unit the first time; swap its single model when the aimed creature
    // changes. finalize (inside these) builds the model itself from the display id, so there is
    // no legacy g_previewModel path - and PlaceLocalUnitModel below runs UNCONDITIONALLY, so a
    // re-aim always re-shows the (zero-scale-hidden) model. That was the bug: placement used to
    // sit behind an early return.
    if (g_descDumped && displayId)
    {
        if (!g_localGO)
        {
            g_workshopDisplayId = displayId;
            SummonPreview_CreateLocalGO();
        }
        else if (displayId != g_builtDisplayId)
        {
            g_workshopDisplayId = displayId;
            RebuildLocalUnitModel(displayId);              // keeps exactly ONE model in memory
        }
        // Submesh recovery after a skin unload (teleport / running far) is handled IN PLACE by the
        // per-frame skin-kick + re-bind in PlaceLocalUnitModel - no rebuild/retry needed here.
    }

    PlaceLocalUnitModel();                                 // show + follow the cursor every frame

    // DIAGNOSTIC (throttled) - only when diagnostics are enabled.
    if (g_diag && g_localGO)
    {
        void* mdl = *(void**)((char*)g_localGO + 0xB4);
        DWORD now = GetTickCount();
        if (mdl && now - g_cullLogMs >= 500)
        {
            g_cullLogMs = now;
            void* asset = *(void**)((char*)mdl + 0x2c);
            void* u8c = *(void**)((char*)g_localGO + 0x8c);
            char b[240];
            _snprintf_s(b, sizeof(b), _TRUNCATE,
                "loadstate: mdl=%08X m10=%08X mdl54=%08X | asset=%08X a8=%08X | u8c=%08X u8c54=%08X\r\n",
                (DWORD)mdl, *(DWORD*)((char*)mdl + 0x10), *(DWORD*)((char*)mdl + 0x54),
                (DWORD)asset, asset ? *(DWORD*)((char*)asset + 8) : 0,
                (DWORD)u8c, u8c ? *(DWORD*)((char*)u8c + 0x54) : 0);
            Note(b);
        }
    }
}

// Called by the stub. startPtr/endPtr point at Vec3 (3 floats); copied as raw
// dwords, never touched as FP here.
extern "C" void __cdecl GOEditorProbeLog(DWORD caller, const DWORD* startPtr,
                                         const DWORD* endPtr, DWORD flags)
{
    if (g_summonEnabled) SummonPreview_Tick();       // creature preview (opt-in)
    MaybeDumpGOs();                                 // one-shot GO memory dump (opt-in)
    if (g_moveTest && flags == 0x01000124)          // cursor-pick call: start == eye
        MoveTestTick(startPtr);

    if (!g_enabled)                                 // ray logging is the probe itself
        return;

    DWORD now = GetTickCount();
    if (!ThrottleOK(caller, now))
        return;

    DWORD sx = 0, sy = 0, sz = 0, ex = 0, ey = 0, ez = 0;
    if (startPtr) { sx = startPtr[0]; sy = startPtr[1]; sz = startPtr[2]; }
    if (endPtr)   { ex = endPtr[0];   ey = endPtr[1];   ez = endPtr[2];   }

    EnterCriticalSection(&g_lock);
    int n = _snprintf_s(g_buf + g_len, sizeof(g_buf) - g_len, _TRUNCATE,
                        "caller=%08X start=%08X,%08X,%08X end=%08X,%08X,%08X flags=%08X\r\n",
                        caller, sx, sy, sz, ex, ey, ez, flags);
    if (n > 0)
        g_len += n;
    if (g_len >= GOE_BUF)
        FlushLocked();
    LeaveCriticalSection(&g_lock);
}

// The detour at 0x7A3B70 jmps here (cdecl, same signature as CWorld::Intersect).
// Entered via jmp, so the caller's return address is on top of the stack and this
// behaves as a normal cdecl call: [ebp+8]=start ... its `ret` returns to Intersect's
// caller. We run the entry work (summon tick + optional ray log), then call the
// REAL Intersect through the trampoline, then read the now-filled *pFrac to compute
// the cursor->terrain hit. x87 is fnsave/frstor-bracketed around our own FP work so
// the x87-heavy caller's state is preserved (same discipline as the old naked stub).
static char __cdecl IntersectWrapper(const float* start, const float* end, void* hitOut,
                                     float* pFrac, int flags, int a6)
{
    __asm { fnsave g_fpuSave }
    GOEditorProbeLog(0, (const DWORD*)start, (const DWORD*)end, (DWORD)flags);
    __asm { frstor g_fpuSave }

    char r = pIntersectReal(start, end, hitOut, pFrac, flags, a6);   // the real CWorld::Intersect

    if (g_summonEnabled && r && pFrac && start && end)
    {
        __asm { fnsave g_fpuSave }
        float t  = *pFrac;                                // in=1.0, out=hit fraction
        float hx = start[0] + t * (end[0] - start[0]);
        float hy = start[1] + t * (end[1] - start[1]);
        float hz = start[2] + t * (end[2] - start[2]);
        DWORD* d = 0;
        if      ((DWORD)flags == 0x01020124) { d = g_hitA; ++g_hitSeenA; }
        else if ((DWORD)flags == 0x00100151) { d = g_hitB; ++g_hitSeenB; }
        else if ((DWORD)flags == 0x00120171) { d = g_hitC; ++g_hitSeenC; }
        if (d)
        {
            d[0] = *(DWORD*)&hx; d[1] = *(DWORD*)&hy; d[2] = *(DWORD*)&hz;
            if (d == g_hitA) g_hitAValid = 1;
        }
        __asm { frstor g_fpuSave }
    }
    return r;
}

// Entered by a jmp planted at 0x7A3B70, before the prologue runs, so esp is
// exactly as at function entry: [esp]=caller retaddr, [esp+4]=&start,
// [esp+8]=&end, [esp+0x14]=flags (cdecl).
__declspec(naked) static void GOEditorProbeHook()
{
    __asm {
        pushad
        pushfd
        fnsave g_fpuSave                // save+reinit x87 so the summon model calls are FP-safe
        mov  eax, esp
        add  eax, 36                    // pushad(32)+pushfd(4) -> original esp (fnsave to a global
                                        // did not move esp, so this offset is unchanged)
        push dword ptr [eax + 0x14]     // flags   (arg5)
        push dword ptr [eax + 8]        // &end    (arg2)
        push dword ptr [eax + 4]        // &start  (arg1)
        push dword ptr [eax + 0]        // caller  (return address)
        call GOEditorProbeLog
        add  esp, 16
        frstor g_fpuSave                // restore the caller's x87 state
        popfd
        popad

        // replay the stolen 6-byte prologue, then resume
        _emit 0x55                      // push ebp
        _emit 0x8B                      // mov ebp, esp
        _emit 0xEC
        _emit 0x83                      // sub esp, 0x18
        _emit 0xEC
        _emit 0x18
        push kResume
        ret
    }
}

// Write our world placement into m+0xB4 RIGHT BEFORE the pose reads it. A real
// (owned) model's owner rewrites m+0xB4 every frame from the collect walk; doing
// it from the Intersect hook instead leaves a window where the value is stale or
// reset. Called from the collect hook just before Animate. Integer-only (raw float
// bits), so no FPU. Identity rotation + g_hitA translation == exactly what a real
// workshop's m+0xB4 holds (verified via REALb4 dump).
extern "C" void __cdecl PreviewPlace()
{
    char* m = (char*)g_previewModel;
    if (!m || !g_hitAValid) return;
    DWORD* M = (DWORD*)(m + 0xB4);
    M[0]=ONE_F; M[1]=0;      M[2]=0;      M[3]=0;
    M[4]=0;     M[5]=ONE_F;  M[6]=0;      M[7]=0;
    M[8]=0;     M[9]=0;      M[10]=ONE_F; M[11]=0;
    M[12]=g_hitA[0]; M[13]=g_hitA[1]; M[14]=g_hitA[2]; M[15]=ONE_F;  // cursor->terrain hit

    // Write the skinning matrix ourselves: *(m+0x98)[0] = m+0xB4(world) * mgr+0x84(view).
    // This is EXACTLY what the internal pose 0x831990 does (verified: 0x8319D6 call 0x4c1f00
    // (mul dest,world,view) then 0x8319DB mov ecx,[m+0x98]; 0x8319E5 call 0x407f80 (copy)),
    // but that pose's dedup gate (m+0x3c==mgr+0x14) is unreliable on our ownerless model and
    // leaves *(m+0x98) at IDENTITY -> the skinning transform collapses the lit mesh, leaving
    // only the shadow. With bonec=1 (setup) the skinning loop applies this matrix to bone 0.
    // Once per frame (guarded on the manager frame counter mgr+0x14); the collect hook fires
    // per walked object, and the view matrix is constant across the frame's walk.
    void* mgr = *(void**)0x00CD754C;
    if (mgr)
    {
        DWORD frame = *(DWORD*)((char*)mgr + 0x14);
        if (frame != g_mtxLastFrame)
        {
            g_mtxLastFrame = frame;
            // BUILD THE GPU BUFFERS. The emit 0x834660 normally calls this prep (0x832EA0)
            // at 0x83468F, but ONLY when m+0x10 bit0 is CLEAR - and ours is SET (0x409363),
            // so it skips prep. Nothing else builds our ownerless model's per-instance vertex/
            // index buffers, so the flush's buffer gates (0x8360A0/0x8362B0) hit null and drop
            // every batch (no lit model, no shadow). We build them ourselves, once per frame.
            DWORD f10 = *(DWORD*)(m + 0x10);
            *(DWORD*)(m + 0x10) = f10 & ~1u;            // clear bit0 so prep actually builds (mirrors gate at 0x834669)
            ((PrepBuffers_t)0x00832EA0)(m);
            *(DWORD*)(m + 0x10) = f10;                  // restore bit0 (loaded)
            // Palette *(m+0x98)[0] = world(m+0xB4) * view(mgr+0x84): the body draw (CM2Batch)
            // skins vertices by the model's own palette, so it must hold world*view.
            void* dst = *(void**)(m + 0x98);
            if (dst)
            {
                float scratch[16];
                ((MatMul_t)0x004C1F00)(scratch, (float*)(m + 0xB4), (float*)((char*)mgr + 0x84));
                ((MatCopy_t)0x00407F80)(dst, scratch);
            }
            *(DWORD*)(m + 0x144) |= 1;                  // pass-enable bit0 (DrawPass(0) skips if clear @0x823CCF)
            // Pin the base-alpha chain AFTER prep (prep zeroes the inline pose block incl.
            // m+0x19c; must set it AFTER, not before). The emit's alpha cull (0x8347AE) drops
            // any batch whose base alpha < 0.55.
            *(DWORD*)(m + 0x178) = ONE_F;
            *(DWORD*)(m + 0x17c) = ONE_F;
            *(DWORD*)(m + 0x19c) = ONE_F;
            // Pin TRANSPARENCY opaque. The cull multiplies base * transparency[m+0xa8][+8];
            // prep left ours 0 (measured [a8+8]=0 vs REAL=1.0) => every batch culled, 0 enqueues.
            // Per-instance array (prep-allocated, distinct from REAL), stride 0x0c, value at +8.
            {
                char* rd2  = *(char**)(m + 0x2c);
                char* geo2 = rd2 ? *(char**)(rd2 + 0x150) : 0;
                char* tb   = *(char**)(m + 0xa8);
                if (geo2 && tb)
                {
                    int tc = *(int*)(geo2 + 0x58);
                    if (tc > 0 && tc <= 256)
                        for (int j = 0; j < tc; j++)
                            *(DWORD*)(tb + j * 0x0c + 8) = ONE_F;
                }
            }
            if (!g_prepDumped && g_realModel && g_realModel != m)
            {
                char b[300];
                char* r = (char*)g_realModel;
                _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "PREV BUF: m140=%08X m170=%08X m178=%08X m17c=%08X m180=%08X m190=%08X m94=%08X m9c=%08X ma4=%08X mac=%08X\r\n",
                    *(DWORD*)(m+0x140),*(DWORD*)(m+0x170),*(DWORD*)(m+0x178),*(DWORD*)(m+0x17c),
                    *(DWORD*)(m+0x180),*(DWORD*)(m+0x190),*(DWORD*)(m+0x94),*(DWORD*)(m+0x9c),*(DWORD*)(m+0xa4),*(DWORD*)(m+0xac));
                Note(b);
                _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "REAL BUF: m140=%08X m170=%08X m178=%08X m17c=%08X m180=%08X m190=%08X m94=%08X m9c=%08X ma4=%08X mac=%08X\r\n",
                    *(DWORD*)(r+0x140),*(DWORD*)(r+0x170),*(DWORD*)(r+0x178),*(DWORD*)(r+0x17c),
                    *(DWORD*)(r+0x180),*(DWORD*)(r+0x190),*(DWORD*)(r+0x94),*(DWORD*)(r+0x9c),*(DWORD*)(r+0xa4),*(DWORD*)(r+0xac));
                Note(b);
                // The batch-visibility array *(m+0x9c): the emit's batch loop (0x834715)
                // skips any batch whose [m+0x9c][idx]==0. Dump its first 12 entries both ways.
                DWORD* pv = *(DWORD**)(m + 0x9c);
                DWORD* rv = *(DWORD**)(r + 0x9c);
                if (pv && IsReadable(pv, 0x30) && rv && IsReadable(rv, 0x30))
                {
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                        "PREV VIS: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\r\n",
                        pv[0],pv[1],pv[2],pv[3],pv[4],pv[5],pv[6],pv[7],pv[8],pv[9],pv[10],pv[11]);
                    Note(b);
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                        "REAL VIS: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\r\n",
                        rv[0],rv[1],rv[2],rv[3],rv[4],rv[5],rv[6],rv[7],rv[8],rv[9],rv[10],rv[11]);
                    Note(b);
                }
                // Alpha state at emit time (post-prep, post-pin). m+0x19c base, m+0xa0 color
                // array ptr, m+0xa8 transparency array ptr - the three the cull multiplies.
                DWORD* pc = *(DWORD**)(m + 0xa0); DWORD* pt = *(DWORD**)(m + 0xa8);
                DWORD* rc = *(DWORD**)(r + 0xa0); DWORD* rt = *(DWORD**)(r + 0xa8);
                _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "PREV ALPHA: m19c=%08X m17c=%08X m178=%08X | a0=%08X [a0+1c]=%08X | a8=%08X [a8+8]=%08X\r\n",
                    *(DWORD*)(m+0x19c),*(DWORD*)(m+0x17c),*(DWORD*)(m+0x178),
                    (DWORD)pc, (pc&&IsReadable(pc,0x20))?pc[7]:0xDEAD, (DWORD)pt, (pt&&IsReadable(pt,0x0c))?pt[2]:0xDEAD);
                Note(b);
                _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "REAL ALPHA: m19c=%08X m17c=%08X m178=%08X | a0=%08X [a0+1c]=%08X | a8=%08X [a8+8]=%08X\r\n",
                    *(DWORD*)(r+0x19c),*(DWORD*)(r+0x17c),*(DWORD*)(r+0x178),
                    (DWORD)rc, (rc&&IsReadable(rc,0x20))?rc[7]:0xDEAD, (DWORD)rt, (rt&&IsReadable(rt,0x0c))?rt[2]:0xDEAD);
                Note(b);
                g_prepDumped = 1;
            }
        }
    }
}

// Build the bone-matrix palette. The render-collect phase (Animate 0x82F0F0 / pose
// 0x831990 / Drain) never evaluates bones - that is done by the animation-UPDATE
// producer CM2Model::animateBone 0x832840, which the engine runs on OWNED models
// from the scene update, never on the render walk. Our ownerless injected model
// gets no scene update, so we drive it here (once per frame). __thiscall(ecx=model,
// keyBone, a2, a3), ret 0xc - same call the world-update loop at 0x4E7777 makes.
// PROBE range 0..64 covers all key bones; narrow once it draws.
static DWORD g_lastPoseFrame = 0xFFFFFFFF;
static DWORD g_previewFrame  = 1;
static DWORD g_poseLogMs     = 0;
// Captured posed bone-matrix block (m+0x100..0x1C0) from a REAL workshop - the model
// is static so its pose is constant. We copy this onto our ownerless model instead of
// running the hang-prone concat. 0x1C0 stops before the owner GUID/world data at 0x1C0.
static DWORD g_poseCapture[48] = {0};   // 0xC0 bytes = 0x100..0x1C0
static int   g_poseCaptured    = 0;
static DWORD g_poseBoneCount   = 0;
typedef void (__thiscall* CM2Build_t)(void* model);   // 0x832EA0 alloc bone runtime arrays
typedef int  (__thiscall* CM2Pose_t)(void* model);    // 0x832450 pose: concat ALL deform bones (raw index)
// The key-bone animateBone was the wrong entry (this static model has no key bones).
// CM2Model::pose 0x832450 -> concat 0x832260 walks bones by RAW index off [m+0x94] with
// no key-bone table and no owner - the exact call the live-model manager tick makes on
// every real creature. Gated on m+0x10 bit0 (loaded) + bit 0x400000 (skeleton dirty) +
// m+0x64==0, and it dedup-skips unless m+0x3c != [[m+0x28]+0x14], so re-set them each frame.
extern "C" void __cdecl PreviewPose()
{
    char* m = (char*)g_previewModel;
    if (!m) return;
    (void)g_lastPoseFrame;

    // Capture the static bone pose from a real workshop ONCE; persists after it's gone.
    if (!g_poseCaptured && g_realModel && g_realModel != m)
    {
        char* r = (char*)g_realModel;
        if (*(DWORD*)(r + 0x90) > 0)
        {
            for (int i = 0; i < 48; ++i) g_poseCapture[i] = *(DWORD*)(r + 0x100 + i * 4);
            g_poseBoneCount = *(DWORD*)(r + 0x90);
            g_poseCaptured  = 1;
        }
    }
    if (!g_poseCaptured) return;                        // nothing to pose with yet

    // SAFE: m+0x98 is a POINTER (crash proved it - writing floats there clobbered it).
    // Do not touch it. Leave posing off for now; the emit will use whatever the client
    // set. No corrupting writes.
    (void)g_poseBoneCount;

    DWORD now = GetTickCount();
    if (now - g_poseLogMs >= 500)
    {
        g_poseLogMs = now;
        char b[224];
        _snprintf_s(b, sizeof(b), _TRUNCATE,
            "posefix: cap=%d m90=%u mB4t=%08X,%08X,%08X m98=%08X\r\n",
            g_poseCaptured, *(DWORD*)(m + 0x90),
            *(DWORD*)(m + 0xE4), *(DWORD*)(m + 0xE8), *(DWORD*)(m + 0xEC),
            *(DWORD*)(m + 0x98));
        Note(b);
    }
}

// Planted at 0x7BBC10, inside the scene collect walk, right AFTER it calls
// 0x834660 for the object it is currently processing. ebp is still the walk's
// frame, so [ebp-0x10] is the live render bucket for this pass. We inject our
// preview model into that same bucket at the correct time (PreviewPlace sets the
// world matrix at m+0xB4 just before the pose), then replay the stolen
// `add [ebp-8],0xF4` and resume. This is the enrollment SetListed does not do.
// Injecting once per walked object over-poses a bit but draws opaque at one depth.
__declspec(naked) static void GOEditorCollectHook()
{
    __asm {
        pushad
        pushfd
        mov  eax, g_summonEnabled
        test eax, eax
        jz   cdone
        mov  eax, g_previewModel
        test eax, eax
        jz   cdone
        mov  ecx, [ebp - 4]             // the walk's current REAL object
        test ecx, ecx
        jz   nocap
        mov  ecx, [ecx + 0x34]          // its model
        test ecx, ecx
        jz   nocap
        mov  edx, g_previewModel        // capture ONLY a real model of the SAME display
        mov  edx, [edx + 0x2c]          //   (same renderData = same 900116) so its
        cmp  edx, [ecx + 0x2c]          //   provider setup is valid for ours
        jne  nocap
        cmp  ecx, g_previewModel        // and not our own model
        je   nocap
        mov  g_realModel, ecx           // capture a matching real, rendering model
    nocap:
        call PreviewPlace               // build buffers + matrix + alpha + placement (once/frame)
        // This hook is in the SHADOW collect walk; [ebp-0x10] is the shadow view's bucket,
        // so this injection draws the preview's projected SHADOW. The lit BODY is injected
        // separately by the main-view hook (below) into the main color view's buckets.
        mov  eax, [ebp - 0x10]          // shadow-view bucketA
        lea  edx, [eax + 0x0C]          // shadow-view bucketB
        push edx
        push eax
        mov  ecx, g_previewModel        // this = our model
        mov  eax, 0x00834660            // Collect/pose/emit; __thiscall, ret 8
        call eax
    cdone:
        popfd
        popad
        _emit 0x81                      // replay stolen: add dword ptr [ebp-8], 0x000000F4
        _emit 0x45                      // (emitted verbatim so the +244 stride is imm32,
        _emit 0xF8                      //  never a sign-extended -12 byte)
        _emit 0xF4
        _emit 0x00
        _emit 0x00
        _emit 0x00
        push kCollectResume             // 0x7BBC17
        ret
    }
}

// Planted at 0x7BD1B2, inside the MAIN COLOR view's collect walk (0x7BD180), right AFTER
// its own 0x834660 call. ebx = the color view index (main = 0), esi = the current scene
// object ([esi+0x34] = its model). We inject the preview's LIT BODY into this view's bucket
// group (0xD25320 + ebx*0x24) so it is drawn by the main color flush - the shadow-walk hook
// only reaches the shadow view. PreviewPlace (once/frame) builds buffers/matrix/alpha here so
// the body has valid GPU state regardless of which walk runs first. Replays the 9 stolen
// bytes (mov eax,[ebp-0xc]; mov eax,[eax+0xc4]) and resumes.
__declspec(naked) static void GOEditorMainViewHook()
{
    __asm {
        pushad
        pushfd
        mov  eax, g_summonEnabled
        test eax, eax
        jz   mvdone
        mov  eax, g_previewModel
        test eax, eax
        jz   mvdone
        mov  ecx, [esi + 0x34]          // current object's model
        test ecx, ecx
        jz   mvnocap
        mov  edx, g_previewModel
        mov  edx, [edx + 0x2c]
        cmp  edx, [ecx + 0x2c]          // same renderData (same display) ?
        jne  mvnocap
        cmp  ecx, g_previewModel        // and not our own model
        je   mvnocap
        mov  g_realModel, ecx           // capture a matching real, rendering model
    mvnocap:
        inc  g_mvHookCount              // firing count
        mov  g_mvViewIdx, ebx           // view index seen
        mov  eax, ebx                   // view index
        imul eax, eax, 0x24
        add  eax, 0x00D25320            // eax = this view's record base
        mov  g_mvBucketBase, eax        // stash across the calls
        call PreviewPlace               // build buffers + matrix + alpha + placement (once/frame)
        mov  eax, g_mvBucketBase
        mov  ecx, [eax + 0x10]          // bucketA count BEFORE emit (base+0xc+4)
        mov  g_preA, ecx
        lea  edx, [eax + 0x18]          // bucketB (blended) = base + 0x18
        lea  ecx, [eax + 0x0C]          // bucketA (opaque)  = base + 0x0C
        push edx
        push ecx
        mov  ecx, g_previewModel        // this = our model
        mov  eax, 0x00834660            // Collect/pose/emit; __thiscall, ret 8
        call eax
        mov  eax, g_mvBucketBase
        mov  ecx, [eax + 0x10]          // bucketA count AFTER emit
        mov  g_postA, ecx
    mvdone:
        popfd
        popad
        _emit 0x8B                      // replay stolen: mov eax,[ebp-0xc]
        _emit 0x45
        _emit 0xF4
        _emit 0x8B                      // mov eax,[eax+0xc4]
        _emit 0x80
        _emit 0xC4
        _emit 0x00
        _emit 0x00
        _emit 0x00
        push kMainViewResume            // 0x7BD1BB
        ret
    }
}

// Planted OVER the `call 0x829AA0` at 0x7E3E03 (main world-M2 LIT render loop 0x7E3D20).
// This is the IMMEDIATE-MODE per-object body draw (0x829AA0 __thiscall(model), draws using
// the model's own palette *(m+0x98)=world*view, NO projector, NO 0xD25320 bucket) - the
// path that actually shows the lit 3D body. The 0x834660 / 0xD25320 bucket subsystem is the
// planar-SHADOW descriptor (0xD43158, flush projects along 0xD43180), which is why every
// bucket injection only ever produced a shadow. On entry ecx = the game's current model.
// We replicate the original draw for it, then draw OUR preview model in the same lit state.
__declspec(naked) static void GOEditorLitDrawHook()
{
    __asm {
        // capture a matching real model (ecx = game model here) for the A/B compare
        pushad
        pushfd
        mov  eax, g_summonEnabled
        test eax, eax
        jz   lcapdone
        mov  eax, g_previewModel
        test eax, eax
        jz   lcapdone
        test ecx, ecx
        jz   lcapdone
        mov  edx, g_previewModel
        mov  edx, [edx + 0x2c]
        cmp  edx, [ecx + 0x2c]          // same renderData (same display)?
        jne  lcapdone
        cmp  ecx, g_previewModel
        je   lcapdone
        mov  g_realModel, ecx
    lcapdone:
        popfd
        popad
        // 1) original per-object lit draw for the game model (ecx unchanged = game model)
        mov  eax, 0x00829AA0
        call eax                        // __thiscall, ret 0 (no stack args); preserves ebx/esi/edi/ebp
        // 2) inject OUR preview model into the same lit pass
        pushad
        pushfd
        mov  eax, g_summonEnabled
        test eax, eax
        jz   ldone
        mov  eax, g_previewModel
        test eax, eax
        jz   ldone
        call PreviewPlace               // prep buffers + matrix(world*mainview) + alpha (once/frame)
        mov  ecx, g_previewModel
        mov  eax, 0x00829AA0
        call eax                        // draw our body, lit, main-view device state
    ldone:
        popfd
        popad
        ret                             // -> 0x7E3E08 (after the original call site)
    }
}

// Planted OVER `add esp,4 ; test eax,eax` at 0x7E3AC3 (right after the world-M2 list build
// `call 0x7E35F0` returns, before the draw loops 0x7E3D20/0x7E4233). We APPEND our preview
// model to the list at 0xD38014 (count 0xD38054, cap 10 raw CM2Model*) so the loop draws it
// with full native per-object setup (matrix-slot copy + 0x872B00/0x873900 + 0x829AA0) - the
// bucket subsystem was shadow-only, this is the real lit-body path. Self-clears each frame
// (build zeroes the count). On the preview path we skip the empty early-out so the draw runs;
// with no preview we replay the native `test eax,eax; je` behaviour.
__declspec(naked) static void GOEditorListAppendHook()
{
    __asm {
        add  esp, 4                     // replicate stolen `add esp,4` (build stack cleanup); eax = build return
        pushad
        pushfd
        inc  dword ptr [g_laFires]      // DIAG: hook fired
        mov  eax, g_summonEnabled
        test eax, eax
        jz   lano
        mov  eax, g_previewModel
        test eax, eax
        jz   lano
        call PreviewPlace               // build buffers + m+0xB4 world placement + alpha (once/frame)
        mov  eax, dword ptr [kM2ListCount]
        mov  dword ptr [g_laCount], eax // DIAG: count seen
        cmp  eax, 10                    // cap: only slots 0..9 are the array (0xD3803C is a live global)
        jae  lano
        mov  ecx, g_previewModel
        mov  dword ptr [eax*4 + kM2List], ecx   // array[count] = ourModel (raw CM2Model*)
        inc  eax
        mov  dword ptr [kM2ListCount], eax      // count++
        inc  dword ptr [g_laAppends]    // DIAG: appended
        popfd
        popad
        mov  eax, 1                     // force non-empty so the draw path runs
        push kListDrawResume            // 0x7E3ACE (after the empty-early-out je)
        ret
    lano:
        popfd
        popad
        test eax, eax                   // no append: replay native `test eax,eax`
        push kListEmptyJe               // 0x7E3AC8 (the native je uses the flags we just set)
        ret
    }
}

// DIAGNOSTIC helper: called from the draw probe with (rd, caller). If rd matches our
// preview's rd (= a workshop draw), record the caller (deduped, logged once each).
extern "C" void __cdecl RecordDrawCaller(void* rd, DWORD* ebp)
{
    char* pm = (char*)g_previewModel;
    if (!pm || !rd) return;
    if (*(void**)(pm + 0x2c) != rd) return;            // not the workshop rd
    g_drawWorkshop++;
    if (!IsReadable(ebp, 8)) return;
    DWORD key = ebp[1];                                 // caller of 0x81F700 (dedup key)
    for (int i = 0; i < 8; i++)
    {
        if (g_callers[i] == key) return;
        if (g_callers[i] == 0)
        {
            g_callers[i] = key;
            // walk the ebp chain and log the return-address call stack above 0x81F700
            char b[220]; int n = 0;
            n += _snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, "drawchain[%d]:", i);
            DWORD* fp = ebp;
            for (int k = 0; k < 7 && IsReadable(fp, 8); k++)
            {
                n += _snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, " %08X", fp[1]);
                DWORD* nf = (DWORD*)fp[0];
                if (nf <= fp) break;                    // stack frames go upward
                fp = nf;
            }
            _snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, "\r\n");
            Note(b);
            return;
        }
    }
}

// DIAGNOSTIC: planted over the 6-byte prologue of the low-level M2 vertex-buffer bind
// 0x8362B0 (__thiscall(rd,...)). ecx = rd at entry. Every drawn M2 batch (body OR shadow)
// passes through here; we record the distinct CALLERS whose rd is the workshop's, to find
// the real body-draw path.
__declspec(naked) static void GOEditorDrawProbeHook()
{
    __asm {
        pushad
        pushfd
        inc  dword ptr [g_drawTotal]
        push ebp                        // arg2 = caller's ebp (0x81F700's frame) - walk the chain
        push ecx                        // arg1 = rd (ecx preserved through pushad)
        call RecordDrawCaller
        add  esp, 8
        popfd
        popad
        // replay stolen prologue, resume at 0x8362B6
        push ebp
        mov  ebp, esp
        sub  esp, 0x14
        push kDrawProbeResume
        ret
    }
}

// Planted OVER the 7-byte `cmp [0xac79a4],2` at 0x4F9122 - the return of the pass-0
// CM2Model::DrawPass call in the world orchestrator 0x4F8EA0 (empirically THE lit-body
// path). After the real model's pass-0 body draw, we invoke DrawPass(ourPreview, 0) so our
// body draws in the same live device state. PreviewPlace (once/frame) has built our buffers,
// palette, alpha, and pass-enable mask. Replays the stolen cmp (sets flags for the jge at
// 0x4F9129) and resumes.
__declspec(naked) static void GOEditorBodyHook()
{
    __asm {
        pushad
        mov  eax, g_summonEnabled
        test eax, eax
        jz   bdone
        mov  eax, g_previewModel
        test eax, eax
        jz   bdone
        call PreviewPlace               // build buffers + palette + alpha + placement + pass mask
        mov  ecx, g_previewModel
        mov  eax, [ecx + 4]             // owner/render-node sub-object DrawPass derefs
        test eax, eax
        jz   bdone                      // null on an ownerless model -> skip (would crash at [null+4])
        push 0                           // passIndex 0
        mov  eax, 0x00823CB0
        call eax                         // CM2Model::DrawPass(ourModel, 0); __thiscall, ret 4 (self-cleans arg)
    bdone:
        popad
        // relocated stolen `cmp [0xac79a4],2` - set flags for the jge at 0x4F9129 without
        // clobbering registers (pop does not affect flags)
        push eax
        mov  eax, dword ptr ds:[0x00AC79A4]
        cmp  eax, 2
        pop  eax
        push kBodyHookResume             // 0x4F9129
        ret
    }
}

// One-shot dump of a REAL unit create-descriptor (arg2 of ctor 0x73F660). Skips our own
// zeroed buffer so it captures a genuine server spawn. This is the template we replicate.
extern "C" void __cdecl DumpUnitDesc(void* arg1, void* desc)
{
    if (g_descDumped || !desc || desc == (void*)g_unitCreateDesc || !IsReadable(desc, 0x60)) return;
    g_descDumped = 1;
    char b[420]; int n = 0;
    n += _snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, "unitdesc arg1=%08X desc=%08X:", (DWORD)arg1, (DWORD)desc);
    for (int i = 0; i < 0x14; i++)   // 20 dwords = 0x50 bytes
        n += _snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, " %08X", *(DWORD*)((char*)desc + i * 4));
    _snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, "\r\n");
    Note(b);
    // Copy the real descriptor as our template (position at +0x28/2C/30 is overridden per-create).
    for (int i = 0; i < 0x14; i++) g_unitCreateDesc[i] = *(DWORD*)((char*)desc + i * 4);
}

// Planted over the 9-byte prologue of the unit ctor 0x73F660 to capture the real create-
// descriptor (arg2) of a live spawn. At entry: [esp]=ret, [esp+4]=arg1, [esp+8]=arg2.
__declspec(naked) static void GOEditorUnitCtorProbe()
{
    __asm {
        pushad
        pushfd
        mov  eax, [esp + 44]        // arg2 (orig [esp+8], +36 for pushad/pushfd)
        push eax                    // push arg2 -> esp drops 4, so arg1 (orig [esp+4]) is now at [esp+44]
        mov  eax, [esp + 44]        // arg1
        push eax
        call DumpUnitDesc           // __cdecl(arg1, arg2)
        add  esp, 8
        popfd
        popad
        // replay stolen prologue (9 bytes: push ebp; mov ebp,esp; mov eax,[ebp+0xc]; sub esp,0x14)
        push ebp
        mov  ebp, esp
        mov  eax, [ebp + 0x0c]
        sub  esp, 0x14
        push kUnitCtorResume
        ret
    }
}

// Planted at 0x6FD72C, inside the ground-target targeting render 0x6FD6B0, on the
// success path right after it calls the world resolver 0x6FCD60. ebp is still that
// frame, so the resolver's DST out-vec is [ebp-0x34] (the reticle world point, the
// same one sent as the cast dest) and its SRC/origin is [ebp-0x44]. We copy both
// out, then replay the 3 stolen instrs (lea edx,[ebp-0x34]; push edx; lea eax,[ebp-0x44])
// and resume. Read-only w.r.t. the client; captures whatever the client itself plants.
__declspec(naked) static void GOEditorReticleHook()
{
    __asm {
        pushad
        pushfd
        mov  eax, g_summonEnabled
        test eax, eax
        jz   rdone
        lea  esi, [ebp - 0x34]          // DST out-vec (reticle / cast-dest world xyz)
        lea  edi, g_reticle
        mov  eax, [esi]
        mov  [edi], eax
        mov  eax, [esi + 4]
        mov  [edi + 4], eax
        mov  eax, [esi + 8]
        mov  [edi + 8], eax
        lea  esi, [ebp - 0x44]          // SRC out-vec (ray origin)
        lea  edi, g_reticleSrc
        mov  eax, [esi]
        mov  [edi], eax
        mov  eax, [esi + 4]
        mov  [edi + 4], eax
        mov  eax, [esi + 8]
        mov  [edi + 8], eax
        mov  dword ptr g_reticleValid, 1
        inc  g_reticleSeen
    rdone:
        popfd
        popad
        // replay stolen: lea edx,[ebp-0x34]; push edx; lea eax,[ebp-0x44]
        _emit 0x8D
        _emit 0x55
        _emit 0xCC
        _emit 0x52
        _emit 0x8D
        _emit 0x45
        _emit 0xBC
        push kReticleResume             // 0x6FD733
        ret
    }
}

static bool IsReadable(const void* addr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
        return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & ok) || (mbi.Protect & PAGE_GUARD))
        return false;
    const BYTE* end = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
    return (const BYTE*)addr + len <= end;
}

void GOEditor_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir);

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;                                          // no ini => stays OFF

    g_enabled     = GetPrivateProfileIntA("GOEditorProbe", "Enabled", 0, ini);
    g_throttleMs  = GetPrivateProfileIntA("GOEditorProbe", "ThrottleMs", 40, ini);
    g_dumpGO      = GetPrivateProfileIntA("GOEditorProbe", "DumpGO", 0, ini);
    g_dumpDelayMs = GetPrivateProfileIntA("GOEditorProbe", "DumpDelayMs", 5000, ini);
    g_moveTest    = GetPrivateProfileIntA("GOEditorProbe", "MoveTest", 0, ini);
    g_summonEnabled = GetPrivateProfileIntA("SummonPreview", "Enabled", 0, ini);
}

// True if `n` bytes at `va` are readable and match `sig`.
static bool SigOk(DWORD va, const BYTE* sig, size_t n)
{
    return IsReadable((void*)va, n) && memcmp((void*)va, sig, n) == 0;
}

void GOEditor_Install()
{
    if (!g_enabled && !g_summonEnabled)              // the hook drives both features
        return;

    BYTE* p = (BYTE*)kSite;
    if (!IsReadable(p, sizeof(kSig)) || memcmp(p, kSig, sizeof(kSig)) != 0)
    {
        Note("goeditor: signature mismatch at 0x7A3B70 - not installed (wrong build?)\r\n");
        return;
    }

    // Arm the summon preview only if all its callable signatures still match.
    if (g_summonEnabled)
    {
        if (SigOk(kCreateModel, kCreateModelSig, sizeof(kCreateModelSig)) &&
            SigOk(kGetRecord,   kGetRecordSig,   sizeof(kGetRecordSig)) &&
            SigOk(kRelease,     kReleaseSig,     sizeof(kReleaseSig)) &&
            SigOk(kBindModel,   kBindModelSig,   sizeof(kBindModelSig)) &&
            SigOk(kBatchBuild,  kBatchBuildSig,  sizeof(kBatchBuildSig)) &&
            SigOk(kBuildBatches,kBuildBatchesSig,sizeof(kBuildBatchesSig)) &&
            SigOk(kCollect,     kCollectSig,     sizeof(kCollectSig)) &&
            SigOk(kBodyHookSite, kBodyHookSig,   sizeof(kBodyHookSig)) &&
            SigOk(kDrain,       kDrainSig,       sizeof(kDrainSig)))
        {
            g_summonArmed = 1;
            Note("summonpreview: armed (create/getrecord/release/bind/batch/collect signatures OK)\r\n");
        }
        else
        {
            g_summonEnabled = 0;
            Note("summonpreview: DISABLED - a client signature did not match this build\r\n");
        }
    }

    InitializeCriticalSection(&g_lock);
    g_lockInit = 1;

    // Truncate any previous log and write the header, then CLOSE. Only when diagnostics are on;
    // with g_diag==0 we never touch the log file at all.
    if (g_diag)
    {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%sgoeditor_probe.log", g_dir);
        const char* hdr = "# goeditor_probe: CWorld::Intersect ray log + optional GO memory dump.\r\n"
                          "# ray x/y/z are raw 32-bit float hex: "
                          "python -> struct.unpack('<f', struct.pack('<I', int(h,16)))[0]\r\n";
        HANDLE hf = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE)
        {
            DWORD wrote = 0;
            WriteFile(hf, hdr, (DWORD)strlen(hdr), &wrote, NULL);
            CloseHandle(hf);
        }
    }

    // Build a trampoline for the REAL CWorld::Intersect: the 6 stolen prologue
    // bytes, then jmp back to kSite+6. Calling pIntersectReal() runs the original
    // function unchanged, so the wrapper can read *pFrac after it returns. Built
    // BEFORE we patch kSite (memcpy needs the original prologue bytes).
    g_intersectTramp = (BYTE*)VirtualAlloc(0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_intersectTramp)
    {
        Note("goeditor: Intersect trampoline alloc failed - not installed\r\n");
        return;
    }
    memcpy(g_intersectTramp, (const void*)kSite, 6);     // stolen prologue: 55 8B EC 83 EC 18
    g_intersectTramp[6] = 0xE9;                          // jmp rel32 -> kSite+6 (0x7A3B76)
    *(DWORD*)(g_intersectTramp + 7) = (kSite + 6) - ((DWORD)g_intersectTramp + 11);
    FlushInstructionCache(GetCurrentProcess(), g_intersectTramp, 16);
    pIntersectReal = (IntersectFn_t)g_intersectTramp;

    DWORD old = 0;
    if (!VirtualProtect(p, sizeof(kSig), PAGE_EXECUTE_READWRITE, &old))
    {
        Note("goeditor_probe: VirtualProtect failed - not installed\r\n");
        return;
    }
    p[0] = 0xE9;                                          // jmp rel32 -> IntersectWrapper
    *(DWORD*)(p + 1) = (DWORD)IntersectWrapper - (kSite + 5);
    p[5] = 0x90;                                          // pad the 6th stolen byte
    VirtualProtect(p, sizeof(kSig), old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, sizeof(kSig));

    // Plant the scene-collect-walk hook (0x7BBC10) that injects the preview model
    // into the live render bucket at the correct frame time. Only when summon is
    // armed (all its signatures, incl. the 7-byte add at 0x7BBC10, matched).
    if (g_summonArmed)
    {
        // Plant the LIT-BODY list-append hook: jmp from 0x7E3AC3 to our trampoline, which
        // appends the preview model to the world-M2 draw list so the native instanced loop
        // draws it. (The 0x834660/0xD25320 bucket subsystem is shadow-only.)
        if (0)  // DISABLED: list-append into the world-M2 draw list was the shadow-era approach (wrong subsystem).
        {
            BYTE* lp = (BYTE*)kListAppendSite;
            DWORD lold = 0;
            if (VirtualProtect(lp, sizeof(kListAppendSig), PAGE_EXECUTE_READWRITE, &lold))
            {
                lp[0] = 0xE9;                            // jmp rel32 -> list-append trampoline
                *(DWORD*)(lp + 1) = (DWORD)GOEditorListAppendHook - (kListAppendSite + 5);
                VirtualProtect(lp, sizeof(kListAppendSig), lold, &lold);
                FlushInstructionCache(GetCurrentProcess(), lp, sizeof(kListAppendSig));
                Note("summonpreview: lit-body list-append hook planted at 0x7E3AC3\r\n");
            }
            else
                Note("summonpreview: list-append VirtualProtect failed - body will not render\r\n");
        }
        // Plant the LIT-BODY hook at 0x4F9122 (return of the pass-0 CM2Model::DrawPass call
        // in the world orchestrator). Steal 7 bytes: jmp(5) + 2 nop pad.
        if (0)  // DISABLED: hand-calling scene DrawPass 0x823CB0 with our MODEL as ecx was the crash. DrawPass takes the manager [0xCD754C], not a model. Correct path = engine renders a scene-registered unit.
        {
            BYTE* bp = (BYTE*)kBodyHookSite;
            DWORD bold = 0;
            if (VirtualProtect(bp, sizeof(kBodyHookSig), PAGE_EXECUTE_READWRITE, &bold))
            {
                bp[0] = 0xE9;                            // jmp rel32 -> body-draw trampoline
                *(DWORD*)(bp + 1) = (DWORD)GOEditorBodyHook - (kBodyHookSite + 5);
                bp[5] = 0x90; bp[6] = 0x90;              // pad the 2 extra stolen bytes (7-byte cmp)
                VirtualProtect(bp, sizeof(kBodyHookSig), bold, &bold);
                FlushInstructionCache(GetCurrentProcess(), bp, sizeof(kBodyHookSig));
                Note("summonpreview: LIT-BODY hook planted at 0x4F9122 (DrawPass pass 0)\r\n");
            }
            else
                Note("summonpreview: body-hook VirtualProtect failed - body will not render\r\n");
        }
        // DIAGNOSTIC: probe 0x8362B0 to confirm the workshop-rd draws (and via our injection).
        if (SigOk(kDrawProbeSite, kDrawProbeSig, sizeof(kDrawProbeSig)))
        {
            BYTE* dp = (BYTE*)kDrawProbeSite;
            DWORD dold = 0;
            if (VirtualProtect(dp, sizeof(kDrawProbeSig), PAGE_EXECUTE_READWRITE, &dold))
            {
                dp[0] = 0xE9;
                *(DWORD*)(dp + 1) = (DWORD)GOEditorDrawProbeHook - (kDrawProbeSite + 5);
                dp[5] = 0x90;                            // pad the 6th stolen byte
                VirtualProtect(dp, sizeof(kDrawProbeSig), dold, &dold);
                FlushInstructionCache(GetCurrentProcess(), dp, sizeof(kDrawProbeSig));
                Note("summonpreview: draw-probe planted at 0x829AA0\r\n");
            }
        }
        // Capture a real unit create-descriptor from the ctor 0x73F660 (one-shot).
        {
            BYTE* ub = (BYTE*)kUnitCtorSite;
            char db[144];
            _snprintf_s(db, sizeof(db), _TRUNCATE,
                "unitctor sigcheck @%08X: %02X %02X %02X %02X %02X %02X %02X %02X %02X match=%d\r\n",
                (DWORD)kUnitCtorSite, ub[0],ub[1],ub[2],ub[3],ub[4],ub[5],ub[6],ub[7],ub[8],
                SigOk(kUnitCtorSite, kUnitCtorSig, sizeof(kUnitCtorSig)) ? 1 : 0);
            Note(db);
        }
        if (SigOk(kUnitCtorSite, kUnitCtorSig, sizeof(kUnitCtorSig)))
        {
            BYTE* up = (BYTE*)kUnitCtorSite;
            DWORD uold = 0;
            if (VirtualProtect(up, sizeof(kUnitCtorSig), PAGE_EXECUTE_READWRITE, &uold))
            {
                up[0] = 0xE9;
                *(DWORD*)(up + 1) = (DWORD)GOEditorUnitCtorProbe - (kUnitCtorSite + 5);
                up[5] = 0x90; up[6] = 0x90; up[7] = 0x90; up[8] = 0x90;   // pad 4 extra stolen bytes
                VirtualProtect(up, sizeof(kUnitCtorSig), uold, &uold);
                FlushInstructionCache(GetCurrentProcess(), up, sizeof(kUnitCtorSig));
                Note("summonpreview: unit-ctor descriptor probe planted at 0x73F660\r\n");
            }
        }

        // Plant the reticle-source hook (0x6FD72C): capture the world point the
        // targeting render resolves each frame while aiming a ground-target spell.
        if (SigOk(kReticleSite, kReticleSig, sizeof(kReticleSig)))
        {
            BYTE* rp = (BYTE*)kReticleSite;
            DWORD rold = 0;
            if (VirtualProtect(rp, sizeof(kReticleSig), PAGE_EXECUTE_READWRITE, &rold))
            {
                rp[0] = 0xE9;                            // jmp rel32 -> reticle hook
                *(DWORD*)(rp + 1) = (DWORD)GOEditorReticleHook - (kReticleSite + 5);
                rp[5] = 0x90; rp[6] = 0x90;              // pad the 2 extra stolen bytes (7-byte steal)
                VirtualProtect(rp, sizeof(kReticleSig), rold, &rold);
                FlushInstructionCache(GetCurrentProcess(), rp, sizeof(kReticleSig));
                Note("summonpreview: reticle-source hook planted at 0x6FD72C\r\n");
            }
            else
                Note("summonpreview: reticle-source VirtualProtect failed - preview will not track the cursor\r\n");
        }
        else
            Note("summonpreview: reticle-source signature mismatch at 0x6FD72C - preview will not track the cursor\r\n");
    }

    g_installTick = GetTickCount();
    if (g_dumpGO || g_moveTest)
    {
        BYTE* e = (BYTE*)kEnumVisible;
        BYTE* o = (BYTE*)kObjectPtr;
        if (IsReadable(e, sizeof(kEnumSig)) && memcmp(e, kEnumSig, sizeof(kEnumSig)) == 0 &&
            IsReadable(o, sizeof(kObjectPtrSig)) && memcmp(o, kObjectPtrSig, sizeof(kObjectPtrSig)) == 0)
        {
            pEnumVisible = (EnumVisible_t)kEnumVisible;
            pObjectPtr   = (ObjectPtr_t)kObjectPtr;
            Note("goeditor_probe: object-manager armed (GO dump / move-test)\r\n");
        }
        else
        {
            g_dumpGO = 0;
            g_moveTest = 0;
            Note("goeditor_probe: GO dump/move-test DISABLED - EnumVisible/ObjectPtr signature mismatch\r\n");
        }
    }
    Note("goeditor_probe: installed at 0x7A3B70\r\n");
}

void GOEditor_Shutdown()
{
    if (!g_lockInit)
        return;
    EnterCriticalSection(&g_lock);
    FlushLocked();
    LeaveCriticalSection(&g_lock);
}
