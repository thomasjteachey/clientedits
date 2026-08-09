// Combat/packet tracer for Wow.exe 3.3.5a 12340.
//
// Purpose: distinguish "the attack packet was processed late" from "the packet
// arrived on time but the swing animation was held". Logs, with a common
// high-resolution clock:
//
//   OP   - every opcode the client dispatches   (hook at 0x632013, esi = opcode)
//   ANIM - every CM2Model::PlayAnimation call   (hook at 0x832AB0)
//
// Read the resulting CSV with tracefmt.py, which resolves opcode names from the
// server's Opcodes.h and animation names from AnimationData.dbc.
//
// Design constraints that matter here:
//   * NO floating point anywhere in the hook path. PlayAnimation is called from
//     x87-heavy code and pushad/pushfd do not save FPU state, so timestamps are
//     logged as raw QueryPerformanceCounter ticks and the float rate argument as
//     its raw bit pattern. tracefmt.py converts both.
//   * Records are appended to a memory buffer under a critical section and
//     flushed in blocks; packet dispatch can run off the main thread.
//   * Everything is read-only with respect to game state.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

// hook sites -----------------------------------------------------------------

// 0x632013: mov eax, [edi + esi*4 + 0x53c]   (handler = table[opcode], esi = opcode)
static const DWORD kOpcodeSite  = 0x00632013;
static const BYTE  kOpcodeSig[] = { 0x8B, 0x84, 0xB7, 0x3C, 0x05, 0x00, 0x00 };

// 0x832AB0: push ebp / mov ebp,esp / sub esp,0x20   (CM2Model::PlayAnimation)
static const DWORD kAnimSite  = 0x00832AB0;
static const BYTE  kAnimSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20 };

// 0x826C40: the sequence setter PlayAnimation ultimately calls.
//   ecx = CM2Model, arg0 [ebp+8] = sequence index, arg1 [ebp+0xC] = SLOT index
//   (the slot is scaled by 0xAC into the track array at [model+0x94]).
// Logging this settles whether two animations share a track - i.e. whether one
// can actually clobber the other, or whether they play concurrently.
// Steal 7 bytes: push ebp / mov ebp,esp / push ecx / mov edx,[ebp+8].
static const DWORD kSlotSite  = 0x00826C40;
static const BYTE  kSlotSig[] = { 0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x55, 0x08 };

// ----------------------------------------------------------------------------

static int   g_traceOpcodes = 0;
static int   g_traceAnims = 0;
static int   g_traceSkipStand = 1;   // drop idle Stand re-plays (97% of records)
static int   g_traceOn = 0;
static char  g_tracePath[MAX_PATH];

static CRITICAL_SECTION g_traceLock;
static HANDLE g_traceFile = INVALID_HANDLE_VALUE;

#define TRACE_BUF 0x8000
static char  g_buf[TRACE_BUF + 512];
static int   g_bufLen = 0;

static DWORD g_opcodeRet = 0;
static DWORD g_animRet = 0;
static DWORD g_slotRet = 0;
static int   g_traceSlots = 0;

// Last time a packet arrived that means "a NEW attack really happened":
// SMSG_ATTACKERSTATEUPDATE, SMSG_SPELL_START/GO, SMSG_SPELLNONMELEEDAMAGELOG.
// The client's own duplicate re-issue of a swing has no such packet behind it,
// which is what separates it from a genuine second attack. A rogue's Eviscerate
// lands well inside the replay window and must NOT be suppressed - that
// regression is exactly what this gate exists to prevent.
static LONGLONG g_lastCombatPacket = 0;

static void TraceFlushLocked()
{
    if (g_bufLen == 0 || g_traceFile == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(g_traceFile, g_buf, (DWORD)g_bufLen, &written, NULL);
    g_bufLen = 0;
}

// Integer-only record append. No CRT float formatting - see header comment.
static void TraceRecord(const char* kind, unsigned a, unsigned b, unsigned c)
{
    if (!g_traceOn)
        return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    EnterCriticalSection(&g_traceLock);
    int n = _snprintf_s(g_buf + g_bufLen, sizeof(g_buf) - g_bufLen, _TRUNCATE,
                        "%I64d,%s,%u,%u,%u\r\n", now.QuadPart, kind, a, b, c);
    if (n > 0)
        g_bufLen += n;
    if (g_bufLen >= TRACE_BUF)
        TraceFlushLocked();
    LeaveCriticalSection(&g_traceLock);
}

extern "C" void __cdecl TraceOpcode(unsigned opcode)
{
    // tracked even when tracing is off - DropSlot3Replay depends on it
    if (opcode == 0x14A || opcode == 0x131 || opcode == 0x132 || opcode == 0x250) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_lastCombatPacket = now.QuadPart;
    }
    if (g_traceOpcodes)
        TraceRecord("OP", opcode, 0, 0);
}

