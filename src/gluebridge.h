// gluebridge.h - lets the glue screens (character select / create) talk to the
// server. Stock 3.3.5a has no packet for anything the Centurion character
// screens need (character order, create-time challenge modes), so this module
// adds one Lua function to the character select screen's set:
//
//   CenturionGlueRequest(text)  -> 1 when sent, nil otherwise
//
// It sends `text` as a C string on opcode 0x002 (CMSG_DBLOOKUP, which the stock
// client never sends); the server handles it in
// game/Miscellaneous/CharacterScreen.cpp. See gluebridge.cpp for the reversing.
#pragma once

// Read [GlueBridge] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). ON with no ini: the character screens depend on it.
void GlueBridge_LoadSettings(const char* dir);

// Verify the client signatures, then take over the character select screen's
// script-function (un)registration. Fails safe (logs + stays off) on a mismatch.
void GlueBridge_Install();
