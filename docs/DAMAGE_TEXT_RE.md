# Floating damage text and COMBAT_TEXT_UPDATE (Wow.exe 3.3.5a 12340)

Static RE, 2026-10-29, with `clientedits\reversing\peexplore.py`. VAs, imagebase 0x400000.
Handlers are registered through `RegisterHandler 0x6B0B80(opcode, handler, 0)`, and they are
called as `handler(param, opcode, time, CDataStore* msg)`.

There are two separate systems:

* **World text**: the numbers over *another* unit when *you* (or your pet) hit or heal it. One
  shared gate decides "is the source me or my pet": **0x007190A0**.
* **COMBAT_TEXT_UPDATE**: Blizzard_CombatText's incoming numbers. These fire when the
  target (or subject) equals a separate global GUID, the *combat-text unit*, at
  **0x00CA1380 (lo) / 0x00CA1384 (hi)**. Lua `CombatTextSetActiveUnit(token)` sets it.

## Packet handlers

| opcode | handler | worker | world text | COMBAT_TEXT_UPDATE |
|---|---|---|---|---|
| 0x14A ATTACKERSTATEUPDATE | 0x756800 (switch, case 0x7568A1; also 0x143-0x149, 0x1FC, 0x261, 0x3BF) | parse 0x755630; impact 0x755A60 | 0x722340 dmg @0x755E26, 0x71F990 miss/dodge/parry... | 0x7512B0 |
| 0x1FC ENVIRONMENTAL_DAMAGE_LOG | 0x756800 case 0x756A5D | 0x755270 -> 0x751150 | none | yes (0x751229 compares 0xCA1380) |
| 0x250 SPELLNONMELEEDAMAGELOG | 0x6D3C10 | 0x6D3840 | 0x722340 @0x6D3AD9; 0x71F990 @0x6D3B2D (absorb 0xA / block 5 / resist 2) | 0x751C40 (or 0x752DE0 when hitInfo&8) |
| 0x24E PERIODICAURALOG | 0x753710 | 0x7531F0 | 0x722340 @0x753363 (periodic=1); 0x71F990 @0x7533B8/0x7534D5 | 0x751C40 / 0x751F70 / 0x752040 |
| 0x150 SPELLHEALLOG | 0x6D3DD0 | 0x6D3C30 -> 0x751F70 -> 0x750860 | 0x722440 @0x750A78, **only if target != CT unit** | 0x750860 (target == CT unit) |
| 0x151 SPELLENERGIZELOG | 0x6D3EF0 | 0x6D3DF0 -> 0x752040 -> 0x750A90 | none | yes (0x750B11) |
| 0x24F SPELLDAMAGESHIELD | 0x6D3750 | inline | 0x722340 @0x6D3819 (arg6=1) | 0x752C90 |

When the attacker exists client-side, melee world text is deferred. `0x755050` copies the
hit into the attacker's pending slot at `+0xB98`. It is drawn by `0x755E40 -> 0x755A60` at
the swing impact (0x756463), or flushed by the attacker's next swing (`0x756180`). With no
attacker object it is drawn immediately (0x756A01).

## World-text functions (thiscall, ecx = target CGUnit_C)

* `0x722340(WGUID* src, int amount, SpellRec* spell, bool crit, bool periodic, bool spellColour)`, `ret 18h`: damage.
* `0x722440(WGUID* healer, int amount, int ?, bool crit, int 0)`, `ret 14h`: heal. Shown only when the out flag is 1 (the player, not a pet) and `CombatHealing` is on.
* `0x71F990(WGUID* src, int type, SpellRec* spell, bool periodic)`, `ret 10h`: MISS/DODGE/... (type indexes the string table 0xADBFDC).

Each of them:
1. skips when the target guid == **active mover** `[0x00CA1238]` (written only by SetActiveMover 0x729010), so you get no world text over yourself or your vehicle;
2. calls **0x7190A0**;
3. checks the CVars, then spawns the text with `0x7E6030(anchor=[unit+0xB0], int flags, char* text, CImVector* colour, 0)` (cdecl).

CVar pointers (value at `+0x30`): `0xBD0980` CombatDamage, `0xBD0984` CombatLogPeriodicSpells,
`0xBD0988` PetMeleeDamage, `0xBD098C` PetSpellDamage, `0xBD0990` CombatHealing.

`0x7E6030` drops the text if the anchor is null, or if the unit is the camera target
(`[[0xB7436C]+0x7E20]+0x88`) while the camera is in first person (`0x5FE3A0`, distance
< `[0xA1E308]`), or if `[objmgr+0xC8]` (an unidentified flag) is set. `0x7E5C30` holds at
most 4 live texts per unit.

## 0x7190A0: the shared "source is me or my pet" gate (3 callers only)

Custom convention: **eax = WGUID\* source**, `[esp+4] = bool* outIsPlayer`, and the caller
does `add esp,4`. It returns al. The callers are 0x722395 (returns to 0x72239A), 0x722470
(returns to 0x722475) and 0x71F9D1 (returns to 0x71F9D6). There are no other refs, jmps
or data pointers to it.

```
src == ActivePlayerGuid() (0x4D3790)          -> *out=1, return 1
u = ObjectPtr(src, TYPEMASK_UNIT); !u          -> 0
own = u.CHARMEDBY ? u.CHARMEDBY : u.CREATEDBY  (unit fields [u+0xD0]+0x18 / +0x28)
own == player                                  -> *out=0, return 1   (pet / totem / guardian)
o = ObjectPtr(own); o && 0x4F5F20(&o->fields) == player -> *out=0, return 1  (owner's owner)
else 0
```

## COMBAT_TEXT_UPDATE

