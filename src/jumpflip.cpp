// jumpflip.cpp - see jumpflip.h for what and why.
//
// Wow.exe 3.3.5a 12340 facts this relies on (all read from the disassembly,
// docs/ANIMATION_FIXES.md "Jump flip"):
//
//   CM2Model::PlayAnimation 0x832AB0, __thiscall(model, 7 args), ret 0x1C
//     arg0 key bone (-1 = whole model), arg1 animation id, arg2 variation
//     (-1 = roll), arg4 float rate. Unit animation code passes -1 for arg2
//     (e.g. 0x71559A).
//   model+0x10 bit 0   model loaded (else PlayAnimation queues the request)
//   model+0x2C         CM2Shared*;  shared+0x150 = the M2 header in memory,
//                      offsets already fixed up to pointers
//   header+0x1C/+0x20  sequence count / sequence array (stride 0x40)
//   sequence +0x00 id (u16), +0x02 variationIndex (u16), +0x10 frequency
//                      (the roll at 0x826E60 reads it as a dword), +0x3C next
//                      variation index (u16, 0xFFFF = end)
//   unit+0xB4          the unit's own CM2Model* (the druid form model while
//                      shapeshifted; the form models carry the flip too)
//   unit+0x08          descriptor block; +0x5C UNIT_FIELD_BYTES_0: byte 0 race
//                      (4 = night elf), byte 2 gender (1 = female);
//                      +0x114 UNIT_FIELD_MOUNTDISPLAYID (0 = not mounted)

#include "jumpflip.h"
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------- client ABI
static const DWORD kGetActiveGuid = 0x004D3790;   // cdecl () -> u64 (edx:eax)
static const BYTE  kGetActiveSig[] = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };
static const DWORD kObjectPtr = 0x004D4DB0;       // cdecl (lo, hi, typeMask, file, line)
static const BYTE  kObjectPtrSig[] = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C };
static const DWORD kEnumVisible = 0x004D4B30;     // cdecl (cb, arg)
static const BYTE  kEnumSig[] = { 0x55, 0x8B, 0xEC, 0xA1, 0xBC, 0x39, 0xD4, 0x00 };
static const DWORD kGetAuraCount = 0x004F8850;    // thiscall (unit) -> int
static const BYTE  kAuraCountSig[] = { 0x8B, 0x81, 0xD0, 0x0D, 0x00, 0x00, 0x83, 0xF8, 0xFF };
static const DWORD kGetAuraInfo = 0x004F8870;     // thiscall (unit, index) -> spellId
static const BYTE  kAuraInfoSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xB9, 0xD0, 0x0D, 0x00, 0x00, 0xFF };

typedef unsigned __int64 (__cdecl* GetActiveGuid_t)();
typedef void* (__cdecl* ObjectPtr_t)(DWORD lo, DWORD hi, DWORD typeMask, const char* file, int line);
typedef int   (__cdecl* EnumCb_t)(DWORD lo, DWORD hi, void* arg);
typedef int   (__cdecl* EnumVisible_t)(EnumCb_t cb, void* arg);
typedef int   (__thiscall* GetAuraCount_t)(void* unit);
typedef int   (__thiscall* GetAuraInfo_t)(void* unit, int index);

static const DWORD kTypeMaskUnitOrPlayer = 0x18;
static const DWORD kAnimJumpStart = 37;
static const DWORD kAnimJump = 38;
static const DWORD kAnimFall = 40;
static const DWORD kAnimFly = 135;

