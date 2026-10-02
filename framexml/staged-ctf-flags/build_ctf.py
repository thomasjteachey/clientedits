#!/usr/bin/env python3
"""Build the CTF flag / PvP portrait client files from the live archive copies.

Inputs (extracted from the live archives, md5-checked):
  A_WSF     patch-enUS-A WorldStateFrame.lua          75f0a8a0 (== patch-enUS-8's)
  A_PVPBF   patch-enUS-A PVPBattlegroundFrame.lua     3846a46b (== patch-enUS-8's)
  Y16_PVPBF patch-Y 1.00016 PVPBattlegroundFrame.lua  f5953118 (A's base)
  FrameXML.toc of patch-enUS-A (9f36f55b) and patch-enUS-8 (5bc510af)
  wsf_fixed_tokens.lua, wsf_note_single.lua          the replacement Lua

Outputs under ctf_stage/{A,8}/Interface/FrameXML/:
  WorldStateFrame.lua      carrier markers in client slot order, stale carriers dropped
  BPlusPVPPortraitFix.lua  the portrait block from A/8's shadowed PVPBattlegroundFrame.lua
  FrameXML.toc             + BPlusPVPPortraitFix.lua after BPlusHiddenAuras.lua
"""
import difflib
import hashlib
import os
import sys

SP = os.path.dirname(os.path.abspath(__file__))
FX = os.path.join('Interface', 'FrameXML')
IN = {
    'A_WSF': (os.path.join(SP, 'inputs', 'A_WorldStateFrame.lua'), '75f0a8a03b3827a4ca8765bd9e06619a'),
    'A_PVPBF': (os.path.join(SP, 'inputs', 'A_PVPBattlegroundFrame.lua'), '3846a46bdfe2214e9bf78bea9080da6d'),
    'Y16_PVPBF': (os.path.join(SP, 'inputs', 'Y1.00016_PVPBattlegroundFrame.lua'), 'f5953118877fa149dae21a775e3f7443'),
}
OUT = os.path.join(SP, 'shipped-rebuild')


def load(key):
    path, want = IN[key]
    data = open(path, 'rb').read()
    got = hashlib.md5(data).hexdigest()
    if got != want:
        sys.exit('%s: md5 %s, expected %s (%s)' % (key, got, want, path))
    if b'\r' in data:
        sys.exit('%s: unexpected CR bytes' % key)
    return data.decode('latin-1')


def fragment(name):
    data = open(os.path.join(SP, name), 'rb').read()
    if b'\r' in data or b'\t' not in data:
        sys.exit('%s: needs LF line endings and tab indentation' % name)
    return data.decode('latin-1')


