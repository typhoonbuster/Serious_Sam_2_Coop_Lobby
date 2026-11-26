// CRT-less helpers: strings, logging, configuration.
#pragma once
#include "win32.h"

extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);

int  StrLen(const char *s);
int  StrCmp(const char *a, const char *b);
int  StrNCmp(const char *a, const char *b, int n);
void StrCopy(char *dst, const char *src, int cap);      // always NUL-terminates
void StrCat(char *dst, const char *src, int cap);
const char *StrFileName(const char *path);              // pointer after the last '/' or '\'
void StrFileBase(char *dst, const char *path, int cap); // file name without extension

// Logging to <dll dir>/SS2CoopLobby.log (and the engine console once it is available).
void LogInit(void *hModule);
void Log(const char *fmt, ...);   // wsprintfA format (%d %u %x %s %c), max ~900 chars

struct Config {
  int enabled;          // master switch
  int preLobby;         // lobby BEFORE loading: wait until the session is full (max players)
  int lobbyTimeout;     // seconds: max wait in that lobby before loading anyway (0 = forever)
  int pauseDuringLobby; // keep the world frozen until everybody is in (server side)
  int waitOnLevelChange;// also wait for everybody (and play movies) on level transitions
  int loadTimeout;      // seconds: max wait for players still loading (0 = forever)
  int introTimeout;     // seconds: max wait for everybody to finish the intro (0 = forever)
  int playIntro;        // play EPISODE/INTRO movies in coop
  int playOutro;        // play OUTRO movies in coop
  int overlay;          // draw the lobby overlay
  int keyStart;         // virtual key: host starts now (does not wait for the session to fill)
  int language;         // overlay language (enum Lang in lang.h; "auto" = the system language)
  int verbose;          // extra logging
  int steamInvites;     // join a game from a Steam invitation (the base game cannot)
};
extern Config g_cfg;
void ConfigLoad(void *hModule);