static const DWORD kOff_Model = 0xB4;
static const DWORD kOff_Desc = 0x08;
static const DWORD kDesc_Bytes0 = 0x5C;
static const DWORD kDesc_MountDisplay = 0x114;    // (OBJECT_END 6 + 0x3F) * 4
static const DWORD kDesc_Bytes2 = 0x1E8;          // UNIT_FIELD_BYTES_2 (6 + 0x74) * 4; byte 3 = form
static const DWORD kFormMoonkin = 31;
// The unit's embedded CMovement is at unit+0x788 (playercollide.cpp). Verified in
// Wow.exe 12340 2026-10-28: flags at CMovement+0x44 (0x1000 = FALLING); the start-fall
// routine 0x988370 writes the launch vertical speed to CMovement+0xB8 (negative = up)
// and zeroes the fall timer +0x80; 0x986E10 = current speed = [+0xB8] + t * gravity.
// 0x987410 re-bases a fall in mid-air ([+0xB8] := current speed), so +0xB8 only
// holds the launch speed near the start - hence the leap memory below.
static const DWORD kOff_MoveFlags = 0x788 + 0x44;
static const DWORD kOff_LaunchSpeed = 0x788 + 0xB8;
static const DWORD kMoveFalling = 0x1000;
static const DWORD kModel_Flags = 0x10;
static const DWORD kModel_Shared = 0x2C;
static const DWORD kShared_Header = 0x150;
static const DWORD kHdr_SeqCount = 0x1C;
static const DWORD kHdr_Seqs = 0x20;
static const DWORD kSeq_Stride = 0x40;
static const DWORD kSeq_Var = 0x02;
static const DWORD kSeq_Freq = 0x10;
static const DWORD kSeq_Next = 0x3C;

// ------------------------------------------------------------- settings/state
static int  g_on = 1;
static int  g_aura = 94301;
static int  g_chanceMale = 25;          // percent, like NightElfMale's 8191/32767
static int  g_chanceFemale = 50;        // like NightElfFemale's 16383/32767
static int  g_debug = 0;
static int  g_installed = 0;
static char g_log[MAX_PATH];
// Moonkin leap flap: Feral Charge - Moonkin (83111) launches at 0.5 * g * 2.5 s =
// 24.1 yd/s up (SpellEffects.cpp EffectJumpDest); a jump is 8. Anything launched
// faster than this while in Moonkin Form plays Fly until it lands.
static int   g_flapOn = 1;
static int   g_flapLaunch = 20;          // yd/s, upward
static DWORD g_flapLaunchBits = 0x41A00000;   // the float 20.0f; compared as bits in the hook

// test seams: the self-test swaps these for stubs (no game in that process)
static void* (*g_unitForModel)(void* model);
static bool  (*g_unitHasAura)(void* unit, int spellId);
static bool  (*g_unitIsFemale)(void* unit);
static bool  (*g_unitIsNightElf)(void* unit);
static bool  (*g_unitIsMounted)(void* unit);
static DWORD (*g_unitForm)(void* unit);
// falling? and the launch speed's raw float bits (CMovement+0xB8)
static bool  (*g_unitFall)(void* unit, DWORD* launchBits);

// A unit seen leaving the ground faster than g_flapLaunch keeps flapping until we
// see it on the ground again (the launch speed itself does not last, see above).
struct Leap { void* unit; DWORD tick; };
static const int kLeaps = 16;
static const DWORD kLeapMaxMs = 10000;
static Leap g_leap[kLeaps];
static int g_leapNext = 0;

static const DWORD kRaceNightElf = 4;

// The same jump can reach PlayAnimation more than once (whole model and a bone
// subset, or a re-issue in the same frame). Remember the last answer per model
// for a moment so one jump never half-flips.
struct Decision { void* model; DWORD tick; int variation; };
static const int kDecisions = 16;
static const DWORD kDecisionMs = 300;
static Decision g_dec[kDecisions];
static int g_decNext = 0;

static DWORD g_rng = 0;

// ------------------------------------------------------------- helpers
static void Log(const char* fmt, ...)
{
    if (!g_log[0])
        return;
    char line[512];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] jumpflip: ",
                        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    int m = _vsnprintf_s(line + n, sizeof(line) - n - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (m < 0)
        m = (int)strlen(line + n);
    n += m;
    line[n++] = '\r';
    line[n++] = '\n';
    HANDLE h = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD w = 0;
    WriteFile(h, line, (DWORD)n, &w, NULL);
    CloseHandle(h);
}

