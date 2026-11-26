#include "engine.h"
#include "util.h"

Engine E;
extern void (*g_consoleSink)(const char *line);

static void ConsoleSink(const char *line) {
  if (E.conLogF) E.conLogF(line);
}

static u32 PeTimestamp(void *mod) {
  u8 *b = (u8 *)mod;
  u32 pe = *(u32 *)(b + 0x3c);
  return *(u32 *)(b + pe + 8);
}

static bool g_resolveOk = true;
static void *Sym(void *mod, const char *dll, const char *name) {
  void *p = GetProcAddress(mod, name);
  if (!p) {
    Log("ERROR: symbol not found in %s: %s", dll, name);
    g_resolveOk = false;
  }
  return p;
}

// An MSVC SEH prologue "55 8B EC 6A FF 68 <handler>" with the handler at a known RVA identifies
// the exact function of this build, even if the DLL got relocated.
static bool CheckSehPrologue(u8 *fn, u32 handlerRva, const char *what, void *module = 0) {
  static const u8 pro[6] = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
  for (int i = 0; i < 6; i++)
    if (fn[i] != pro[i]) {
      Log("ERROR: unexpected prologue for %s at %x", what, (u32)fn);
      return false;
    }
  u32 h = *(u32 *)(fn + 6) - (u32)(module ? module : E.gameBase);
  if (h != handlerRva) {
    Log("ERROR: unexpected SEH handler for %s (%x != %x)", what, h, handlerRva);
    return false;
  }
  return true;
}

