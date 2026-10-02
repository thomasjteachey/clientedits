// sndguard.cpp - keeps a NaN sound position from crashing the client.
//
// THE CRASH
// ---------
// ERROR #132, 0xC0000094 INT_DIVIDE_BY_ZERO at 0x0094BF6B. First reported
// 2026-10-01 from Arathi Basin. 0x0094BD50 is FMOD Ex's per-channel 3D update
// (slot 1 of the vtables at 0x00A98E30 / 0x00A99EE0, run on FMOD's mixer
// thread). It projects the source onto the listener axes, scales by 256.0
// (0x009E2A4C), converts with cvttsd2si (0x0088B9C0) and computes the
// speaker-pan angle with a fixed-point atan2:
//
//   0x0094BF32  if (esi == 0 && edi == 0) { [ebp-4] = 0; goto 0x0094BFDD; }
//   0x0094BF44  edi <<= 10;  ecx = |edi| + 1;  esi <<= 10;
//   0x0094BF5A  esi >= 0:  [ebp-4] = (ecx + esi) >> 10
//   0x0094BF67      mov ecx,[ebp-4] / cdq / idiv ecx        <- crash site
//   0x0094BF81  esi <  0:  [ebp-4] = (ecx - esi) >> 10
//   0x0094BF8E      mov ecx,[ebp-4] / cdq / idiv ecx        <- same, other half
//   0x0094BFDD  fild [ebp-4] -> angle in degrees -> [channel+0x98]
//
// cvttsd2si returns 0x80000000 for NaN, inf or anything out of int range. That
// passes the "both zero" test, the << 10 wraps it to 0, and the divisor becomes
// (1 + 0) >> 10 = 0. The player's crash dump had the source x/y/z all
// 0xFFC00000 (NaN) with a healthy listener; ESI = EDI = 0 after the shifts.
//
// THE GUARD
// ---------
// For any real position the divisor is >= 1: either shifted value is a nonzero
// multiple of 1024, and the sums cannot go below 1 without wrapping. So a
// divisor <= 0 only ever comes from a garbage position. (<= 0 and not just == 0:
// a wrapped sum can come out at -1, and 0x80000000 / -1 faults too.)
//
// Each divide is the 6-byte `8B 4D FC 99 F7 F9`, room for a 5-byte jmp. The
// stub re-does the load and, when the divisor is bad, takes exactly the stock
// "no direction" exit the client already uses at 0x0094BF3C: [ebp-4] = 0,
// jmp 0x0094BFDD. No floating-point state is touched between the ftol calls and
// either jump, so the x87 stack is the same as on that stock path. The broken
// channel pans centre for that update; the NaN stays in FMOD's float math
// (volume, doppler), where it is harmless. The guard does not fix whatever gave
// the emitter a NaN position - it stops that from taking the whole client down.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "sndguard.h"

// ------------------------------------------------------------------ client ABI
static const BYTE  kDivSig[] = { 0x8B, 0x4D, 0xFC, 0x99, 0xF7, 0xF9 };

static const DWORD kSiteA   = 0x0094BF67;   // esi >= 0 half (the reported crash)
static const DWORD kSiteB   = 0x0094BF8E;   // esi <  0 half
static DWORD       g_resumeA = 0x0094BF6D;  // instruction after site A's idiv
static DWORD       g_resumeB = 0x0094BF94;  // instruction after site B's idiv

// The common tail: fild dword ptr [ebp-4]. Also the target of the stock
// zero-direction jmp at 0x0094BF3F (E9 99 00 00 00).
static DWORD       g_tail    = 0x0094BFDD;
static const BYTE  kTailSig[]      = { 0xDB, 0x45, 0xFC };
static const DWORD kStockZeroJmp   = 0x0094BF3C;
static const BYTE  kStockZeroSig[] = { 0x89, 0x75, 0xFC, 0xE9, 0x99, 0x00, 0x00, 0x00 };

// --------------------------------------------------------------- settings
static int  g_enabled = 1;
static char g_dir[MAX_PATH];
static volatile LONG g_trips = 0;

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
                          "[sndguard pid%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (len < 0) return;

    // FILE_SHARE_WRITE: two test clients share one log file.
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

// ---------------------------------------------------------------- the stubs
//
// Runs on FMOD's mixer thread. A NaN emitter is re-updated every mix tick, so
// only the first few trips are logged - enough to see it happened, never a
// file write per tick.
extern "C" void __cdecl SndGuardTripped(int which)
{
    LONG n = InterlockedIncrement(&g_trips);
    if (n <= 3)
        Log("blocked a divide by zero in the 3D pan (site %c, trip %ld): a sound "
            "had a NaN/inf position", which ? 'B' : 'A', n);
}

__declspec(naked) static void DivStubA()
{
    __asm {
        mov  ecx, dword ptr [ebp-4]
        test ecx, ecx
        jle  bad
        cdq
        idiv ecx
        jmp  [g_resumeA]
    bad:
        mov  dword ptr [ebp-4], 0
        pushad
        pushfd
        push 0
        call SndGuardTripped
        add  esp, 4
        popfd
        popad
        jmp  [g_tail]
    }
}

__declspec(naked) static void DivStubB()
{
    __asm {
        mov  ecx, dword ptr [ebp-4]
        test ecx, ecx
        jle  bad
        cdq
        idiv ecx
        jmp  [g_resumeB]
    bad:
        mov  dword ptr [ebp-4], 0
        pushad
        pushfd
        push 1
        call SndGuardTripped
        add  esp, 4
        popfd
        popad
        jmp  [g_tail]
    }
}

// ------------------------------------------------------------------ settings
void SndGuard_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir ? dir : "");

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);

    // Compiled-in default ON: the shipped zip carries no ini.
    g_enabled = GetPrivateProfileIntA("SoundGuard", "Enabled", 1, ini);
}

// ------------------------------------------------------------------- install
static bool PatchJump(DWORD site, void* target, size_t len)
{
    BYTE* p = (BYTE*)site;
    DWORD old = 0;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed at 0x%08X", site);
        return false;
    }
    // No race to worry about: Wow.exe imports DINPUT8.dll statically, so this
    // runs from DllMain before the game's entry point and FMOD's mixer thread
    // does not exist yet. The trailing byte becomes a nop; nothing jumps into
    // the middle of the replaced span.
    p[5] = 0x90;
    *(DWORD*)(p + 1) = (DWORD)target - (site + 5);
    p[0] = 0xE9;
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return true;
}

void SndGuard_Install()
{
    if (!g_enabled)
        return;

    if (!VerifySig(kSiteA,        kDivSig,       sizeof(kDivSig),       "pan divide A") ||
        !VerifySig(kSiteB,        kDivSig,       sizeof(kDivSig),       "pan divide B") ||
        !VerifySig(g_tail,        kTailSig,      sizeof(kTailSig),      "pan tail") ||
        !VerifySig(kStockZeroJmp, kStockZeroSig, sizeof(kStockZeroSig), "stock zero path")) {
        g_enabled = 0;
        return;
    }

    if (!PatchJump(kSiteA, (void*)&DivStubA, sizeof(kDivSig)) ||
        !PatchJump(kSiteB, (void*)&DivStubB, sizeof(kDivSig))) {
        Log("could not patch the pan divides - disabled");
        g_enabled = 0;
        return;
    }

    Log("installed at 0x%08X and 0x%08X", kSiteA, kSiteB);
}
