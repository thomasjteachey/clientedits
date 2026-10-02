// gluebridge.cpp - CenturionGlueRequest(text) for the glue screens.
//
// Reversing (Wow.exe 3.3.5a 12340; addresses are VAs):
//
//   0x004E2570  CharacterSelect register:  for (i = 0; i < 13; ++i)
//                 FrameScript_RegisterFunction(table[i].name, table[i].func)
//               over the {name, func} table at 0x00AC4378 (SetCharSelectModelFrame
//               .. GetSelectBackgroundModel; the loop bound is `cmp esi, 68h`).
//   0x004E25A0  CharacterSelect unregister: the same loop through
//               FrameScript_UnregisterFunction(table[i].name).
//   0x00817F90  FrameScript_RegisterFunction(const char*, lua_CFunction), cdecl
//   0x00817FD0  FrameScript_UnregisterFunction(const char*), cdecl
//   0x0084DF60  lua_isstring(L, idx)          (RenameCharacter's argument check)
//   0x0084E0E0  lua_tolstring(L, idx, len*)   (DeclineCharacter reads its names)
//   0x0084E2A0  lua_pushnumber(L, double)     (DeclineCharacter's return value)
//   0x0084E280  lua_pushnil(L)
//
// Both (un)registration functions take no arguments and return nothing, so
// each is replaced whole: a jmp at its entry to a C function that runs the same
// loop and adds CenturionGlueRequest. Nothing of the original body runs, so
// there is no trampoline.
//
// Sending: the same CDataStore sequence the declined-names request builds at
// 0x004D9B06 (PutUInt32 opcode, PutString ..., ClientServices::Send 0x006B0B50),
// which is also what playercollide.cpp's attestation uses in world. The +0x14
// read cursor must be reset to 0 before Send, or the packet goes out a byte long.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "gluebridge.h"

static char g_dir[MAX_PATH] = { 0 };
static int  g_enabled = 1;

static const DWORD kRegisterSite   = 0x004E2570;
static const BYTE  kRegisterSig[]  = { 0x56, 0x33, 0xF6, 0x8B, 0x86, 0x7C, 0x43, 0xAC, 0x00,
                                       0x8B, 0x8E, 0x78, 0x43, 0xAC, 0x00, 0x50, 0x51 };
static const DWORD kUnregisterSite  = 0x004E25A0;
static const BYTE  kUnregisterSig[] = { 0x56, 0x33, 0xF6, 0x8B, 0x86, 0x78, 0x43, 0xAC, 0x00, 0x50 };

// Lua will not call a C function outside Wow.exe's .text: every call is checked
// by 0x0086B5A0 against the .text range that 0x0086B510 reads from the PE section
// headers, and anything else is fatal error #134 "Invalid function pointer".
// So what gets registered is a 5-byte jmp written into the int3 padding right
// after that check, which jumps on to the DLL.
static const DWORD kLuaThunkSite  = 0x0086B5F5;
static const BYTE  kLuaThunkSig[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };

struct GlueFunction { const char* name; void* func; };
static const GlueFunction* const kCharSelectFunctions = (const GlueFunction*)0x00AC4378;
static const int kCharSelectFunctionCount = 13;

typedef void        (__cdecl* RegisterFunction_t)(const char*, void*);
typedef void        (__cdecl* UnregisterFunction_t)(const char*);
typedef int         (__cdecl* LuaIsString_t)(void*, int);
typedef const char* (__cdecl* LuaToLString_t)(void*, int, size_t*);
typedef void        (__cdecl* LuaPushNumber_t)(void*, double);
typedef void        (__cdecl* LuaPushNil_t)(void*);
static const RegisterFunction_t   pRegisterFunction   = (RegisterFunction_t)  0x00817F90;
static const UnregisterFunction_t pUnregisterFunction = (UnregisterFunction_t)0x00817FD0;
static const LuaIsString_t        pLuaIsString        = (LuaIsString_t)       0x0084DF60;
static const LuaToLString_t       pLuaToLString       = (LuaToLString_t)      0x0084E0E0;
static const LuaPushNumber_t      pLuaPushNumber      = (LuaPushNumber_t)     0x0084E2A0;
static const LuaPushNil_t         pLuaPushNil         = (LuaPushNil_t)        0x0084E280;

typedef void* (__thiscall* CdsCtor_t)(void*);
typedef void* (__thiscall* CdsPutU32_t)(void*, DWORD);
typedef void* (__thiscall* CdsPutStr_t)(void*, const char*);
typedef void  (__cdecl*    NetSend_t)(void*);
typedef void  (__thiscall* CdsDtor_t)(void*);
static const CdsCtor_t   pCdsCtor   = (CdsCtor_t)  0x00401050;
static const CdsPutU32_t pCdsPutU32 = (CdsPutU32_t)0x0047B0A0;
static const CdsPutStr_t pCdsPutStr = (CdsPutStr_t)0x0047B300;
static const NetSend_t   pNetSend   = (NetSend_t)  0x006B0B50;
static const CdsDtor_t   pCdsDtor   = (CdsDtor_t)  0x00403880;
#define kNetClientPtr (*(void**)0x00C79CF4)