def write(rel, text):
    path = os.path.join(OUT, *rel.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.encode('latin-1')
    with open(path, 'wb') as f:
        f.write(data)
    print('wrote %-50s %6d bytes md5 %s' % (rel, len(data), hashlib.md5(data).hexdigest()))
    return data


def replace_count(text, old, new, count, what):
    n = text.count(old)
    if n != count:
        sys.exit('%s: anchor found %d times, expected %d' % (what, n, count))
    return text.replace(old, new)


def only_inserts(base, result, what):
    """Opcodes base -> result must be equal/insert only; returns inserted line blocks."""
    a, b = base.splitlines(True), result.splitlines(True)
    blocks = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == 'equal':
            continue
        if tag != 'insert':
            sys.exit('%s: unexpected %s at base lines %d-%d' % (what, tag, i1 + 1, i2))
        blocks.append((i1, ''.join(b[j1:j2])))
    return blocks


# ------------------------------------------------------------------ WorldStateFrame.lua
wsf = load('A_WSF')

# 1. CenturionWSG_GetFixedTokens: whole function replaced (reversed slot order,
#    stale carrier markers dropped). Pinned by the md5 of the removed text.
start = 'function CenturionWSG_GetFixedTokens(battlefieldFlags)\n'
after = '\n-- Kept only for compatibility with older patched files.'
if wsf.count(start) != 1 or wsf.count(after) != 1:
    sys.exit('WSF: GetFixedTokens anchors not unique')
i, j = wsf.index(start), wsf.index(after)
old_func = wsf[i:j]
if hashlib.md5(old_func.encode('latin-1')).hexdigest() != '8a68d40dc316f75b483a8bd56bf24dc8' or old_func.count('\n') != 84:
    sys.exit('WSF: GetFixedTokens region is not the 84-line function the fix was written against')
if not old_func.endswith('\treturn fixed;\nend\n') or '"AllianceFlag"' not in old_func:
    sys.exit('WSF: GetFixedTokens region does not look like the function')
wsf_new = wsf[:i] + fragment('wsf_fixed_tokens.lua') + wsf[j:]

# 2. Header note: the order the client really holds the carriers in.
wsf_new = replace_count(wsf_new, '''-- Server stock position order is deterministic:
--   marker 1 = physical Alliance flag carrier, if present
--   marker 2 = physical Horde flag carrier, if present
-- When both flags are carried, this lets us know without proximity guessing.
''', '''-- Stock position order is deterministic, but the client reverses the packet:
--   the server writes the physical Alliance flag carrier first and the Horde
--   flag carrier second (HandleBattlegroundPlayerPositionsOpcode, picker slots
--   indexed by flag identity), and the client's MSG_BATTLEGROUND_PLAYER_POSITIONS
--   handler (Wow.exe 0x54B3F0) stores N carriers from slot N-1 down to slot 0.
--   GetBattlefieldFlagPosition(i) reads slot i-1, so with both flags carried:
--     marker 1 = physical Horde flag carrier
--     marker 2 = physical Alliance flag carrier
--   With one flag carried it is always marker 1, and CWSG says which flag.
-- When both flags are carried, this lets us know without proximity guessing.
''', 1, 'WSF header note')

# 3. Remember which flag was carried alone, updated as CWSG arrives.
wsf_new = replace_count(wsf_new, 'local function CenturionCTF_ApplyFullPayload(payload)\n',
                        fragment('wsf_note_single.lua') + 'local function CenturionCTF_ApplyFullPayload(payload)\n', 1,
                        'WSF note-single insert')
wsf_new = replace_count(wsf_new, '\tCenturionCTF_RefreshFlagFrames();\n\treturn true;\nend\n',
                        '\tCenturionCTF_NoteSingleCarrier();\n\tCenturionCTF_RefreshFlagFrames();\n\treturn true;\nend\n', 2,
                        'WSF payload handlers')
wsf_new = replace_count(wsf_new, '\t\tCENTURION_WSG_AUTH.HasFull = false;\n',
                        '\t\tCENTURION_WSG_AUTH.HasFull = false;\n\t\tCENTURION_WSG_AUTH.LastSingleSide = nil;\n', 1,
                        'WSF reset')

# The two NoteSingleCarrier calls must sit in the two payload handlers.
for handler in ('local function CenturionCTF_ApplyFullPayload(payload)\n', 'local function CenturionCTF_ApplySidePayload(payload)\n'):
    body = wsf_new[wsf_new.index(handler):]
    body = body[:body.index('\nend\n') + 5]
    if 'CenturionCTF_NoteSingleCarrier();' not in body:
        sys.exit('WSF: NoteSingleCarrier call missing from %s' % handler.strip())
# NoteSingleCarrier must be defined before its first use, GetStockSides after its helpers.
if wsf_new.index('local function CenturionCTF_NoteSingleCarrier()') > wsf_new.index('CenturionCTF_NoteSingleCarrier();'):
    sys.exit('WSF: NoteSingleCarrier used before it is defined')
for helper in ('local function CenturionCTF_IsAuthReady()', 'local function CenturionCTF_GetSingleHeldTokenFromWorldStates()',
               'local function CenturionCTF_TokenFromPath('):
    if wsf_new.index(helper) > wsf_new.index('local function CenturionCTF_GetStockSides('):
        sys.exit('WSF: %s defined after GetStockSides' % helper)
for arch in ('A', '8'):
    write(arch + '/Interface/FrameXML/WorldStateFrame.lua', wsf_new)
with open(os.path.join(OUT, 'WorldStateFrame.lua.diff'), 'w', newline='\n') as f:
    f.writelines(difflib.unified_diff(wsf.splitlines(True), wsf_new.splitlines(True),
                                      'patch-enUS-A/8 WorldStateFrame.lua (75f0a8a0)', 'fixed', n=3))

# ------------------------------------------------------------------ BPlusPVPPortraitFix.lua (A and 8)
# The portrait block at the top of A/8's PVPBattlegroundFrame.lua never loaded: patch-Y's
# copy of that file shadows it. As its own toc entry it loads from the winning toc.
y16, a_pvp = load('Y16_PVPBF'), load('A_PVPBF')
y16_lines, a_lines = y16.splitlines(True), a_pvp.splitlines(True)
added = len(a_lines) - len(y16_lines)
# A = Y16[0:2] + block + Y16[2:]  (proved by slices, not by where difflib anchors it)
if added != 62 or a_lines[:2] != y16_lines[:2] or a_lines[2 + added:] != y16_lines[2:]:
    sys.exit('A PVPBF: not Y 1.00016 with one 62-line block after line 2')
block = a_lines[2:2 + added]
old_header = [
    '-- BarracksPlus: keep the main PvP window portrait from showing as a hollow frame.\n',
    '-- PVPFrame.xml is loaded immediately before this file, but the custom FrameXML\n',
    '-- pack only overrides the battleground/arena/battlefield frames.  If the main\n',
    '-- PvP parent portrait is missing or blank, create/force it here using artwork\n',
    '-- that is known to exist in the 3.3.5 client.\n',
]
if block[:5] != old_header or block[-2:] != ['\n', '\n']:
    sys.exit('A PVPBF: portrait block header/trailer not as expected')
code = block[5:-2]
if not code[0].startswith('local BPLUS_PVP_FRAME_PORTRAIT_TEXTURE') or not code[-1].startswith('BPlusPVPFramePortraitFixFrame:SetScript('):
    sys.exit('A PVPBF: portrait block code boundaries not as expected')
portrait_file = ''.join([
    '-- BarracksPlus: keep the main PvP window portrait from showing as a hollow frame.\n',
    '-- Stock PVPFrame_OnShow fills PVPFramePortrait with SetPortraitTexture(..., "player"),\n',
    '-- which comes out empty here, so artwork that is known to exist in the 3.3.5 client\n',
    '-- is forced whenever the PvP window or one of its tabs is shown, and the parent\n',
    '-- portrait is created if it is missing.\n',
    '--\n',
    "-- This used to sit at the top of patch-enUS-A/-8's PVPBattlegroundFrame.lua, where\n",
    "-- patch-Y's copy of that file shadowed it. As its own FrameXML.toc entry it loads from\n",
    '-- the winning toc: patch-enUS-A for Barracks+/Centurion, patch-enUS-8 for Legionnaire+.\n',
] + code)
for arch in ('A', '8'):
    write(arch + '/Interface/FrameXML/BPlusPVPPortraitFix.lua', portrait_file)

# ------------------------------------------------------------------ FrameXML.toc (A and 8)
TOCS = {
    'A': (os.path.join(SP, 'inputs', 'A_FrameXML.toc'), '9f36f55b33da1e3dfc05b9c9214c65d0'),
    '8': (os.path.join(SP, 'inputs', '8_FrameXML.toc'), '5bc510af9d7067578bfb866c6ad290b5'),
}
for arch, (path, want) in TOCS.items():
    data = open(path, 'rb').read()
    if hashlib.md5(data).hexdigest() != want or b'\r' in data:
        sys.exit('%s toc: md5 %s, expected %s' % (arch, hashlib.md5(data).hexdigest(), want))
    toc = data.decode('latin-1')
    if 'BPlusPVPPortraitFix.lua' in toc:
        sys.exit('%s toc already lists BPlusPVPPortraitFix.lua' % arch)
    new_toc = replace_count(toc, '\nBPlusHiddenAuras.lua\n', '\nBPlusHiddenAuras.lua\nBPlusPVPPortraitFix.lua\n', 1, arch + ' toc')
    if new_toc.index('BPlusPVPPortraitFix.lua') > new_toc.index('## add new modules above here'):
        sys.exit('%s toc: new line landed below the modules marker' % arch)
    if only_inserts(toc, new_toc, arch + ' toc') != [(new_toc.splitlines().index('BPlusPVPPortraitFix.lua'), 'BPlusPVPPortraitFix.lua\n')]:
        sys.exit('%s toc: not exactly one inserted line' % arch)
    write(arch + '/Interface/FrameXML/FrameXML.toc', new_toc)