bool EngineResolve(void) {
  memset(&E, 0, sizeof(E));
  E.hCore = GetModuleHandleA("Core.dll");
  E.hEngine = GetModuleHandleA("Engine.dll");
  E.hGame = GetModuleHandleA("Sam2Game.dll");
  if (!E.hCore || !E.hEngine || !E.hGame) {
    Log("ERROR: game modules not loaded (Core=%x Engine=%x Sam2Game=%x)", (u32)E.hCore,
        (u32)E.hEngine, (u32)E.hGame);
    return false;
  }
  E.gameBase = (u8 *)E.hGame;
  u32 tsGame = PeTimestamp(E.hGame), tsEng = PeTimestamp(E.hEngine);
  Log("Sam2Game.dll at %x (timestamp %u), Engine.dll timestamp %u", (u32)E.gameBase, tsGame, tsEng);
  if (tsGame != SAM2GAME_TIMESTAMP || tsEng != ENGINE_TIMESTAMP) {
    Log("ERROR: unsupported game build (expected Sam2Game %u / Engine %u). Mod disabled.",
        SAM2GAME_TIMESTAMP, ENGINE_TIMESTAMP);
    return false;
  }

  void *C = E.hCore, *N = E.hEngine, *G = E.hGame;
  *(void **)&E.CString_ctor        = Sym(C, "Core", "??0CString@SeriousEngine@@QAE@PBD@Z");
  *(void **)&E.CString_dtor        = Sym(C, "Core", "??1CString@SeriousEngine@@QAE@XZ");
  *(void **)&E.conLogF             = Sym(C, "Core", "?conLogF@SeriousEngine@@YAXPBDZZ");
  *(void **)&E.hvHandleToPointer   = Sym(C, "Core", "?hvHandleToPointer@SeriousEngine@@YAPAXK@Z");
  *(void **)&E.sysIsAppForeground  = Sym(C, "Core", "?sysIsAppForeground@SeriousEngine@@YAHXZ");
  *(void **)&E.mdModuleDeclareBeg  = Sym(C, "Core", "?mdModuleDeclareBeg@SeriousEngine@@YAXPBD@Z");
  *(void **)&E.mdModuleDeclareEnd  = Sym(C, "Core", "?mdModuleDeclareEnd@SeriousEngine@@YAXXZ");

  *(void **)&E.netIsLocal          = Sym(N, "Engine", "?netIsLocal@SeriousEngine@@YAHXZ");
  *(void **)&E.netIsRemote         = Sym(N, "Engine", "?netIsRemote@SeriousEngine@@YAHXZ");
  *(void **)&E.netIsServer         = Sym(N, "Engine", "?netIsServer@SeriousEngine@@YAHXZ");
  *(void **)&E.netIsHost           = Sym(N, "Engine", "?netIsHost@SeriousEngine@@YAHXZ");
  *(void **)&E.netIsDemoPlayback   = Sym(N, "Engine", "?netIsDemoPlayback@SeriousEngine@@YAHXZ");
  *(void **)&E.netGetCurrent       = Sym(N, "Engine", "?netGetCurrent@SeriousEngine@@YAPAVCNetworkInterface@1@XZ");
  *(void **)&E.cliGetClientIndex   = Sym(N, "Engine", "?cliGetClientIndex@SeriousEngine@@YAJXZ");
  *(void **)&E.Sim_IsStarted       = Sym(N, "Engine", "?IsStarted@CSimulation@SeriousEngine@@QAEHXZ");
  *(void **)&E.Sim_IsPaused        = Sym(N, "Engine", "?IsPaused@CSimulation@SeriousEngine@@QAEHXZ");
  *(void **)&E.Sim_RequestPause    = Sym(N, "Engine", "?RequestPause@CSimulation@SeriousEngine@@QAEXXZ");
  *(void **)&E.Sim_RequestUnPause  = Sym(N, "Engine", "?RequestUnPause@CSimulation@SeriousEngine@@QAEXXZ");
  *(void **)&E.CCurrentDrawPort_ctor = Sym(N, "Engine", "??0CCurrentDrawPort@SeriousEngine@@QAE@PAVCDrawPort@1@H@Z");
  *(void **)&E.gfxPopCurrentDrawPort = Sym(N, "Engine", "?gfxPopCurrentDrawPort@SeriousEngine@@YAXXZ");
  *(void **)&E.gfuOrtho            = Sym(N, "Engine", "?gfuOrtho@SeriousEngine@@YAXXZ");
  *(void **)&E.gfuSetCurrentFont   = Sym(N, "Engine", "?gfuSetCurrentFont@SeriousEngine@@YAXPAVCFontInstance@1@@Z");
  *(void **)&E.gfuPutColorText3f   = Sym(N, "Engine", "?gfuPutColorText3f@SeriousEngine@@YAXPBDABVVector3f@1@K@Z");
  *(void **)&E.gfuGetStringWidth   = Sym(N, "Engine", "?gfuGetStringWidth@SeriousEngine@@YAMPBDPBVCFontInstance@1@@Z");
  *(void **)&E.gfuGetStringHeight  = Sym(N, "Engine", "?gfuGetStringHeight@SeriousEngine@@YAMPBDPBVCFontInstance@1@@Z");
  *(void **)&E.gfuGradient3f       = Sym(N, "Engine", "?gfuGradient3f@SeriousEngine@@YAXABVBox3f@1@KKKK@Z");
  *(void **)&E.simStart_t          = Sym(N, "Engine", "?simStart_t@SeriousEngine@@YAXPBD@Z");
  E.fnChangeWorld = (u8 *)Sym(N, "Engine", "?ChangeWorld@CSimulation@SeriousEngine@@QAEXPBD@Z");
  *(void **)&E.Loader_Reset        = Sym(C, "Core", "?ResetLoader@CResourceLoader@SeriousEngine@@QAEXXZ");
  *(void **)&E.priGetCurrent       = Sym(N, "Engine", "?priGetCurrent@SeriousEngine@@YAPAVCProjectInstance@1@XZ");
  *(void **)&E.wldGetSoundSyncList = Sym(N, "Engine", "?wldGetSoundSyncList@SeriousEngine@@YAPAVCSoundSyncList@1@XZ");
  *(void **)&E.wldSuspendSyncList  = Sym(N, "Engine", "?wldSuspendSyncList@SeriousEngine@@YAXXZ");
  *(void **)&E.SoundList_Suspend   = Sym(N, "Engine", "?SuspendList@CSoundSyncList@SeriousEngine@@QAEXXZ");
  *(void **)&E.wldGetCurrent       = Sym(N, "Engine", "?wldGetCurrent@SeriousEngine@@YAPAVCWorld@1@XZ");
  E.fnIterateEntities = (u8 *)Sym(N, "Engine", "?wldIterateEntities@SeriousEngine@@YAXPAVCEntityIterator@1@@Z");

  *(void **)&E.samIsCoop           = Sym(G, "Sam2Game", "?samIsCoop@SeriousEngine@@YAHXZ");
  *(void **)&E.samGetGameInfoEntity = Sym(G, "Sam2Game", "?samGetGameInfoEntity@SeriousEngine@@YA?AV?$Handle@VCGameInfoEntity@SeriousEngine@@@1@XZ");
  E.fnCreatePlayer  = (u8 *)Sym(G, "Sam2Game", "?CreatePlayer@CGameInfoEntity@SeriousEngine@@UAEXJVCString@2@@Z");
  E.fnSpreadFrags   = (u8 *)Sym(G, "Sam2Game", "?SpreadCountOfLeftFragsChanged@CGameInfoEntity@SeriousEngine@@QAEXJ@Z");
  E.sam_bSkipMovies = (int *)Sym(G, "Sam2Game", "?sam_bSkipMovies@SeriousEngine@@3HA");
  if (!g_resolveOk) return false;

  u8 *b = E.gameBase;
  *(void **)&E.QueueLevelMovies = b + RVA_FN_QUEUE_LEVEL_MOVIES;
  *(void **)&E.QueueInputLock   = b + RVA_FN_QUEUE_INPUT_LOCK;
  E.projectVtbl = (void **)(b + RVA_PROJECT_VTABLE);
  E.fontBig     = b + RVA_FONT_BIG;
  E.fontSmall   = b + RVA_FONT_SMALL;
  E.hud_bShowPausedMessage = (int *)(b + RVA_CVAR_SHOWPAUSEDMSG);

  // Signature checks on everything we call or patch that is not exported.
  bool ok = true;
  ok &= CheckSehPrologue(b + RVA_FN_QUEUE_LEVEL_MOVIES, 0x269da8, "QueueLevelMovies");
  ok &= CheckSehPrologue(b + RVA_FN_QUEUE_INPUT_LOCK, 0x26b141, "QueueInputLock");
  ok &= CheckSehPrologue(E.fnCreatePlayer, 0x25233a, "CGameInfoEntity::CreatePlayer");
  ok &= CheckSehPrologue(E.fnSpreadFrags, 0x252410, "CGameInfoEntity::SpreadCountOfLeftFragsChanged");
  ok &= CheckSehPrologue(E.fnIterateEntities, 0x1f54c8, "wldIterateEntities", E.hEngine);
  struct { int slot; u32 rva; } vt[] = {
      {8, RVA_FN_PROJECT_STEP}, {9, RVA_FN_PROJECT_RENDER},
      {15, RVA_FN_PROJECT_WORLDLOAD}, {18, RVA_FN_PROJECT_WORLDSTOP}};
  for (int i = 0; i < 4; i++) {
    u32 v = (u32)E.projectVtbl[vt[i].slot] - (u32)b;
    if (v != vt[i].rva) {
      Log("ERROR: project vtable slot %d = %x, expected %x", vt[i].slot, v, vt[i].rva);
      ok = false;
    }
  }
  if (!ok) return false;
  g_consoleSink = ConsoleSink;
  return true;
}

