// Self-test for AnimSpeedFix without needing the game.
//
// Reproduces the client's gate in scratch memory:
//
//     <site>      jnp <skip>      ; taken when sequence.movespeed == 0
//     <site+6>    <scaling path>  ; rate = speed / movespeed
//     <skip>      <rate stays 1.0>
//
// then has the DLL patch it and checks that listed animations are forced onto
// the skip path while everything else still behaves exactly as before.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <math.h>

static const BYTE kGateSig[] = { 0x0F, 0x8B, 0xC8, 0x00, 0x00, 0x00 };  // jnp +0xC8
static const DWORD SCALING_PATH = 0x0BADF00D;
static const DWORD SKIP_PATH    = 0xDEADBEEF;   // rate stays 1.0

static DWORD g_site = 0;

typedef BOOL (*InstallAtFn)(DWORD, char*, int);

static int g_fail = 0;
static void Check(const char* what, bool ok, const char* detail = "")
{
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (!ok) g_fail++;
}

// Enters the gate with esi = animId and PF derived from the second argument:
//   0 -> test 0,0 -> even parity -> PF=1 -> jnp NOT taken -> scaling path
//   1 -> test 1,1 -> odd  parity -> PF=0 -> jnp taken     -> skip
// In the client PF comes from `test ah, 0x44` after comparing 0.0 with
// sequence.movespeed, so "falls through" == "movespeed != 0" == would scale.
#define MOVESPEED_NONZERO 0
#define MOVESPEED_ZERO    1

__declspec(naked) static DWORD __cdecl CallGate(int, int)
{
    __asm {
        push ebx
        push esi
        mov  esi, dword ptr [esp + 12]      // animId
        mov  eax, dword ptr [esp + 16]      // parity selector
        mov  ebx, g_site
        test al, al                         // sets PF; call does not disturb it
        call ebx
        pop  esi
        pop  ebx
        ret
    }
}

static DWORD g_play = 0;
static DWORD g_espDelta = 0;      // esp drift measured across the call itself

// Calls the synthetic PlayAnimation the way the client does: __thiscall,
// ecx = model, 7 stack args, callee cleans up (ret 0x1C).
//
// esp is snapshotted immediately before the argument pushes and compared
// immediately after the call returns, so the check is the real invariant and
// is immune to whatever the optimiser does around the call site.
__declspec(naked) static DWORD __cdecl CallPlay(unsigned, int)
{
    __asm {
        push ebx
        push esi
        mov  eax, dword ptr [esp + 16]     // animId
        mov  esi, esp                      // snapshot before the arg pushes
        push 1                             // arg6
        push 1                             // arg5
        push 0                             // arg4 rate
        push 0                             // arg3
        push 0                             // arg2
        push eax                           // arg1 = animId
        push -1                            // arg0
        mov  ecx, dword ptr [esp + 40]     // model
        mov  ebx, g_play
        call ebx                           // callee must pop the 7 args
        sub  esi, esp                      // 0 == correctly cleaned up
        mov  g_espDelta, esi
        pop  esi
        pop  ebx
        ret
    }
}

// Same call shape, with the variation (arg2) chosen by the test. Against a
// synthetic PlayAnimation that returns arg2, the result shows whether the hook
// rewrote the variation.
__declspec(naked) static DWORD __cdecl CallPlayVar(unsigned, int, int)
{
    __asm {
        push ebx
        push esi
        mov  eax, dword ptr [esp + 16]     // animId
        mov  edx, dword ptr [esp + 20]     // variation
        mov  esi, esp                      // snapshot before the arg pushes
        push 1                             // arg6
        push 1                             // arg5
        push 0                             // arg4 rate
        push 0                             // arg3
        push edx                           // arg2 = variation
        push eax                           // arg1 = animId
        push -1                            // arg0
        mov  ecx, dword ptr [esp + 40]     // model
        mov  ebx, g_play
        call ebx
        sub  esi, esp
        mov  g_espDelta, esi
        pop  esi
        pop  ebx
        ret
    }
}

