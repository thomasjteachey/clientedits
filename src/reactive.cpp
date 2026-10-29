// reactive.cpp - forget a reactive ability's alert once the spell is unlearned.
//
// PROBLEM
// -------
// Owner 2026-10-29, All-Star arena: "still getting hammer of wrath notifications
// when i'm not a paladin?" - and before it, a bear-form druid life told to
// Execute and Overpower after a warrior life. The server does unlearn the old
// life (live log: "client sync: 70 spells learned, 1136 removed"), the
// spellbook and the known-spell bits follow, and still the floating combat
// text kept saying "Hammer of Wrath!".
//
// REVERSING (Wow.exe 3.3.5a 12340, verified statically)
// ---------------------------------------------------
//   COMBAT_TEXT_UPDATE type table   .data 0x00ADB820, "SPELL_ACTIVE" = index 0x23
//   FireCombatText("%s%s")          0x0074D520(type, text)
//   reactive scans                  0x0053CF10 and the loop at 0x0053D1F6 walk the
//                                   TRIGGER-ACTIVATE list and fire type 0x23 with the
//                                   spell's name when one turns usable
//                                   (0x0080B5D0) and is off cooldown (0x008017E0).
//                                   Neither asks whether the spell is still known.
//   the list                        count [0x00BE8E58], capacity [0x00BE8E54],
//                                   data  [0x00BE8E5C]; 8-byte entries
//                                   { DWORD spellId; BYTE usable; BYTE ready; pad }
//   who adds                        AddKnownSpell 0x00542030, for a spell whose
//                                   AttributesEx4 (record +0x20) has 0x200
//                                   (SPELL_ATTR4_TRIGGER_ACTIVATE) - one entry per
//                                   spell name, upgraded in place to the top rank
//                                   (0x00542297..0x00542431).
//   who empties                     only the full reset 0x0053EC40 (0x0053EE66..72),
//                                   reached from the active player's creation
//                                   (0x006E7F50 <- 0x006E8378) - world entry.
//   who never touches it            RemoveKnownSpell 0x0053FAD0: clears the known
//                                   bit ([0x00BE8DC4], [0x00BE8DC0] dwords), the
//                                   spellbook list [0x00BE8E28/2C] and a row of
//                                   single-slot globals - not the reactive list.
//   SMSG_REMOVED_SPELL (0x203)      0x006E7840 -> 0x006E71D0, whose call
//                                   0x006E7361 `call 0x0053FAD0` is the one we take.
//
// In retail nobody unlearned Execute mid-session, so this never mattered. The
// realm's All-Star swap does it every death.
//
// Spells the client flags (patch-enUS-A Spell.dbc, 19 names): Execute,
// Overpower, Revenge, Victory Rush, Hammer of Wrath, The Art of War, Riposte,
// Counterattack, Mongoose Bite, Kill Shot, Wolverine Bite, Deep Freeze,
// Maelstrom (Ready!), Decimation, Soul Fire!, Rune Strike, Overpower Ready!,
// Recharging Battery.
//
// FIX
// ---
// The REMOVED handler's call to RemoveKnownSpell is pointed at a wrapper that
// runs the original and then drops every reactive entry whose spell the known
// bits no longer hold. Only that call site: the other caller, the rank-up path
// 0x00542EB0 (SMSG_SUPERCEDED_SPELL), removes the old rank and at once re-adds
// the new one, which the list already upgrades in place - left exactly as it is.
//
// Edge kept as it was: if the top rank alone is unlearned while a lower rank
// stays, the name's entry goes and the lower rank stops announcing itself until
// the next world entry. Nothing on the realm does that today.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "reactive.h"

// ------------------------------------------------------------------ client
static const DWORD kRemovedCallSite  = 0x006E735E;   // push 0 / push ebx / call RemoveKnownSpell / push ebx / call ...
static const BYTE  kRemovedCallSig[] = { 0x6A, 0x00, 0x53, 0xE8, 0x6A, 0x87, 0xE5, 0xFF, 0x53, 0xE8, 0x24, 0x38, 0xEC, 0xFF };
static const DWORD kCallInsn         = 0x006E7361;   // the E8 itself

static const DWORD kRemoveKnownSpell    = 0x0053FAD0;
static const BYTE  kRemoveKnownSpellSig[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x0D, 0xC4, 0x8D, 0xBE, 0x00, 0x81, 0xEC, 0xB0, 0x02, 0x00, 0x00 };