// Steam invitations reuse the game's own join routine (the one of its server list) and the cvars
// it reads. Check that the code still refers to them where expected before using any of it.
bool EngineResolveInvites(void) {
  u8 *b = E.gameBase;
  u8 *join = b + RVA_FN_JOIN_SERVER, *close = b + RVA_FN_CLOSE_MENUS;
  int *gs = (int *)GetProcAddress(E.hEngine, "?net_bSteamGameserver@SeriousEngine@@3HA");
  u32 *lobby = (u32 *)GetProcAddress(E.hEngine, "?net_idSteamServerLobby@SeriousEngine@@3VCSteamID@@A");
  bool ok = gs && lobby && CheckSehPrologue(join, 0x24b8ba, "JoinServer");
  // join+0xce: mov ecx,[sam_strConnect]  join+0x155: mov eax,[observer]  join+0x15a: mov ecx,[private]
  ok = ok && *(u32 *)(join + 0xd0) == (u32)(b + RVA_CVAR_CONNECT) &&
       *(u32 *)(join + 0x156) == (u32)(b + RVA_CVAR_JOIN_OBSERVER) &&
       *(u32 *)(join + 0x15c) == (u32)(b + RVA_CVAR_JOIN_PRIVATE);
  // close: mov ecx,[menu list]
  ok = ok && close[0] == 0x8b && close[1] == 0x0d && *(u32 *)(close + 2) == (u32)(b + RVA_MENU_LIST);
  // menu screen commands (SequencerShowsOnlyMenus): their constructor stores the expected vftable
  u8 *vtStore = b + RVA_MENU_CMD_VT_STORE;
  ok = ok && vtStore[0] == 0xc7 && vtStore[1] == 0x06 &&
       *(u32 *)(vtStore + 2) == (u32)(b + RVA_VT_MENU_COMMAND);
  if (!ok) {
    Log("Steam invites: the game's join code is not the expected one, invites disabled");
    return false;
  }
  E.net_bSteamGameserver = gs;
  E.net_idSteamServerLobby = lobby;
  *(void **)&E.JoinServer = join;
  *(void **)&E.CloseMenus = close;
  E.sam_strConnect = (CStr *)(b + RVA_CVAR_CONNECT);
  E.sam_bJoinAsPrivate = (int *)(b + RVA_CVAR_JOIN_PRIVATE);
  E.sam_bJoinAsObserver = (int *)(b + RVA_CVAR_JOIN_OBSERVER);
  return true;
}