// --- small per-model timestamp tables, shared by the guard and replay logic --
#define GUARD_SLOTS 64

static void NoteTime(unsigned* models, LONGLONG* when, unsigned model, LONGLONG now)
{
    unsigned slot = (model >> 4) % GUARD_SLOTS;
    for (unsigned i = 0; i < GUARD_SLOTS; ++i) {
        unsigned s = (slot + i) % GUARD_SLOTS;
        if (models[s] == model || models[s] == 0) {
            models[s] = model;
            when[s] = now;
            return;
        }
    }
    models[slot] = model;                        // table full: evict
    when[slot] = now;
}

// 0 if this model has no entry
static LONGLONG LookupTime(unsigned* models, LONGLONG* when, unsigned model)
{
    unsigned slot = (model >> 4) % GUARD_SLOTS;
    for (unsigned i = 0; i < GUARD_SLOTS; ++i) {
        unsigned s = (slot + i) % GUARD_SLOTS;
        if (models[s] == model)
            return when[s];
        if (models[s] == 0)
            return 0;
    }
    return 0;
}

static bool RecentFor(unsigned* models, LONGLONG* when, unsigned model,
                      LONGLONG now, LONGLONG window)
{
    LONGLONG t = LookupTime(models, when, model);
    return t != 0 && (now - t) < window;
}


// While moving, the client puts the swing on slot 3 (the track that actually
// renders in that case) and then re-issues the SAME swing on slot 0 ~180-215 ms
// later. That re-issue is a second, spurious swing - the "flush". Dropping it
// leaves the correctly-timed slot-3 swing alone.
static unsigned g_s3Model[GUARD_SLOTS];
static LONGLONG g_s3When[GUARD_SLOTS];
static unsigned g_s3AnimId[GUARD_SLOTS];
static LONGLONG g_replayTicks = 0;      // upper bound of the re-issue band
static LONGLONG g_replayMinTicks = 0;   // lower bound - the re-issue is never early
static LONGLONG g_causeTicks = 0;       // "a packet this recent CAUSED this anim"
static int      g_dropReplay = 0;

static void NoteS3(unsigned model, unsigned animId, LONGLONG now)
{
    unsigned slot = (model >> 4) % GUARD_SLOTS;
    for (unsigned i = 0; i < GUARD_SLOTS; ++i) {
        unsigned s = (slot + i) % GUARD_SLOTS;
        if (g_s3Model[s] == model || g_s3Model[s] == 0) {
            g_s3Model[s] = model;
            g_s3When[s] = now;
            g_s3AnimId[s] = animId;
            return;
        }
    }
    g_s3Model[slot] = model;                     // table full: evict
    g_s3When[slot] = now;
    g_s3AnimId[slot] = animId;
}

static LONGLONG LookupS3(unsigned model, unsigned* animOut)
{
    unsigned slot = (model >> 4) % GUARD_SLOTS;
    for (unsigned i = 0; i < GUARD_SLOTS; ++i) {
        unsigned s = (slot + i) % GUARD_SLOTS;
        if (g_s3Model[s] == model) {
            if (animOut)
                *animOut = g_s3AnimId[s];
            return g_s3When[s];
        }
        if (g_s3Model[s] == 0)
            return 0;
    }
    return 0;
}

// When the base track (slot 0) is busy - typically with Run - the client puts
// the swing on slot 3 instead, and then re-issues the SAME swing on slot 0
// ~180-215 ms later. That is the "swallowed and spat back out" swing, and it
// happens with SwingGuard disabled too, so it is stock client behaviour.
//
// ForceAttackSlot0 redirects that swing to slot 0 immediately. Experimental:
// slot 3 may exist for blending, and forcing 0 stomps whatever is there (Run is
// re-issued constantly, so it should recover). Returns the slot to actually use.
static int      g_forceSlot0 = 0;
static unsigned g_pendingAttackModel = 0;   // set by AnimHook microseconds earlier
static unsigned g_pendingAttackAnim = 0;    // ditto - which attack went to slot 3