static const DWORD  kGlueRequestOpcode = 0x002;   // CMSG_DBLOOKUP
static const size_t kMaxRequestLength  = 1024;    // the server drops anything longer
static const char   kFunctionName[]    = "CenturionGlueRequest";

static void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sAnimSpeedFix.log", g_dir);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME t; GetLocalTime(&t);
    char msg[768];
    va_list ap; va_start(ap, fmt);
    int n = _snprintf_s(msg, sizeof(msg), _TRUNCATE, "[%02d:%02d:%02d pid%lu] gluebridge: ",
                        t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    int m = _vsnprintf_s(msg + n, sizeof(msg) - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0 && m >= 0) {
        int len = n + m;
        if (len < (int)sizeof(msg) - 2) { msg[len] = '\r'; msg[len + 1] = '\n'; len += 2; }
        DWORD w = 0; WriteFile(h, msg, (DWORD)len, &w, NULL);
    }
    CloseHandle(h);
}

static bool IsReadable(const void* addr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return false;
    return (const BYTE*)addr + len <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

// CenturionGlueRequest(text): 1 when the request went out, nil otherwise.
static int __cdecl Lua_CenturionGlueRequest(void* L)
{
    if (!pLuaIsString(L, 1)) {
        pLuaPushNil(L);
        return 1;
    }
    const char* text = pLuaToLString(L, 1, NULL);
    if (!text || !*text || strlen(text) > kMaxRequestLength || !kNetClientPtr) {
        pLuaPushNil(L);                                  // Send aborts on a NULL connection
        return 1;
    }

    DWORD cds[8] = { 0 };                                // CDataStore is 0x18
    pCdsCtor(cds);                                       // sets +0x14 = 0xFFFFFFFF
    pCdsPutU32(cds, kGlueRequestOpcode);
    pCdsPutStr(cds, text);                               // with its terminator
    cds[5] = 0;                                          // +0x14 read cursor = 0 (REQUIRED)
    pNetSend(cds);
    pCdsDtor(cds);

    pLuaPushNumber(L, 1.0);
    return 1;
}

// Replaces 0x004E2570 whole.
static void __cdecl CharSelectRegisterFunctions()
{
    for (int i = 0; i < kCharSelectFunctionCount; ++i)
        pRegisterFunction(kCharSelectFunctions[i].name, kCharSelectFunctions[i].func);
    pRegisterFunction(kFunctionName, (void*)kLuaThunkSite);   // see kLuaThunkSite

}

// Replaces 0x004E25A0 whole.
static void __cdecl CharSelectUnregisterFunctions()
{
    for (int i = 0; i < kCharSelectFunctionCount; ++i)
        pUnregisterFunction(kCharSelectFunctions[i].name);
    pUnregisterFunction(kFunctionName);
}

static bool SigOk(DWORD site, const BYTE* sig, size_t len)
{
    return IsReadable((void*)site, len) && memcmp((void*)site, sig, len) == 0;
}

// jmp rel32 at the function entry; the rest of the signature is left alone
// because none of it runs again.
static bool JumpTo(DWORD site, void* target)
{
    DWORD old = 0;
    BYTE* p = (BYTE*)site;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)target - (site + 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

void GlueBridge_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir);

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;                                          // no ini => stays ON
    g_enabled = GetPrivateProfileIntA("GlueBridge", "Enabled", 1, ini);
}

void GlueBridge_Install()
{
    if (!g_enabled)
        return;

    // Verify everything before touching anything, so a different build is left
    // exactly as it was.
    if (!SigOk(kRegisterSite, kRegisterSig, sizeof(kRegisterSig)) ||
        !SigOk(kUnregisterSite, kUnregisterSig, sizeof(kUnregisterSig)) ||
        !SigOk(kLuaThunkSite, kLuaThunkSig, sizeof(kLuaThunkSig)) ||
        !IsReadable(kCharSelectFunctions, sizeof(GlueFunction) * kCharSelectFunctionCount)) {
        Log("not applied: client signature mismatch (not Wow.exe 3.3.5a 12340?)");
        g_enabled = 0;
        return;
    }

    // The thunk before anything registers it.
    if (!JumpTo(kLuaThunkSite, (void*)&Lua_CenturionGlueRequest)) {
        Log("not applied: VirtualProtect failed at 0x%08lX", kLuaThunkSite);
        g_enabled = 0;
        return;
    }

    // Unregister first: if only the register hook went in, the function would
    // outlive the screen that owns it.
    if (!JumpTo(kUnregisterSite, (void*)&CharSelectUnregisterFunctions)) {
        Log("not applied: VirtualProtect failed at 0x%08lX", kUnregisterSite);
        g_enabled = 0;
        return;
    }
    if (!JumpTo(kRegisterSite, (void*)&CharSelectRegisterFunctions)) {
        Log("not applied: VirtualProtect failed at 0x%08lX", kRegisterSite);
        g_enabled = 0;
        return;
    }
}