// In-memory M2 just deep enough for JumpFlip_FlipPosition:
// model+0x10 loaded flag, model+0x2C -> shared, shared+0x150 -> header,
// header+0x1C/+0x20 -> sequences (stride 0x40: id, variation, frequency, next).
struct FakeSeq { WORD id, var; DWORD freq; WORD next; };
static BYTE* BuildFakeModel(BYTE* mem, const FakeSeq* s, int n, bool loaded)
{
    memset(mem, 0, 0x1000);
    BYTE* model = mem;
    BYTE* shared = mem + 0x200;
    BYTE* hdr = mem + 0x400;
    BYTE* seqs = mem + 0x600;
    *(DWORD*)(model + 0x10) = loaded ? 1 : 0;
    *(BYTE**)(model + 0x2C) = shared;
    *(BYTE**)(shared + 0x150) = hdr;
    *(DWORD*)(hdr + 0x1C) = (DWORD)n;
    *(BYTE**)(hdr + 0x20) = seqs;
    for (int i = 0; i < n; ++i) {
        BYTE* q = seqs + i * 0x40;
        *(WORD*)q = s[i].id;
        *(WORD*)(q + 2) = s[i].var;
        *(DWORD*)(q + 0x10) = s[i].freq;
        *(WORD*)(q + 0x3C) = s[i].next;
    }
    return model;
}

static void WriteStub(BYTE* at, DWORD value)
{
    at[0] = 0xB8;                           // mov eax, imm32
    *(DWORD*)(at + 1) = value;
    at[5] = 0xC3;                           // ret
}

static const char* PathName(DWORD r)
{
    return r == SKIP_PATH ? "skip (rate 1.0)"
         : r == SCALING_PATH ? "scaling path"
         : "???";
}

static void Expect(const char* what, int animId, int parity, DWORD want)
{
    DWORD r = CallGate(animId, parity);
    char d[80];
    sprintf_s(d, "got %s", PathName(r));
    Check(what, r == want, d);
}