extern "C" unsigned __cdecl TraceSlot(unsigned model, unsigned seqIndex, unsigned slot)
{
    if (g_traceSlots)
        TraceRecord("SLOT", seqIndex, slot, model);

    // remember that this model's swing went to slot 3, so the late slot-0
    // re-issue can be recognised as a duplicate
    if (slot == 3 && model && model == g_pendingAttackModel) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        NoteS3(model, g_pendingAttackAnim, now.QuadPart);
        g_pendingAttackModel = 0;

        if (g_forceSlot0) {
            if (g_traceSlots)
                TraceRecord("FORCE", seqIndex, 0, model);
            return 0;
        }
    }
    return slot;
}

// --------------------------------------------------------------- SwingGuard
//
// Proven with slot logging (hook at 0x826C40): attack and spell animations are
// written to the SAME track, so the later one destroys the earlier. Measured on
// 12 packet-driven swings:
//
//   SpellCastDirected  +0.19 .. 0.69 ms   4 swings   <- same frame, never renders
//   Walkbackwards      +41, +278 ms       2 swings   <- normal interrupt
//   Run                +87 ms             1 swing    <- normal interrupt
//
// Only the same-frame case is a bug: the swing is destroyed before a single
// frame draws it. A swing cut short 40-280 ms in has already been seen, and
// suppressing movement to "save" it would freeze the character.
//
// So the rule is deliberately animation-agnostic and time-tight: drop anything
// that would take an attack animation's place within ProtectMs (about one
// frame). Attack animations are never suppressed, nor is anything listed in
// NeverSuppress.

#define MAX_ANIM_ID 1024

static int      g_guardOn = 0;
static LONGLONG g_guardTicks = 0;                // window, in QPC ticks
static BYTE     g_isAttack[MAX_ANIM_ID];
static BYTE     g_neverSuppress[MAX_ANIM_ID];
static unsigned g_gModel[GUARD_SLOTS];           // model ptr -> last attack time
static LONGLONG g_gWhen[GUARD_SLOTS];
static unsigned g_guardHits = 0;                 // suppressions, for reporting

// Set by the helper, read by the stub immediately after, same thread. A race
// could at worst mis-handle one animation; it cannot corrupt anything.
static int g_suppress = 0;

static void GuardNoteAttack(unsigned model, LONGLONG now)
{
    NoteTime(g_gModel, g_gWhen, model, now);
}

static bool GuardAttackActive(unsigned model, LONGLONG now)
{
    return RecentFor(g_gModel, g_gWhen, model, now, g_guardTicks);
}

// The model pointer is logged so player and target animations can be told
// apart - without it every unit's animations are an undifferentiated stream,
// and idle Stand re-plays drown everything else.
//
// Also decides SwingGuard suppression; g_suppress is read by the stub right
// after this returns.
extern "C" void __cdecl TraceAnim(unsigned model, unsigned animId, unsigned rateBits)
{
    g_suppress = 0;

    if (g_guardOn && model && animId < MAX_ANIM_ID) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_isAttack[animId]) {
            // The slot-0 re-issue of a swing already placed on slot 3 has a
            // fixed fingerprint, and ALL THREE parts of it are required:
            //
            //   1. same animation as the slot-3 placement, in the measured
            //      re-issue band (183-214 ms observed; band is Min..Max).
            //      A lower bound matters: when a packet batch is dispatched
            //      and rendered in one frame (auto swing + Eviscerate), the
            //      second GENUINE attack lands milliseconds after the slot-3
            //      placement - the re-issue never comes that early.
            //   2. no combat packet CLOSE BEFORE this anim. A genuine attack
            //      is rendered within a frame or two of the packet that
            //      caused it; the re-issue is an orphan. The old rule
            //      ("no packet since the placement") failed both ways: the
            //      batched frame above ate Eviscerate, and a rogue's poison
            //      procs (SPELL_GO/DAMAGELOG stream) held the gate open so
            //      real duplicates were never dropped while strafing.
            unsigned s3anim = 0;
            LONGLONG t3 = g_dropReplay ? LookupS3(model, &s3anim) : 0;
            LONGLONG dt = now.QuadPart - t3;
            if (t3 != 0 && s3anim == animId &&
                dt >= g_replayMinTicks && dt < g_replayTicks &&
                (now.QuadPart - g_lastCombatPacket) > g_causeTicks) {
                g_suppress = 1;
                ++g_guardHits;
                g_pendingAttackModel = 0;
                if (g_traceAnims)
                    TraceRecord("DROP2", animId, rateBits, model);
                return;
            }
            GuardNoteAttack(model, now.QuadPart);
            g_pendingAttackModel = model;   // SlotHook fires microseconds later
            g_pendingAttackAnim = animId;
        } else if (!g_neverSuppress[animId] && GuardAttackActive(model, now.QuadPart)) {
            g_suppress = 1;
            ++g_guardHits;
            if (g_traceAnims)
                TraceRecord("DROP", animId, rateBits, model);
            return;
        }
    }

    if (!g_traceAnims)
        return;
    if (g_traceSkipStand && animId == 0)     // Stand is ~97% of all records
        return;
    TraceRecord("ANIM", animId, rateBits, model);
}

