#!/usr/bin/env python3
"""Run ctf_test.lua in Lua 5.3 (liblua5.3.so.0 via ctypes) against WorldStateFrame.lua variants.

usage: ctf_test.py <label>=<WorldStateFrame.lua>[+<BattlefieldFrame.lua>] [...]
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
X7 = '/tmp/centmode/bffmerge/x/patch-enUS-7.zip/Interface'

lua = ctypes.CDLL('liblua5.3.so.0')
lua.luaL_newstate.restype = ctypes.c_void_p
lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
lua.luaL_loadbufferx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p]
lua.luaL_loadbufferx.restype = ctypes.c_int
lua.lua_pcallk.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ssize_t, ctypes.c_void_p]
lua.lua_pcallk.restype = ctypes.c_int
lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
lua.lua_tolstring.restype = ctypes.c_char_p
lua.lua_pushstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lua.lua_setglobal.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lua.lua_getglobal.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lua.lua_close.argtypes = [ctypes.c_void_p]


def function_chunk(path, header):
    text = open(path, encoding='latin-1').read()
    i = text.index(header)
    j = text.index('\nend\n', i) + len('\nend\n')
    return text[i:j]


wmf = function_chunk(os.path.join(X7, 'FrameXML', 'WorldMapFrame.lua'), 'function WorldMapFrame_UpdateBattlefieldFlags()')
bfm = function_chunk(os.path.join(X7, 'AddOns', 'Blizzard_BattlefieldMinimap', 'Blizzard_BattlefieldMinimap.lua'),
                     'function BattlefieldMinimap_UpdateFlags()')
script = open(os.path.join(HERE, 'ctf_test.lua'), 'rb').read()

failed = False
for arg in sys.argv[1:]:
    label, path = arg.split('=', 1)
    path, _, bff_path = path.partition('+')
    L = lua.luaL_newstate()
    lua.luaL_openlibs(L)
    for name, value in (('WSF_PATH', path), ('BFF_PATH', bff_path), ('WMF_CHUNK', wmf), ('BFM_CHUNK', bfm)):
        lua.lua_pushstring(L, value.encode('latin-1'))
        lua.lua_setglobal(L, name.encode())
    rc = lua.luaL_loadbufferx(L, script, len(script), b'@ctf_test.lua', None)
    if rc == 0:
        rc = lua.lua_pcallk(L, 0, 0, 0, 0, None)
    print('==================== %s (%s)' % (label, path))
    if rc != 0:
        print('LUA ERROR: %s' % lua.lua_tolstring(L, -1, None).decode('latin-1'))
        failed = True
    else:
        lua.lua_getglobal(L, b'OUT')
        print(lua.lua_tolstring(L, -1, None).decode('latin-1'))
    lua.lua_close(L)
sys.exit(1 if failed else 0)
