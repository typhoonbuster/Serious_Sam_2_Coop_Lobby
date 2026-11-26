// Minimal Win32 declarations for a freestanding (no CRT, no SDK) i686 DLL.
#pragma once

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef int                i32;
typedef unsigned long      ulong;

#define WINAPI __stdcall

#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_READWRITE         0x04
#define MEM_COMMIT             0x1000
#define MEM_RESERVE            0x2000
#define GENERIC_WRITE          0x40000000
#define FILE_SHARE_READ        0x1
#define FILE_SHARE_WRITE       0x2
#define CREATE_ALWAYS          2
#define OPEN_ALWAYS            4
#define FILE_ATTRIBUTE_NORMAL  0x80
#define FILE_END               2
#define INVALID_HANDLE         ((void *)-1)

struct SYSTEMTIME_ {
  u16 wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

extern "C" {
// kernel32
__declspec(dllimport) void *WINAPI GetModuleHandleA(const char *name);
__declspec(dllimport) void *WINAPI GetProcAddress(void *mod, const char *name);
__declspec(dllimport) int   WINAPI VirtualProtect(void *addr, u32 size, u32 prot, u32 *oldProt);
__declspec(dllimport) void *WINAPI VirtualAlloc(void *addr, u32 size, u32 type, u32 prot);
__declspec(dllimport) int   WINAPI FlushInstructionCache(void *proc, const void *addr, u32 size);
__declspec(dllimport) void *WINAPI GetCurrentProcess(void);
__declspec(dllimport) u32   WINAPI GetTickCount(void);
__declspec(dllimport) void *WINAPI CreateFileA(const char *fn, u32 access, u32 share, void *sa,
                                               u32 disp, u32 flags, void *tmpl);
__declspec(dllimport) int   WINAPI WriteFile(void *h, const void *buf, u32 n, u32 *written, void *ov);
__declspec(dllimport) int   WINAPI CloseHandle(void *h);
__declspec(dllimport) u32   WINAPI SetFilePointer(void *h, i32 dist, i32 *distHigh, u32 method);
__declspec(dllimport) u32   WINAPI GetModuleFileNameA(void *mod, char *buf, u32 size);
__declspec(dllimport) u32   WINAPI GetPrivateProfileIntA(const char *sec, const char *key, int def,
                                                         const char *file);
__declspec(dllimport) u32   WINAPI GetPrivateProfileStringA(const char *sec, const char *key,
                                                            const char *def, char *out, u32 size,
                                                            const char *file);
__declspec(dllimport) void  WINAPI OutputDebugStringA(const char *s);
__declspec(dllimport) void  WINAPI GetLocalTime(SYSTEMTIME_ *st);
__declspec(dllimport) const char *WINAPI GetCommandLineA(void);
__declspec(dllimport) u32   WINAPI GetUserDefaultLCID(void);
// user32
__declspec(dllimport) short WINAPI GetAsyncKeyState(int vk);
__declspec(dllimport) int __cdecl wsprintfA(char *out, const char *fmt, ...);
}