int main()
{
    printf("AnimSpeedFix self-test\n\n");

    BYTE* page = (BYTE*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE,
                                     PAGE_EXECUTE_READWRITE);
    if (!page) {
        printf("  VirtualAlloc failed: %lu\n", GetLastError());
        return 2;
    }
    memset(page, 0xCC, 0x10000);

    g_site = (DWORD)(page + 0x100);
    memcpy((void*)g_site, kGateSig, sizeof(kGateSig));
    WriteStub((BYTE*)(g_site + 6), SCALING_PATH);
    WriteStub((BYTE*)(g_site + 6 + 0xC8), SKIP_PATH);

    printf("before patching (harness sanity)\n");
    Expect("movespeed != 0 reaches the scaling path", 5, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("movespeed == 0 skips to rate 1.0",        5, MOVESPEED_ZERO,    SKIP_PATH);

    SetEnvironmentVariableA("ANIMSPEEDFIX_SELFTEST", "1");
    HMODULE dll = LoadLibraryA("AnimSpeedFix.dll");
    if (!dll) {
        printf("  LoadLibrary(AnimSpeedFix.dll) failed: %lu\n", GetLastError());
        return 2;
    }
    InstallAtFn installAt = (InstallAtFn)GetProcAddress(dll, "AnimSpeedFix_InstallAt");
    if (!installAt) {
        printf("  AnimSpeedFix_InstallAt not exported\n");
        return 2;
    }

    {
        typedef void (*StatusFn)(int*, int*, int*, int*);
        StatusFn st = (StatusFn)GetProcAddress(dll, "AnimSpeedFix_Status");
        int gate = -1, guard = -1, drop = -1, force = -1;
        if (st) st(&gate, &guard, &drop, &force);
        char d[128];
        sprintf_s(d, "stealth-lock=%d swingguard=%d dropReplay=%d forceSlot0=%d",
                  gate, guard, drop, force);
        bool noIni = GetFileAttributesA("AnimSpeedFix.ini") == INVALID_FILE_ATTRIBUTES;
        printf("\nresolved settings (ini %s)\n",
               noIni ? "ABSENT - shipped defaults" : "present, may override");
        if (noIni) {
            // the case that actually ships: everything armed, nothing experimental
            Check("shipped defaults are correct",
                  gate == 1 && guard == 1 && drop == 1 && force == 0, d);
        } else {
            printf("  [info] %s (an ini is present, so this reflects it)\n", d);
        }
    }

    printf("\npatching\n");
    char perr[256] = { 0 };
    Check("InstallAt succeeded", installAt(g_site, perr, sizeof(perr)) != FALSE, perr);
    Check("rewritten to jmp rel32", *(BYTE*)g_site == 0xE9);
    Check("trailing byte nopped", *(BYTE*)(g_site + 5) == 0x90);

    printf("\nstealth animations are locked at 1.0x\n");
    Expect("StealthRun(223)  would-scale -> forced skip", 223, MOVESPEED_NONZERO, SKIP_PATH);
    Expect("StealthWalk(119) would-scale -> forced skip", 119, MOVESPEED_NONZERO, SKIP_PATH);
    Expect("StealthRun(223)  movespeed 0 -> still skip",  223, MOVESPEED_ZERO,    SKIP_PATH);

    printf("\neverything else keeps stock 3.3.5a behaviour\n");
    Expect("Run(5)            still scales",  5, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("Walk(4)           still scales",  4, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("Sprint(143)       still scales",143, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("Swim(42)          still scales", 42, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("StealthStand(120) still scales",120, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("Run(5) movespeed 0 -> skip",      5, MOVESPEED_ZERO,    SKIP_PATH);
    Expect("out-of-range id 5000 -> scales",5000, MOVESPEED_NONZERO, SCALING_PATH);
    Expect("id 0 (Stand) -> scales",          0, MOVESPEED_NONZERO, SCALING_PATH);

    printf("\nrefuses to patch anything else\n");
    {
        BYTE* bogus = page + 0x400;
        memset(bogus, 0x90, 16);
        char e[256] = { 0 };
        BOOL ok = installAt((DWORD)bogus, e, sizeof(e));
        Check("wrong bytes are rejected", ok == FALSE, e);
        Check("and left untouched", bogus[0] == 0x90);
    }
    {
        // must report, not fault, when the address isn't mapped at all
        char e[256] = { 0 };
        BOOL ok = installAt(0x00000010, e, sizeof(e));
        Check("unmapped address is rejected without faulting", ok == FALSE, e);
    }
    {
        // reserved-but-not-committed memory is equally off limits
        BYTE* reserved = (BYTE*)VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        if (reserved) {
            char e[256] = { 0 };
            BOOL ok = installAt((DWORD)reserved, e, sizeof(e));
            Check("uncommitted address is rejected", ok == FALSE, e);
            VirtualFree(reserved, 0, MEM_RELEASE);
        }
    }

    // ---- SwingGuard: the suppress path must emulate `ret 0x1C` exactly ----
    printf("\nSwingGuard (stack safety of the suppress path)\n");
    {
        typedef BOOL (*ArmFn)(DWORD, int, char*, int);
        typedef unsigned (*HitsFn)(void);
        ArmFn arm = (ArmFn)GetProcAddress(dll, "AnimSpeedFix_TestArmGuard");
        HitsFn hits = (HitsFn)GetProcAddress(dll, "AnimSpeedFix_TestGuardHits");
        if (!arm || !hits) {
            Check("guard test entry points exported", false);
        } else {
            // synthetic PlayAnimation: prologue + body returning 0xC0FFEE,
            // cleaning up 7 stack args like the real one (ret 0x1C).
            BYTE* fn = page + 0x800;
            static const BYTE proc[] = {
                0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20,          // push ebp; mov ebp,esp; sub esp,20h
                0xB8, 0xEE, 0xFF, 0xC0, 0x00,                // mov eax, 0C0FFEEh
                0x8B, 0xE5, 0x5D, 0xC2, 0x1C, 0x00           // mov esp,ebp; pop ebp; ret 1Ch
            };
            memcpy(fn, proc, sizeof(proc));
            g_play = (DWORD)fn;

            char d[96];
            DWORD r = CallPlay(0x11110000, 18);              // Attack2H, unhooked
            sprintf_s(d, "ret=0x%lX espDelta=%ld", r, (long)g_espDelta);
            Check("baseline: synthetic PlayAnimation returns and balances",
                  r == 0xC0FFEE && g_espDelta == 0, d);

            char err[256] = { 0 };
            // 20 ms == the shipped ProtectMs default (~one rendered frame)
            Check("arm guard + hook", arm(g_play, 20, err, sizeof(err)) != FALSE, err);

            unsigned before = hits();
            r = CallPlay(0x11110000, 18);                    // attack: must pass through
            sprintf_s(d, "ret=0x%lX espDelta=%ld", r, (long)g_espDelta);
            Check("attack animation passes through untouched",
                  r == 0xC0FFEE && g_espDelta == 0 && hits() == before, d);

            before = hits();
            r = CallPlay(0x11110000, 53);                    // SpellCastDirected: suppress
            sprintf_s(d, "ret=0x%lX espDelta=%ld", r, (long)g_espDelta);
            Check("cast animation suppressed after an attack", hits() == before + 1, d);
            Check("suppress path leaves the stack balanced", g_espDelta == 0, d);
            Check("suppress path returns 0 (did not run the function)", r == 0, d);

            before = hits();
            r = CallPlay(0x22220000, 53);                    // different model: allow
            sprintf_s(d, "ret=0x%lX espDelta=%ld", r, (long)g_espDelta);
            Check("cast on a model with no recent attack is allowed",
                  r == 0xC0FFEE && hits() == before && g_espDelta == 0, d);

            // movement is only suppressed inside the same-frame window; after it
            // expires a legitimate interrupt must get through
            before = hits();
            CallPlay(0x33330000, 18);                        // attack
            r = CallPlay(0x33330000, 13);                    // Walkbackwards, immediate
            Check("same-frame movement is suppressed too (animation-agnostic)",
                  hits() == before + 1 && r == 0);
            Sleep(60);                                       // > ProtectMs (20 ms)
            before = hits();
            r = CallPlay(0x33330000, 13);                    // now outside the window
            sprintf_s(d, "ret=0x%lX", r);
            Check("movement after the window is NOT suppressed",
                  r == 0xC0FFEE && hits() == before, d);

            before = hits();
            CallPlay(0x44440000, 18);                        // attack
            r = CallPlay(0x44440000, 6);                     // Dead - must always win
            sprintf_s(d, "ret=0x%lX", r);
            Check("death animation is never suppressed",
                  r == 0xC0FFEE && hits() == before, d);

            // The regression that shipped: a genuine follow-up attack (rogue
            // Eviscerate) inside the replay window must NOT be dropped. It is
            // only safe because no slot-3 placement was recorded here - the
            // packet gate is what protects the real in-game case.
            before = hits();
            CallPlay(0x55550000, 18);                        // melee swing
            r = CallPlay(0x55550000, 17);                    // Attack1H right after
            sprintf_s(d, "ret=0x%lX", r);
            Check("a second attack animation is not suppressed",
                  r == 0xC0FFEE && hits() == before, d);
        }
    }

    // ---- JumpFlip: Acrobatics asks for the zero-weight JumpStart variation ----
    printf("\nJumpFlip (Acrobatics jump flip)\n");
    {
        typedef int (*PosFn)(const void*);
        typedef void (*JArmFn)(void*, void*, int, int, int, int, int, int);
        typedef BOOL (*HookFn)(DWORD, char*, int);
        PosFn pos = (PosFn)GetProcAddress(dll, "JumpFlip_FlipPosition");
        JArmFn jarm = (JArmFn)GetProcAddress(dll, "JumpFlip_TestArm");
        HookFn hook = (HookFn)GetProcAddress(dll, "AnimSpeedFix_TestHookAnim");
        if (!pos || !jarm || !hook) {
            Check("jump flip entry points exported", false);
        } else {
            BYTE* mem = (BYTE*)VirtualAlloc(NULL, 0x8000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            const WORD E = 0xFFFF;
            FakeSeq stock[] = { {0, 0, 32767, E}, {37, 0, 32767, E}, {38, 0, 32767, E} };
            FakeSeq human[] = { {0, 0, 32767, E}, {37, 0, 32767, 3}, {38, 0, 32767, E}, {37, 1, 0, E} };
            FakeSeq belf[]  = { {37, 0, 31129, 1}, {37, 1, 1638, 3}, {38, 0, 32767, E}, {37, 2, 0, E} };
            FakeSeq nelf[]  = { {37, 0, 24576, 1}, {37, 1, 8191, E} };
            FakeSeq loop[]  = { {37, 0, 32767, 1}, {37, 1, 100, 0} };
            char d[96];
            int r;
            r = pos(BuildFakeModel(mem, stock, 3, true));
            sprintf_s(d, "pos=%d", r); Check("stock model: no flip variation", r == -1, d);
            r = pos(BuildFakeModel(mem, human, 4, true));
            sprintf_s(d, "pos=%d", r); Check("patched model: flip at chain position 1", r == 1, d);
            r = pos(BuildFakeModel(mem, belf, 4, true));
            sprintf_s(d, "pos=%d", r); Check("blood elf: flip after its own alt jump (2)", r == 2, d);
            r = pos(BuildFakeModel(mem, nelf, 2, true));
            sprintf_s(d, "pos=%d", r); Check("night elf (weighted stock flip): left alone", r == -1, d);
            r = pos(BuildFakeModel(mem, loop, 2, true));
            sprintf_s(d, "pos=%d", r); Check("looping chain is bounded", r == -1, d);
            r = pos(BuildFakeModel(mem, human, 4, false));
            sprintf_s(d, "pos=%d", r); Check("model not loaded yet: no decision", r == -1, d);
            Check("null model is safe", pos(NULL) == -1);
            Check("unreadable model pointer is safe", pos((void*)0x10) == -1);

            // the hook on a synthetic PlayAnimation that returns its variation arg
            BYTE* fn = page + 0xA00;
            static const BYTE retVar[] = {
                0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20,      // push ebp; mov ebp,esp; sub esp,20h
                0x8B, 0x45, 0x10,                        // mov eax, [ebp+10h]  (arg2)
                0x8B, 0xE5, 0x5D, 0xC2, 0x1C, 0x00       // mov esp,ebp; pop ebp; ret 1Ch
            };
            memcpy(fn, retVar, sizeof(retVar));
            g_play = (DWORD)fn;
            DWORD v = CallPlayVar(0x12340000, 37, -1);
            sprintf_s(d, "v=%d espDelta=%ld", (int)v, (long)g_espDelta);
            Check("baseline: synthetic PlayAnimation returns arg2", v == 0xFFFFFFFF && g_espDelta == 0, d);
            char err[256] = { 0 };
            Check("hook placed on the synthetic function", hook(g_play, err, sizeof(err)) != FALSE, err);

            BYTE* model = BuildFakeModel(mem, human, 4, true);
            BYTE* unit = mem + 0x2000;
            unsigned m = (unsigned)(DWORD_PTR)model;

            jarm(model, unit, 1, 0, 0, 100, 100, 1);
            v = CallPlayVar(m, 37, -1);
            sprintf_s(d, "v=%d espDelta=%ld", (int)v, (long)g_espDelta);
            Check("aura at 100%: JumpStart asks for the flip", v == 1 && g_espDelta == 0, d);

            jarm(model, unit, 1, 0, 0, 0, 0, 1);
            v = CallPlayVar(m, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("aura at 0%: engine rolls as usual", (int)v == -1, d);

            jarm(model, unit, 0, 0, 0, 100, 100, 1);
            v = CallPlayVar(m, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("no aura: untouched even at 100%", (int)v == -1, d);

            jarm(model, unit, 1, 1, 0, 0, 100, 1);
            v = CallPlayVar(m, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("female unit uses the female odds", v == 1, d);
            jarm(model, unit, 1, 0, 0, 0, 100, 1);
            v = CallPlayVar(m, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("male unit uses the male odds", (int)v == -1, d);

            jarm(model, unit, 1, 0, 0, 100, 100, 1);
            v = CallPlayVar(m, 38, -1);
            sprintf_s(d, "v=%d", (int)v); Check("other animations untouched", (int)v == -1, d);
            v = CallPlayVar(m, 37, 0);
            sprintf_s(d, "v=%d", (int)v); Check("a variation the caller chose is kept", v == 0, d);

            jarm(model, unit, 1, 0, 0, 100, 100, 1);
            DWORD a = CallPlayVar(m, 37, -1);
            jarm(model, unit, 1, 0, 0, 0, 0, 0);            // odds change, memory kept
            DWORD b = CallPlayVar(m, 37, -1);
            Sleep(350);                                  // > the 300 ms decision window
            DWORD c = CallPlayVar(m, 37, -1);
            sprintf_s(d, "first=%d again=%d later=%d", (int)a, (int)b, (int)c);
            Check("one jump gets one answer; the next jump rolls again",
                  a == 1 && b == 1 && (int)c == -1, d);

            // night elf: stock weighted flip at 1, Acrobatics double flip (weight 0) at 2
            FakeSeq nedbl[] = { {37, 0, 24576, 1}, {37, 1, 8191, 2}, {37, 2, 0, E} };
            BYTE* ne = BuildFakeModel(mem + 0x5000, nedbl, 3, true);
            unsigned nm = (unsigned)(DWORD_PTR)ne;
            r = pos(ne);
            sprintf_s(d, "pos=%d", r); Check("night elf: double flip at chain position 2", r == 2, d);
            jarm(ne, unit, 1, 0, 1, 100, 100, 1);
            v = CallPlayVar(nm, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("night elf + aura, hit: asks for the double flip", v == 2, d);
            jarm(ne, unit, 1, 0, 1, 0, 0, 1);
            v = CallPlayVar(nm, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("night elf + aura, miss: plain jump, no single flip", v == 0, d);
            jarm(ne, unit, 0, 0, 1, 100, 100, 1);
            v = CallPlayVar(nm, 37, -1);
            sprintf_s(d, "v=%d", (int)v); Check("night elf without aura: stock roll untouched", (int)v == -1, d);
            jarm(ne, unit, 1, 0, 1, 0, 0, 1);
            DWORD n1 = CallPlayVar(nm, 37, -1);
            jarm(ne, unit, 1, 0, 1, 100, 100, 0);           // odds change, memory kept
            DWORD n2 = CallPlayVar(nm, 37, -1);
            sprintf_s(d, "first=%d again=%d", (int)n1, (int)n2);
            Check("a remembered plain-jump decision is reused too", n1 == 0 && n2 == 0, d);

            BYTE* plain = BuildFakeModel(mem + 0x4000, stock, 3, true);
            jarm(plain, unit, 1, 0, 0, 100, 100, 1);
            v = CallPlayVar((unsigned)(DWORD_PTR)plain, 37, -1);
            sprintf_s(d, "v=%d espDelta=%ld", (int)v, (long)g_espDelta);
            Check("aura on an unpatched model: untouched", (int)v == -1 && g_espDelta == 0, d);

            // druid forms: the form model (cat, bear, ...) carries the flip at position 1
            typedef void (*JMountFn)(int);
            JMountFn jmount = (JMountFn)GetProcAddress(dll, "JumpFlip_TestMounted");
            if (!jmount) {
                Check("JumpFlip_TestMounted exported", false);
            } else {
                FakeSeq cat[] = { {0, 0, 32767, E}, {37, 0, 32767, 4}, {38, 0, 32767, E},
                                  {39, 0, 32767, E}, {37, 1, 0, E} };
                BYTE* form = BuildFakeModel(mem + 0x6000, cat, 5, true);
                unsigned fm = (unsigned)(DWORD_PTR)form;
                r = pos(form);
                sprintf_s(d, "pos=%d", r); Check("druid form model: flip at chain position 1", r == 1, d);
                jarm(form, unit, 1, 0, 1, 100, 100, 1);
                v = CallPlayVar(fm, 37, -1);
                sprintf_s(d, "v=%d", (int)v); Check("night elf druid in form, hit: single flip", v == 1, d);
                jarm(form, unit, 1, 0, 1, 0, 0, 1);
                v = CallPlayVar(fm, 37, -1);
                sprintf_s(d, "v=%d", (int)v); Check("night elf druid in form, miss: plain jump", v == 0, d);

                // a mounted unit never flips (Tiger.m2 is also a Beast Rider mount)
                jarm(form, unit, 1, 0, 0, 100, 100, 1);
                jmount(1);
                v = CallPlayVar(fm, 37, -1);
                sprintf_s(d, "v=%d", (int)v); Check("mounted + aura at 100%: untouched", (int)v == -1, d);
                jarm(ne, unit, 1, 0, 1, 0, 0, 1);
                jmount(1);
                v = CallPlayVar(nm, 37, -1);
                sprintf_s(d, "v=%d", (int)v); Check("mounted night elf + aura: stock roll untouched", (int)v == -1, d);
                jarm(form, unit, 1, 0, 0, 100, 100, 1);    // arming again dismounts
                v = CallPlayVar(fm, 37, -1);
                sprintf_s(d, "v=%d", (int)v); Check("dismounted again: flips", v == 1, d);

                // ---- Moonkin leap: Fly while in the air from Feral Charge - Moonkin ----
                typedef void (*JMoveFn)(int, int, float);
                typedef int (*HasFn)(const void*, DWORD);
                JMoveFn jmove = (JMoveFn)GetProcAddress(dll, "JumpFlip_TestMove");
                HasFn has = (HasFn)GetProcAddress(dll, "JumpFlip_HasAnim");
                if (!jmove || !has) {
                    Check("moonkin flap entry points exported", false);
                } else {
                    // fn+8 is the displacement of `mov eax, [ebp+X]`: 0x10 returns the
                    // variation (arg2), 0x0C the animation id (arg1)
                    auto play = [&](unsigned mm, int anim, int var, bool readAnim) -> DWORD {
                        fn[8] = readAnim ? 0x0C : 0x10;
                        FlushInstructionCache(GetCurrentProcess(), fn, 16);
                        DWORD res = CallPlayVar(mm, anim, var);
                        fn[8] = 0x10;
                        FlushInstructionCache(GetCurrentProcess(), fn, 16);
                        return res;
                    };
                    FakeSeq owl[] = { {0, 0, 32767, E}, {37, 0, 32767, 5}, {38, 0, 32767, E},
                                      {40, 0, 32767, E}, {135, 0, 32767, E}, {37, 1, 0, E} };
                    BYTE* mk = BuildFakeModel(mem + 0x7000, owl, 6, true);
                    unsigned mkm = (unsigned)(DWORD_PTR)mk;
                    Check("moonkin model has Fly", has(mk, 135) == 1);
                    Check("cat form model has no Fly", has(form, 135) == 0);
                    Check("HasAnim on a null model is safe", has(NULL, 135) == 0);

                    jarm(mk, unit, 1, 0, 0, 100, 100, 1);  // Acrobatics at 100% too
                    jmove(31, 1, -24.1f);                  // Feral Charge - Moonkin launch
                    v = play(mkm, 37, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("leap: JumpStart becomes Fly", v == 135, d);
                    v = play(mkm, 37, -1, false);
                    sprintf_s(d, "v=%d", (int)v); Check("leap: Fly is rolled, no flip variation", (int)v == -1, d);
                    jmove(31, 1, -3.0f);                   // fall re-based mid-air (0x987410)
                    v = play(mkm, 38, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("leap, re-based mid-air: Jump stays Fly", v == 135, d);
                    v = play(mkm, 40, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("leap: Fall becomes Fly", v == 135, d);
                    v = play(mkm, 39, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("leap: landing (JumpEnd) untouched", v == 39, d);
                    jmove(31, 0, 0.0f);                    // on the ground
                    v = play(mkm, 38, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("landed: Jump untouched", v == 38, d);
                    jmove(31, 1, -3.0f);
                    v = play(mkm, 40, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("after landing the leap is forgotten", v == 40, d);

                    jarm(mk, unit, 1, 0, 0, 100, 100, 1);
                    jmove(31, 1, -7.95f);                  // an ordinary jump
                    v = play(mkm, 37, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("moonkin jump: JumpStart kept", v == 37, d);
                    jarm(mk, unit, 1, 0, 0, 100, 100, 1);
                    jmove(31, 1, -7.95f);
                    v = play(mkm, 37, -1, false);
                    sprintf_s(d, "v=%d", (int)v); Check("moonkin jump + Acrobatics: still flips", v == 1, d);

                    jarm(mk, unit, 0, 0, 0, 0, 0, 1);
                    jmove(1, 1, -24.1f);                   // cat form, launched as fast
                    v = play(mkm, 37, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("not in Moonkin Form: untouched", v == 37, d);
                    jarm(mk, unit, 0, 0, 0, 0, 0, 1);
                    jmove(31, 1, 24.1f);                   // falling fast DOWN
                    v = play(mkm, 40, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("fast downward fall: untouched", v == 40, d);
                    jarm(form, unit, 0, 0, 0, 0, 0, 1);
                    jmove(31, 1, -24.1f);                  // a model without Fly
                    v = play(fm, 37, -1, true);
                    sprintf_s(d, "anim=%d", (int)v); Check("model without Fly: untouched", v == 37, d);
                }
            }
            VirtualFree(mem, 0, MEM_RELEASE);
        }
    }

    // ---- PlayerCollide: the push-out math (no game needed) ----
    printf("\nPlayerCollide (cylinder push-out math)\n");
    {
        typedef void (*CfgFn)(int, float, int);
        typedef void (*ResolveFn)(float, float, const float*, int, float, float, float*, float*);
        CfgFn cfg = (CfgFn)GetProcAddress(dll, "PlayerCollide_TestConfig");
        ResolveFn resolve = (ResolveFn)GetProcAddress(dll, "PlayerCollide_TestResolve");
        if (!cfg || !resolve) {
            Check("PlayerCollide test entry points exported", false);
        } else {
            cfg(0, 0.75f, 1);                       // radius 0.75 -> contact dist 1.5
            float ox = 0, oy = 0;
            char d[96];
            const float BIG = 100.0f;               // effectively uncapped step

            // no overlap: a body 2.0 away (> 1.5) must not move us
            float far_[2] = { 2.0f, 0.0f };
            resolve(0, 0, far_, 1, 0.0f, BIG, &ox, &oy);
            sprintf_s(d, "out=(%.3f,%.3f)", ox, oy);
            Check("body beyond reach does not move us", ox == 0.0f && oy == 0.0f, d);

            // single overlap at distance 1.0, uncapped: ejected to contact (1.5)
            float near_[2] = { 1.0f, 0.0f };
            resolve(0, 0, near_, 1, 0.0f, BIG, &ox, &oy);
            sprintf_s(d, "out=(%.3f,%.3f)", ox, oy);
            Check("overlap ejects to the contact distance",
                  fabs(ox - (-0.5f)) < 1e-4f && fabs(oy) < 1e-4f, d);

            // same overlap but the frame's step is capped to 0.1: partial move only,
            // in the same direction. This is what stops the zip.
            resolve(0, 0, near_, 1, 0.0f, 0.1f, &ox, &oy);
            sprintf_s(d, "out=(%.3f,%.3f)", ox, oy);
            Check("deep overlap is capped to the per-frame step (no zip)",
                  fabs(ox - (-0.1f)) < 1e-4f && fabs(oy) < 1e-4f, d);

            // exactly coincident: no divide-by-zero; ejects along -facing. facing 0
            // (east) => pushed toward -x, capped to 0.2.
            float same[2] = { 0.0f, 0.0f };
            resolve(0, 0, same, 1, 0.0f, 0.2f, &ox, &oy);
            sprintf_s(d, "out=(%.3f,%.3f)", ox, oy);
            Check("coincident bodies eject along -facing (no NaN)",
                  fabs(ox - (-0.2f)) < 1e-4f && fabs(oy) < 1e-4f, d);
        }
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASS",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