static DWORD Rand32()
{
    if (!g_rng) {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        g_rng = (DWORD)q.QuadPart ^ GetTickCount() ^ 0x9E3779B9u;
        if (!g_rng)
            g_rng = 1;
    }
    g_rng ^= g_rng << 13;           // xorshift32
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static bool Readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return false;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

// The loaded model's sequence array, or NULL (not loaded / unreadable).
static const BYTE* ModelSequences(const void* model, DWORD* count)
{
    const BYTE* m = (const BYTE*)model;
    if (!Readable(m, kModel_Shared + 4))
        return NULL;
    if (!(*(const DWORD*)(m + kModel_Flags) & 1))
        return NULL;                                     // not loaded yet
    const BYTE* shared = *(const BYTE* const*)(m + kModel_Shared);
    if (!Readable(shared, kShared_Header + 4))
        return NULL;
    const BYTE* hdr = *(const BYTE* const*)(shared + kShared_Header);
    if (!Readable(hdr, kHdr_Seqs + 4))
        return NULL;
    DWORD n = *(const DWORD*)(hdr + kHdr_SeqCount);
    const BYTE* seqs = *(const BYTE* const*)(hdr + kHdr_Seqs);
    if (!n || n > 4096 || !Readable(seqs, n * kSeq_Stride))
        return NULL;
    *count = n;
    return seqs;
}

// True when the model carries animation `animId` (any variation). Exported for the self-test.
extern "C" __declspec(dllexport) int JumpFlip_HasAnim(const void* model, DWORD animId)
{
    __try {
        DWORD n = 0;
        const BYTE* seqs = ModelSequences(model, &n);
        for (DWORD i = 0; seqs && i < n; ++i)
            if (*(const WORD*)(seqs + i * kSeq_Stride) == animId)
                return 1;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Chain position of the model's zero-weight JumpStart variation, or -1.
// Walks the in-memory M2 the same way the client does. Exported for the self-test.
extern "C" __declspec(dllexport) int JumpFlip_FlipPosition(const void* model)
{
    __try {
        DWORD n = 0;
        const BYTE* seqs = ModelSequences(model, &n);
        if (!seqs)
            return -1;
        DWORD head = 0xFFFFFFFF;
        for (DWORD i = 0; i < n; ++i) {
            const BYTE* s = seqs + i * kSeq_Stride;
            if (*(const WORD*)s == kAnimJumpStart && *(const WORD*)(s + kSeq_Var) == 0) {
                head = i;
                break;
            }
        }
        if (head == 0xFFFFFFFF)
            return -1;
        DWORD cur = head;
        for (int pos = 0; pos < 32 && cur < n; ++pos) {
            const BYTE* s = seqs + cur * kSeq_Stride;
            if (pos > 0 && *(const DWORD*)(s + kSeq_Freq) == 0)
                return pos;
            cur = *(const WORD*)(s + kSeq_Next);
        }
        return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// ------------------------------------------------------------- game-side seams
static GetActiveGuid_t pGetActiveGuid;
static ObjectPtr_t     pObjectPtr;
static EnumVisible_t   pEnumVisible;
static GetAuraCount_t  pGetAuraCount;
static GetAuraInfo_t   pGetAuraInfo;

struct FindCtx { void* model; void* unit; };

static int __cdecl FindCb(DWORD lo, DWORD hi, void* arg)
{
    FindCtx* c = (FindCtx*)arg;
    void* o = pObjectPtr(lo, hi, kTypeMaskUnitOrPlayer, "jumpflip.cpp", 0);
    if (o && *(void**)((BYTE*)o + kOff_Model) == c->model) {
        c->unit = o;
        return 0;                                        // found: stop
    }
    return 1;
}

static void* GameUnitForModel(void* model)
{
    __try {
        unsigned __int64 g = pGetActiveGuid();
        if (g) {
            void* me = pObjectPtr((DWORD)g, (DWORD)(g >> 32), kTypeMaskUnitOrPlayer,
                                  "jumpflip.cpp", 0);
            if (me && *(void**)((BYTE*)me + kOff_Model) == model)
                return me;                               // our own jump: the common case
        }
        FindCtx c = { model, NULL };
        pEnumVisible(FindCb, &c);
        return c.unit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static bool GameUnitHasAura(void* unit, int spellId)
{
    __try {
        int n = pGetAuraCount(unit);
        if (n < 0 || n > 256)
            n = 64;
        for (int i = 0; i < n; ++i)
            if (pGetAuraInfo(unit, i) == spellId)
                return true;
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool GameUnitIsFemale(void* unit)
{
    __try {
        const BYTE* desc = *(const BYTE* const*)((const BYTE*)unit + kOff_Desc);
        return desc && ((*(const DWORD*)(desc + kDesc_Bytes0) >> 16) & 0xFF) == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool GameUnitIsNightElf(void* unit)
{
    __try {
        const BYTE* desc = *(const BYTE* const*)((const BYTE*)unit + kOff_Desc);
        return desc && (*(const DWORD*)(desc + kDesc_Bytes0) & 0xFF) == kRaceNightElf;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Mounted units never flip. Tiger.m2 is both the travel form (HD Creatures off)
// and a Beast Rider mount (the hunter rides the pet's own model), and the rider
// would stay put while the tiger spun under them. Unreadable = mounted (no flip).
static bool GameUnitIsMounted(void* unit)
{
    __try {
        const BYTE* desc = *(const BYTE* const*)((const BYTE*)unit + kOff_Desc);
        return !desc || *(const DWORD*)(desc + kDesc_MountDisplay) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

// Shapeshift form (UNIT_FIELD_BYTES_2 byte 3), 0 when unreadable.
static DWORD GameUnitForm(void* unit)
{
    __try {
        const BYTE* desc = *(const BYTE* const*)((const BYTE*)unit + kOff_Desc);
        return desc ? (*(const DWORD*)(desc + kDesc_Bytes2) >> 24) & 0xFF : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool GameUnitFall(void* unit, DWORD* launchBits)
{
    __try {
        const BYTE* u = (const BYTE*)unit;
        *launchBits = *(const DWORD*)(u + kOff_LaunchSpeed);
        return (*(const DWORD*)(u + kOff_MoveFlags) & kMoveFalling) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *launchBits = 0;
        return false;
    }
}

// launch speed (raw float bits, negative = up) is faster upward than the threshold.
// Integer only: the hook runs at PlayAnimation entry and touches no FPU/SSE state.
static bool LaunchedFast(DWORD bits)
{
    if (!(bits & 0x80000000))
        return false;                                    // going down (or zero)
    DWORD mag = bits & 0x7FFFFFFF;
    return mag > g_flapLaunchBits && mag < 0x7F800000;   // |v| > threshold, not inf/NaN
}

// Moonkin leap: while a unit in Moonkin Form is in the air from a fast launch, its
// JumpStart / Jump / Fall become Fly (wings flapping). Returns true when it rewrote
// the request, so the jump flip leaves it alone.
static bool MoonkinFlap(void* model, DWORD* args)
{
    DWORD anim = args[1];
    if (!g_flapOn || (anim != kAnimJumpStart && anim != kAnimJump && anim != kAnimFall))
        return false;
    if (!JumpFlip_HasAnim(model, kAnimFly))
        return false;                                    // no flap to play (old model)
    void* unit = g_unitForModel(model);
    if (!unit || g_unitForm(unit) != kFormMoonkin)
        return false;
    DWORD now = GetTickCount();
    DWORD bits = 0;
    bool falling = g_unitFall(unit, &bits);
    Leap* mark = NULL;
    for (int i = 0; i < kLeaps; ++i)
        if (g_leap[i].unit == unit && now - g_leap[i].tick < kLeapMaxMs)
            mark = &g_leap[i];
    if (!falling) {
        if (mark)
            mark->unit = NULL;                           // on the ground: the leap is over
        return false;
    }
    if (!mark && LaunchedFast(bits)) {
        mark = &g_leap[g_leapNext];
        g_leapNext = (g_leapNext + 1) % kLeaps;
        mark->unit = unit;
        mark->tick = now;
    }
    if (!mark)
        return false;                                    // an ordinary jump or fall
    args[1] = kAnimFly;
    args[2] = (DWORD)-1;
    if (g_debug)
        Log("Moonkin leap: model=%p unit=%p anim %lu -> Fly (launch bits 0x%08lX)",
            model, unit, anim, bits);
    return true;
}

// ------------------------------------------------------------- the hook body
extern "C" void __cdecl JumpFlipAnim(void* model, DWORD* args)
{
    if (!g_installed || !g_on)
        return;
    if (MoonkinFlap(model, args))
        return;
    if (args[1] != kAnimJumpStart)
        return;
    if ((int)args[2] != -1)
        return;                                          // caller chose a variation itself

    DWORD now = GetTickCount();
    for (int i = 0; i < kDecisions; ++i) {
        if (g_dec[i].model == model && now - g_dec[i].tick < kDecisionMs) {
            if (g_dec[i].variation >= 0)
                args[2] = (DWORD)g_dec[i].variation;
            return;
        }
    }

    int pos = JumpFlip_FlipPosition(model);
    if (pos <= 0)
        return;                                          // stock model
    void* unit = g_unitForModel(model);
    int variation = -1;                                  // -1 = leave it to the engine
    int roll = -1, chance = -1;
    bool mounted = unit && g_unitIsMounted(unit);
    bool aura = unit && !mounted && g_unitHasAura(unit, g_aura);
    bool nightElf = false;
    if (aura) {
        chance = g_unitIsFemale(unit) ? g_chanceFemale : g_chanceMale;
        roll = (int)(Rand32() % 100);
        if (roll < chance) {
            variation = pos;                             // the flip (a double on night elves)
        } else {
            // Night elves already flip at these odds through the stock roll. With
            // Acrobatics their flip IS the double, so a miss is a plain jump rather
            // than the stock single flip.
            nightElf = g_unitIsNightElf(unit);
            if (nightElf)
                variation = 0;
        }
    }
    Decision* d = &g_dec[g_decNext];
    g_decNext = (g_decNext + 1) % kDecisions;
    d->model = model;
    d->tick = now;
    d->variation = variation;
    if (variation >= 0)
        args[2] = (DWORD)variation;
    if (g_debug)
        Log("JumpStart model=%p unit=%p mounted=%d aura=%d nightElf=%d flipPos=%d roll=%d chance=%d -> %s",
            model, unit, mounted ? 1 : 0, aura ? 1 : 0, nightElf ? 1 : 0, pos, roll, chance,
            variation > 0 ? "FLIP" : variation == 0 ? "plain jump" : "engine roll");
}

// ------------------------------------------------------------- setup
void JumpFlip_LoadSettings(const char* dir)
{
    _snprintf_s(g_log, sizeof(g_log), _TRUNCATE, "%sAnimSpeedFix.log", dir);
    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;                                          // shipped case: compiled defaults
    g_on = GetPrivateProfileIntA("JumpFlip", "Enabled", g_on, ini);
    g_aura = GetPrivateProfileIntA("JumpFlip", "Aura", g_aura, ini);
    g_chanceMale = GetPrivateProfileIntA("JumpFlip", "ChanceMale", g_chanceMale, ini);
    g_chanceFemale = GetPrivateProfileIntA("JumpFlip", "ChanceFemale", g_chanceFemale, ini);
    g_debug = GetPrivateProfileIntA("JumpFlip", "Debug", g_debug, ini);
    g_flapOn = GetPrivateProfileIntA("JumpFlip", "MoonkinFlap", g_flapOn, ini);
    g_flapLaunch = GetPrivateProfileIntA("JumpFlip", "MoonkinLaunch", g_flapLaunch, ini);
    if (g_flapLaunch < 1)
        g_flapLaunch = 1;
    float f = (float)g_flapLaunch;
    memcpy(&g_flapLaunchBits, &f, 4);
}

static bool Sig(DWORD at, const BYTE* sig, size_t n, const char* what)
{
    if (Readable((const void*)at, n) && memcmp((const void*)at, sig, n) == 0)
        return true;
    Log("disabled: signature mismatch for %s at 0x%08X", what, at);
    return false;
}

void JumpFlip_Install()
{
    if (!g_on)
        return;
    if (!Sig(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActiveGuid") ||
        !Sig(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr") ||
        !Sig(kEnumVisible, kEnumSig, sizeof(kEnumSig), "EnumVisibleObjects") ||
        !Sig(kGetAuraCount, kAuraCountSig, sizeof(kAuraCountSig), "GetAuraCount") ||
        !Sig(kGetAuraInfo, kAuraInfoSig, sizeof(kAuraInfoSig), "GetAuraInfo")) {
        g_on = 0;
        return;
    }
    pGetActiveGuid = (GetActiveGuid_t)kGetActiveGuid;
    pObjectPtr = (ObjectPtr_t)kObjectPtr;
    pEnumVisible = (EnumVisible_t)kEnumVisible;
    pGetAuraCount = (GetAuraCount_t)kGetAuraCount;
    pGetAuraInfo = (GetAuraInfo_t)kGetAuraInfo;
    g_unitForModel = GameUnitForModel;
    g_unitHasAura = GameUnitHasAura;
    g_unitIsFemale = GameUnitIsFemale;
    g_unitIsNightElf = GameUnitIsNightElf;
    g_unitIsMounted = GameUnitIsMounted;
    g_unitForm = GameUnitForm;
    g_unitFall = GameUnitFall;
    g_installed = 1;
    if (g_debug)
        Log("on: aura %d, male %d%%, female %d%%", g_aura, g_chanceMale, g_chanceFemale);
}

bool JumpFlip_WantsAnimHook()
{
    return g_on && g_installed;
}

// ------------------------------------------------------------- self-test seams
static void* g_testModel;
static void* g_testUnit;
static int   g_testAura;
static int   g_testFemale;
static int   g_testNightElf;
static int   g_testMounted;

static void* TestUnitForModel(void* model) { return model == g_testModel ? g_testUnit : NULL; }
static bool  TestHasAura(void*, int spellId) { return g_testAura && spellId == g_aura; }
static bool  TestIsFemale(void*) { return g_testFemale != 0; }
static bool  TestIsNightElf(void*) { return g_testNightElf != 0; }
static bool  TestIsMounted(void*) { return g_testMounted != 0; }
static DWORD g_testForm;
static int   g_testFalling;
static DWORD g_testLaunchBits;
static DWORD TestForm(void*) { return g_testForm; }
static bool  TestFall(void*, DWORD* bits) { *bits = g_testLaunchBits; return g_testFalling != 0; }

// Arms the module with stub resolvers: `model` belongs to `unit`, which has the
// aura when hasAura is set. Chances in percent. clear = forget earlier decisions.
// The unit is not mounted; JumpFlip_TestMounted(1) after this mounts it.
extern "C" __declspec(dllexport)
void JumpFlip_TestArm(void* model, void* unit, int hasAura, int female, int nightElf,
                      int chanceMale, int chanceFemale, int clear)
{
    g_testModel = model;
    g_testUnit = unit;
    g_testAura = hasAura;
    g_testFemale = female;
    g_testNightElf = nightElf;
    g_chanceMale = chanceMale;
    g_chanceFemale = chanceFemale;
    g_unitForModel = TestUnitForModel;
    g_unitHasAura = TestHasAura;
    g_unitIsFemale = TestIsFemale;
    g_unitIsNightElf = TestIsNightElf;
    g_testMounted = 0;
    g_unitIsMounted = TestIsMounted;
    g_testForm = 0;
    g_testFalling = 0;
    g_testLaunchBits = 0;
    g_unitForm = TestForm;
    g_unitFall = TestFall;
    g_flapOn = 1;
    g_on = 1;
    g_installed = 1;
    if (clear) {
        memset(g_dec, 0, sizeof(g_dec));
        memset(g_leap, 0, sizeof(g_leap));
    }
}

extern "C" __declspec(dllexport) void JumpFlip_TestMounted(int mounted)
{
    g_testMounted = mounted;
}

// The armed unit's shapeshift form, whether it is falling, and its launch speed
// (yd/s, negative = up) as CMovement+0xB8 would hold it.
extern "C" __declspec(dllexport) void JumpFlip_TestMove(int form, int falling, float launch)
{
    g_testForm = (DWORD)form;
    g_testFalling = falling;
    memcpy(&g_testLaunchBits, &launch, 4);
}
