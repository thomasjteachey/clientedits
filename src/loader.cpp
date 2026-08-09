// AnimSpeedLoader - starts Wow.exe suspended, injects AnimSpeedFix.dll, resumes.
// Optional convenience: if you already inject client DLLs some other way
// (proxy DLL, your launcher), use that instead and ignore this.
//
//   AnimSpeedLoader.exe [path\to\Wow.exe]
//
// With no argument it looks for Wow.exe next to itself.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static void Die(const char* what)
{
    char msg[512];
    _snprintf_s(msg, sizeof(msg), _TRUNCATE, "%s (GetLastError = %lu)", what, GetLastError());
    MessageBoxA(NULL, msg, "AnimSpeedLoader", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

int main(int argc, char** argv)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (slash) *slash = 0;

    char exe[MAX_PATH], dll[MAX_PATH];
    if (argc > 1)
        strcpy_s(exe, sizeof(exe), argv[1]);
    else
        _snprintf_s(exe, sizeof(exe), _TRUNCATE, "%s\\Wow.exe", dir);
    _snprintf_s(dll, sizeof(dll), _TRUNCATE, "%s\\AnimSpeedFix.dll", dir);

    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) Die("Wow.exe not found");
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) Die("AnimSpeedFix.dll not found");

    // the client insists on running from its own directory
    char workdir[MAX_PATH];
    strcpy_s(workdir, sizeof(workdir), exe);
    slash = strrchr(workdir, '\\');
    if (slash) *slash = 0;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, workdir, &si, &pi))
        Die("CreateProcess failed");

    SIZE_T len = strlen(dll) + 1;
    void* remote = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) Die("VirtualAllocEx failed");
    if (!WriteProcessMemory(pi.hProcess, remote, dll, len, NULL)) Die("WriteProcessMemory failed");

    // kernel32 is at the same base in both processes on the same session
    LPTHREAD_START_ROUTINE loadLib =
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!loadLib) Die("GetProcAddress(LoadLibraryA) failed");

    HANDLE th = CreateRemoteThread(pi.hProcess, NULL, 0, loadLib, remote, 0, NULL);
    if (!th) Die("CreateRemoteThread failed");
    WaitForSingleObject(th, 10000);

    DWORD moduleBase = 0;
    GetExitCodeThread(th, &moduleBase);
    CloseHandle(th);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);

    if (moduleBase == 0) {
        TerminateProcess(pi.hProcess, 1);
        Die("LoadLibraryA in the target returned NULL - DLL failed to load "
            "(32-bit build? missing CRT?)");
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
