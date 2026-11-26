#include "util.h"
#include "lang.h"
#include <stdarg.h>

// The compiler may emit calls to these even with -ffreestanding.
extern "C" void *memset(void *dst, int c, unsigned int n) {
  u8 *d = (u8 *)dst;
  while (n--) *d++ = (u8)c;
  return dst;
}
extern "C" void *memcpy(void *dst, const void *src, unsigned int n) {
  u8 *d = (u8 *)dst;
  const u8 *s = (const u8 *)src;
  while (n--) *d++ = *s++;
  return dst;
}
// Floating point marker required by the MSVC ABI when floats are used.
extern "C" int _fltused = 0;

int StrLen(const char *s) {
  int n = 0;
  if (!s) return 0;
  while (s[n]) n++;
  return n;
}
int StrCmp(const char *a, const char *b) {
  if (!a) a = "";
  if (!b) b = "";
  while (*a && *a == *b) { a++; b++; }
  return (u8)*a - (u8)*b;
}
int StrNCmp(const char *a, const char *b, int n) {
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) return (u8)a[i] - (u8)b[i];
    if (!a[i]) return 0;
  }
  return 0;
}
void StrCopy(char *dst, const char *src, int cap) {
  int i = 0;
  if (cap <= 0) return;
  if (src)
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
  dst[i] = 0;
}
void StrCat(char *dst, const char *src, int cap) {
  int n = StrLen(dst);
  if (n < cap) StrCopy(dst + n, src, cap - n);
}
const char *StrFileName(const char *path) {
  const char *r = path;
  if (!path) return "";
  for (const char *p = path; *p; p++)
    if (*p == '/' || *p == '\\') r = p + 1;
  return r;
}
void StrFileBase(char *dst, const char *path, int cap) {
  StrCopy(dst, StrFileName(path), cap);
  for (int i = StrLen(dst) - 1; i >= 0; i--)
    if (dst[i] == '.') { dst[i] = 0; break; }
}

// ---------------------------------------------------------------------------------------------
static char g_dllDir[300];
static char g_logPath[320];
static int  g_logOk = 0;

static void ModuleDir(void *hModule, char *out, int cap) {
  char buf[300];
  u32 n = GetModuleFileNameA(hModule, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) { StrCopy(out, ".\\", cap); return; }
  int cut = 0;
  for (int i = 0; buf[i]; i++)
    if (buf[i] == '\\' || buf[i] == '/') cut = i + 1;
  buf[cut] = 0;
  StrCopy(out, buf, cap);
}

void LogInit(void *hModule) {
  ModuleDir(hModule, g_dllDir, sizeof(g_dllDir));
  StrCopy(g_logPath, g_dllDir, sizeof(g_logPath));
  StrCat(g_logPath, "SS2CoopLobby.log", sizeof(g_logPath));
  void *h = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (h != INVALID_HANDLE) { CloseHandle(h); g_logOk = 1; }
}

// Engine console hook-up (set by engine.cpp once symbols are resolved).
void (*g_consoleSink)(const char *line) = 0;

void Log(const char *fmt, ...) {
  char msg[1024];
  char line[1100];
  va_list ap;
  va_start(ap, fmt);
  // Forward up to 12 dword args to wsprintfA (all our arguments are ints or pointers).
  u32 a[12];
  for (int i = 0; i < 12; i++) a[i] = va_arg(ap, u32);
  va_end(ap);
  wsprintfA(msg, fmt, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
  SYSTEMTIME_ st;
  GetLocalTime(&st);
  wsprintfA(line, "[%02d:%02d:%02d.%03d] %s\r\n", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, msg);
  OutputDebugStringA(line);
  if (g_logOk) {
    void *h = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != INVALID_HANDLE) {
      u32 w;
      SetFilePointer(h, 0, 0, FILE_END);
      WriteFile(h, line, StrLen(line), &w, 0);
      CloseHandle(h);
    }
  }
  if (g_consoleSink) {
    // The engine formatter interprets '%': replace it to stay safe.
    for (char *p = msg; *p; p++)
      if (*p == '%') *p = '#';
    StrCat(msg, "\n", sizeof(msg));
    g_consoleSink(msg);
  }
}

// ---------------------------------------------------------------------------------------------
Config g_cfg;

void ConfigLoad(void *hModule) {
  char ini[320];
  if (!g_dllDir[0]) ModuleDir(hModule, g_dllDir, sizeof(g_dllDir));
  StrCopy(ini, g_dllDir, sizeof(ini));
  StrCat(ini, "SS2CoopLobby.ini", sizeof(ini));
  const char *S = "Lobby";
  g_cfg.enabled           = GetPrivateProfileIntA(S, "Enabled", 1, ini);
  g_cfg.preLobby          = GetPrivateProfileIntA(S, "PreLobby", 1, ini);
  g_cfg.lobbyTimeout      = GetPrivateProfileIntA(S, "LobbyTimeout", 0, ini);
  g_cfg.pauseDuringLobby  = GetPrivateProfileIntA(S, "PauseDuringLobby", 1, ini);
  g_cfg.waitOnLevelChange = GetPrivateProfileIntA(S, "WaitOnLevelChange", 1, ini);
  g_cfg.loadTimeout       = GetPrivateProfileIntA(S, "LoadTimeout", 120, ini);
  g_cfg.introTimeout      = GetPrivateProfileIntA(S, "IntroTimeout", 300, ini);
  g_cfg.playIntro         = GetPrivateProfileIntA(S, "PlayIntro", 1, ini);
  g_cfg.playOutro         = GetPrivateProfileIntA(S, "PlayOutro", 1, ini);
  g_cfg.overlay           = GetPrivateProfileIntA(S, "Overlay", 1, ini);
  g_cfg.keyStart          = GetPrivateProfileIntA(S, "KeyStart", 0x77 /*F8*/, ini);
  g_cfg.verbose           = GetPrivateProfileIntA(S, "Verbose", 0, ini);
  g_cfg.steamInvites      = GetPrivateProfileIntA(S, "SteamInvites", 1, ini);
  char lang[16];
  GetPrivateProfileStringA(S, "Language", "auto", lang, sizeof(lang), ini);
  g_cfg.language = LangFromSetting(lang);
  Log("config: %s enabled=%d prelobby=%d(timeout %ds) pause=%d waitlevel=%d intro=%d outro=%d "
      "language=%s", ini, g_cfg.enabled, g_cfg.preLobby, g_cfg.lobbyTimeout, g_cfg.pauseDuringLobby,
      g_cfg.waitOnLevelChange, g_cfg.playIntro, g_cfg.playOutro, LangCode(g_cfg.language));
}