void *GetGameInfo(void) {
  ulong h = 0;
  E.samGetGameInfoEntity(&h);
  if (!h) return 0;
  return E.hvHandleToPointer(h);
}

bool CmdQueueIdle(void *project) {
  if (!project) return true;
  int *q = *(int **)((u8 *)project + PRJ_CMDQUEUE);
  if (!q) return true;
  return q[0] == 0 && q[3] == 0;   // no current command and nothing queued
}

// A menu screen other than the loading screen (which waits like a movie).
static bool IsMenuScreen(void *cmd) {
  if (!cmd || *(u32 *)cmd != (u32)(E.gameBase + RVA_VT_MENU_COMMAND)) return false;
  const char *name = *(const char **)((u8 *)cmd + CMD_MENU_NAME);
  return !(name && StrCmp(name, "CMSLoading") == 0);
}

// Queue layout {current, capacity, array, count, ...} (FUN_102314e0 appends, FUN_102313a0 pops).
bool SequencerShowsOnlyMenus(void *project) {
  if (!project) return false;
  int *q = *(int **)((u8 *)project + PRJ_CMDQUEUE);
  if (!q) return true;
  if (q[0] && !IsMenuScreen((void *)q[0])) return false;
  int n = q[3];
  void **arr = (void **)q[2];
  if (n < 0 || n > 256 || (n > 0 && !arr)) return false;
  for (int i = 0; i < n; i++)
    if (!IsMenuScreen(arr[i])) return false;
  return true;
}

u8 *ServerSimStartCallSite(void) {
  u8 *p = E.gameBase + RVA_CALL_SERVER_SIMSTART;
  // push eax; call dword ptr [IAT simStart_t]; add esp,4
  if (p[-1] != 0x50 || p[0] != 0xff || p[1] != 0x15 || p[6] != 0x83 || p[7] != 0xc4 || p[8] != 0x04)
    return 0;
  if (*(u32 *)(p + 2) - (u32)E.gameBase != RVA_IAT_SIMSTART) return 0;
  return p;
}

void CStrSet(CStr *s, const char *txt) { E.CString_ctor(s, txt); }
void CStrFree(CStr *s) {
  if (s->p) E.CString_dtor(s);
  s->p = 0;
}