The event id is 0x1E4 (the name table at 0xC24EB0; slot 0xC25640 -> "COMBAT_TEXT_UPDATE").
It is fired through 0x81B530 from about 20 sites in 0x74D4xx-0x752Exx (UnitCombatLog_C.cpp).
Each one compares the target or subject GUID with `[0xCA1380]/[0xCA1384]`, inline or through
`0x74D430(lo,hi)`. The global is written at:

* `0x74D580` = `CombatTextSetActiveUnit` (`0x60C1C0(token)` -> `mov [0xCA1380],eax; mov [0xCA1384],edx` at 0x74D599, bytes `A3 80 13 CA 00 89 15 84 13 CA 00`);
* `0x753963`, which resets it to 0 in the combat-log init (0x7538A0, reached from the game-init chain 0x405561 -> 0x742220 -> 0x756BD0).

Blizzard_CombatText calls `CombatTextSetActiveUnit("player"/"vehicle")` in
CombatText_UpdateDisplayedMessages and on UNIT_ENTERED_VEHICLE / UNIT_EXITING_VEHICLE (only for
the unit "player").

## Recommended hooks

**Incoming:** no code patch is needed. While spectating, write the spectated guid into
0xCA1380/84. Re-assert it every frame from CameraTick, because Blizzard_CombatText rewrites it
on option changes and vehicle events. On stop, restore the saved value, or run
`CombatTextSetActiveUnit(CombatText.unit)`.

**Outgoing:** one 5-byte jmp at **0x7190A0**.
Signature `55 8B EC 56 8B F0 E8 E5 A6 DB FF 8B 0E 3B C8`; write `E9 rel32 90`; resume **0x7190A6**.

```cpp
static DWORD g_dmgSrcResume = 0x007190A6;
// -1 = not ours, run the original; 0/1 = answer
static int __cdecl SpecDamageSource(const DWORD* src, BYTE* outIsPlayer, const BYTE* target)
{
    if (!g_on) return -1;
    const DWORD lo = g_followLo, hi = g_followHi;
    int self = src[0] == lo && src[1] == hi, pet = 0;
    if (!self) {
        const BYTE* u = (const BYTE*)((ObjectPtr_t)kObjectPtr)(src[0], src[1], kTypeMaskUnit, "damagetext.cpp", __LINE__);
        if (u) {
            const DWORD* f = *(const DWORD* const*)(u + 0xD0);
            const DWORD* own = (f[6] | f[7]) ? f + 6 : f + 10;   // CHARMEDBY else CREATEDBY
            pet = own[0] == lo && own[1] == hi;
        }
    }
    if (!self && !pet) return -1;
    if (target) {                                   // like the active-mover skip: nothing over the spectated unit itself
        const DWORD* g = *(const DWORD* const*)(target + 8);
        if (g[0] == lo && g[1] == hi) return 0;
    }
    *outIsPlayer = (BYTE)self;
    return 1;
}

__declspec(naked) static void DamageSourceHook()
{
    __asm {
        push eax                    // WGUID* source
        mov  ecx, [esp+4]           // return address -> which caller, where its target lives
        xor  edx, edx
        cmp  ecx, 0x0072239A        // 0x722340 damage: target in edi
        jne  n1
        mov  edx, edi
        jmp  have
    n1: cmp  ecx, 0x00722475        // 0x722440 heal: target in esi
        jne  n2
        mov  edx, esi
        jmp  have
    n2: cmp  ecx, 0x0071F9D6        // 0x71F990 miss: target in [ebp-4]
        jne  have
        mov  edx, [ebp-4]
    have:
        push edx
        push dword ptr [esp+0Ch]    // bool* out
        push eax
        call SpecDamageSource
        add  esp, 0Ch
        cmp  eax, -1
        je   original
        add  esp, 4                 // drop the saved WGUID*
        ret                         // al = answer; caller does add esp,4
    original:
        pop  eax
        push ebp                    // the 6 bytes the jmp replaced
        mov  ebp, esp
        push esi
        mov  esi, eax
        jmp  dword ptr [g_dmgSrcResume]
    }
}
```

This affects only these three world-text decisions. Movement, targeting and the active
mover are untouched, and the real player's own numbers still go through the original.

## Pitfalls

* Gates for world text: CombatDamage (damage and miss), CombatLogPeriodicSpells (periodic), PetMeleeDamage / PetSpellDamage (when out=0), CombatHealing (heals; pet heals are never shown). Gates for incoming: the SHOW_COMBAT_TEXT / enableCombatText CVar (Blizzard_CombatText must be loaded and visible) plus its per-type COMBAT_TEXT_SHOW_* options.
* Heals: if the target == CT unit, only COMBAT_TEXT_UPDATE fires (no world text), which matches normal play.
* Self-damage without the target check would draw over the spectated unit, not over the local player, because the active-mover skip compares the viewer's guid.
* Energize and environmental damage are incoming (COMBAT_TEXT_UPDATE) only.
* Cosmetic: Blizzard_CombatText keeps `self.unit = "player"`. With COMBAT_TEXT_SHOW_FRIENDLY_NAMES on, the spectated unit's self-heals show its name, and the low-health/low-mana warnings stay the viewer's.
* fctSpellMechanics world text (0x74DA70) also keys on the CT unit, so it uses the "self" format for the spectated unit.
* Melee from replay units shows at their swing impact, or at the next swing if they never animate.
* Not verified: off-screen culling inside 0x7E5C30; what `[objmgr+0xC8]` means; whether the 0x7538A0 reset also runs on in-session loading screens (re-asserting every frame covers this); the "owner's owner" branch is not replicated in the stub.