// AddKnownSpell's append, to prove the entry layout this module assumes:
//   mov edx,[0xBE8E5C] / lea eax,[edx+ecx*8] / add ecx,1 / mov [0xBE8E58],ecx
//   mov ecx,[ebp+8] / mov [eax],ecx / mov byte [eax+4],0 / mov byte [eax+5],0
static const DWORD kAppendSite  = 0x00542416;
static const BYTE  kAppendSig[] = { 0x8B, 0x15, 0x5C, 0x8E, 0xBE, 0x00, 0x8D, 0x04, 0xCA, 0x83, 0xC1, 0x01,
                                    0x89, 0x0D, 0x58, 0x8E, 0xBE, 0x00, 0x8B, 0x4D, 0x08, 0x89, 0x08,
                                    0xC6, 0x40, 0x04, 0x00, 0xC6, 0x40, 0x05, 0x00 };

static const DWORD kKnownDwords   = 0x00BE8DC0;      // size of the known-spell bitset, in DWORDs
static const DWORD kKnownBits     = 0x00BE8DC4;      // DWORD* bitset
static const DWORD kReactiveCount = 0x00BE8E58;
static const DWORD kReactiveData  = 0x00BE8E5C;      // 8-byte entries, spell id first

typedef int (__cdecl* RemoveKnownSpell_t)(int spellId, int flag);

// ---------------------------------------------------------------- settings
static char g_dir[MAX_PATH];
static int  g_enabled = 1;
static int  g_debug = 0;
static int  g_installed = 0;

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
                          "[reactive pid%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (len < 0) return;

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, line, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

static bool Readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (SIZE_T)((const BYTE*)p - (const BYTE*)mbi.BaseAddress) + n <= mbi.RegionSize;
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

void Reactive_LoadSettings(const char* dir)
{
    if (dir)
        strncpy_s(g_dir, sizeof(g_dir), dir, _TRUNCATE);
    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    g_enabled = GetPrivateProfileIntA("ReactiveList", "Enabled", 1, ini);
    g_debug   = GetPrivateProfileIntA("ReactiveList", "Debug", 0, ini);
}

// ------------------------------------------------------------------- prune
static bool StillKnown(DWORD spellId)
{
    DWORD const* bits = *(DWORD const**)kKnownBits;
    DWORD const dwords = *(DWORD const*)kKnownDwords;
    if (!bits || (spellId >> 5) >= dwords)
        return false;
    return (bits[spellId >> 5] & (1u << (spellId & 31))) != 0;
}

static void PruneReactive()
{
    DWORD const count = *(DWORD const*)kReactiveCount;
    BYTE* const data = *(BYTE**)kReactiveData;
    if (!count || !data || !Readable(data, count * 8))
        return;

    DWORD kept = 0;
    for (DWORD i = 0; i < count; ++i) {
        DWORD const spellId = *(DWORD const*)(data + i * 8);
        if (!StillKnown(spellId)) {
            if (g_debug)
                Log("dropped reactive spell %lu (no longer known)", spellId);
            continue;
        }
        if (kept != i)
            memcpy(data + kept * 8, data + i * 8, 8);
        ++kept;
    }
    if (kept != count)
        *(DWORD*)kReactiveCount = kept;          // capacity [0xBE8E54] and the buffer stay the client's
}

// The REMOVED handler's call lands here: same arguments, same cleanup (the
// caller pops them), same return value.
static int __cdecl RemoveKnownSpellThenPrune(int spellId, int flag)
{
    int const result = ((RemoveKnownSpell_t)kRemoveKnownSpell)(spellId, flag);
    PruneReactive();
    return result;
}

// ------------------------------------------------------------------ install
void Reactive_Install()
{
    if (!g_enabled || g_installed)
        return;

    if (!VerifySig(kRemovedCallSite, kRemovedCallSig, sizeof(kRemovedCallSig), "REMOVED_SPELL call") ||
        !VerifySig(kRemoveKnownSpell, kRemoveKnownSpellSig, sizeof(kRemoveKnownSpellSig), "RemoveKnownSpell") ||
        !VerifySig(kAppendSite, kAppendSig, sizeof(kAppendSig), "reactive list append")) {
        g_enabled = 0;
        return;
    }

    // Retarget the rel32 of that one call. Five bytes, no trampoline: the call
    // still returns to 0x006E7366 and the caller still cleans its two pushes.
    BYTE* p = (BYTE*)kCallInsn;
    DWORD old = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed at 0x%08X - disabled", kCallInsn);
        g_enabled = 0;
        return;
    }
    *(DWORD*)(p + 1) = (DWORD)&RemoveKnownSpellThenPrune - (kCallInsn + 5);
    DWORD ignored = 0;
    VirtualProtect(p, 5, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), p, 5);

    g_installed = 1;
    Log("installed: SMSG_REMOVED_SPELL now prunes the trigger-activate list (debug=%d)", g_debug);
}