// hooks ----------------------------------------------------------------------

// Entered by a jmp planted at 0x632013, so registers are exactly as the client
// left them: esi = opcode, edi = NetClient.
__declspec(naked) static void OpcodeHook()
{
    __asm {
        pushad
        pushfd
        push esi                        // opcode
        call TraceOpcode
        add  esp, 4
        popfd
        popad

        _emit 0x8B                      // mov eax, [edi + esi*4 + 0x53c]
        _emit 0x84
        _emit 0xB7
        _emit 0x3C
        _emit 0x05
        _emit 0x00
        _emit 0x00

        push g_opcodeRet
        ret
    }
}

// Entered by a jmp planted at 0x826C40, before the prologue:
//   ecx = CM2Model, [esp+4] = sequence index, [esp+8] = slot index
__declspec(naked) static void SlotHook()
{
    __asm {
        pushad
        pushfd
        mov  eax, esp
        add  eax, 36                    // pushad(32) + pushfd(4) -> original esp
        push dword ptr [eax + 8]        // arg1: slot
        push dword ptr [eax + 4]        // arg0: sequence index
        push ecx                        // this (CM2Model*)
        call TraceSlot                  // returns the slot to actually use
        add  esp, 12
        mov  edx, esp
        add  edx, 36                    // original esp (flags restored just below)
        mov  dword ptr [edx + 8], eax   // write the slot back into arg1
        popfd
        popad

        _emit 0x55                      // push ebp
        _emit 0x8B                      // mov ebp, esp
        _emit 0xEC
        _emit 0x51                      // push ecx
        _emit 0x8B                      // mov edx, [ebp+8]
        _emit 0x55
        _emit 0x08

        push g_slotRet
        ret
    }
}

// Entered by a jmp planted at 0x832AB0, i.e. before the prologue - so the stack
// is still exactly as at function entry and ecx still holds `this`:
//   [esp+0]=retaddr [esp+4]=arg0 [esp+8]=arg1(animId) ... [esp+0x14]=arg4(float rate)
__declspec(naked) static void AnimHook()
{
    __asm {
        pushad
        pushfd
        mov  eax, esp
        add  eax, 36                    // pushad(32) + pushfd(4) -> original esp
        push dword ptr [eax + 0x14]     // arg4: rate, as raw bits (no FPU)
        push dword ptr [eax + 8]        // arg1: animation id
        push ecx                        // this (CM2Model*)
        call TraceAnim
        add  esp, 12
        popfd
        popad

        // flags are dead here: both paths below set them themselves, and the
        // original prologue's `sub esp,0x20` would clobber them anyway
        cmp  dword ptr [g_suppress], 0
        jne  suppress

        _emit 0x55                      // push ebp
        _emit 0x8B                      // mov ebp, esp
        _emit 0xEC
        _emit 0x83                      // sub esp, 0x20
        _emit 0xEC
        _emit 0x20

        push g_animRet
        ret

    suppress:
        // Skip PlayAnimation entirely, emulating its `ret 0x1C`: drop the
        // return address and the 7 stack args. eax/ecx/edx are volatile under
        // thiscall, so using edx as scratch is safe.
        pop  edx                        // return address
        add  esp, 0x1C                  // 7 dword args
        xor  eax, eax                   // return 0
        jmp  edx
    }
}
