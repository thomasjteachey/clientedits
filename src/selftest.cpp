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
