#!/usr/bin/env python3
"""Compile (not run) a Lua chunk with the system liblua, to catch syntax errors
that a structural checker cannot. Usage: lua_compile.py <file.lua> [...]"""
import ctypes
import sys

L = ctypes.CDLL('liblua5.3.so.0')
L.luaL_newstate.restype = ctypes.c_void_p
L.luaL_loadbufferx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                               ctypes.c_char_p, ctypes.c_char_p]
L.luaL_loadbufferx.restype = ctypes.c_int
L.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
L.lua_tolstring.restype = ctypes.c_char_p

rc_all = 0
for path in sys.argv[1:]:
    with open(path, 'rb') as f:
        src = f.read()
    state = ctypes.c_void_p(L.luaL_newstate())
    rc = L.luaL_loadbufferx(state, src, len(src), path.encode(), None)
    if rc == 0:
        print('OK       %s (%d bytes)' % (path, len(src)))
    else:
        msg = L.lua_tolstring(state, -1, None)
        print('SYNTAX   %s: %s' % (path, msg.decode('utf-8', 'replace') if msg else '?'))
        rc_all = 1
sys.exit(rc_all)
