#!/usr/bin/env python3
"""Run a Lua script in Lua 5.3 (liblua5.3.so.0 via ctypes); print its OUT global.

usage: lua_run.py <script.lua> [NAME=value ...]    (each NAME becomes a string global)
"""
import ctypes
import sys

lua = ctypes.CDLL('liblua5.3.so.0')
lua.luaL_newstate.restype = ctypes.c_void_p
lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
lua.luaL_loadbufferx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p]
lua.lua_pcallk.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ssize_t, ctypes.c_void_p]
lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
lua.lua_tolstring.restype = ctypes.c_char_p
lua.lua_pushstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lua.lua_setglobal.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lua.lua_getglobal.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

script = open(sys.argv[1], 'rb').read()
L = lua.luaL_newstate()
lua.luaL_openlibs(L)
for pair in sys.argv[2:]:
    name, value = pair.split('=', 1)
    lua.lua_pushstring(L, value.encode('latin-1'))
    lua.lua_setglobal(L, name.encode())
rc = lua.luaL_loadbufferx(L, script, len(script), ('@' + sys.argv[1]).encode(), None)
if rc == 0:
    rc = lua.lua_pcallk(L, 0, 0, 0, 0, None)
if rc != 0:
    print('LUA ERROR: %s' % lua.lua_tolstring(L, -1, None).decode('latin-1'))
    sys.exit(1)
lua.lua_getglobal(L, b'OUT')
out = lua.lua_tolstring(L, -1, None)
print(out.decode('latin-1') if out else '(no OUT)')
sys.exit(0 if out and b'fail=0' in out else 1)
