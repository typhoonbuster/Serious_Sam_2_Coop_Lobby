// Serious Engine 2 / Serious Sam 2 v2.224 (Steam build 24849618, 2026) bindings.
// Every address below was recovered by reverse engineering and is checked at startup.
#pragma once
#include "win32.h"

// Engine CString is a single char* (sizeof == 4), allocated by the engine allocator.
struct CStr { char *p; };

// PE TimeDateStamp of the supported build (checked at startup).
#define SAM2GAME_TIMESTAMP 1772656946u
#define ENGINE_TIMESTAMP   1772429639u

// --- Sam2Game.dll RVAs (image base 0x10000000) ---------------------------------------------
#define RVA_PROJECT_VTABLE        0x29cf10  // CSamProjectInstance vftable (28 slots)
#define RVA_FN_QUEUE_LEVEL_MOVIES 0x21ef50  // int  __cdecl(const char *level, int which, float fade)
#define RVA_FN_QUEUE_INPUT_LOCK   0x22f950  // void __cdecl(int on)
#define RVA_FN_PROJECT_STEP       0x0254d0  // vtbl[8]  int  Step()
#define RVA_FN_PROJECT_RENDER     0x0221d0  // vtbl[9]  void Render(CDrawPort*, float)
#define RVA_FN_PROJECT_WORLDLOAD  0x023a10  // vtbl[15] int  OnWorldLoadBegin(const char *world)
#define RVA_FN_PROJECT_WORLDSTOP  0x022af0  // vtbl[18] int  OnWorldStopped(const char *oldWorld)
#define RVA_FONT_BIG              0x4031d0  // CFontInstance used for "PAUSED"
#define RVA_FONT_SMALL            0x403188  // CFontInstance used for HUD texts
#define RVA_CVAR_SHOWPAUSEDMSG    0x3f9e1c  // hud_bShowPausedMessage
#define RVA_CALL_SERVER_SIMSTART  0x026308  // in FUN_10026130 (start server): FF 15 [simStart_t]
#define RVA_IAT_SIMSTART          0x294a38  //   IAT slot of Engine!simStart_t
// Joining a Steam lobby, as the game's server list does (FUN_101cb1b0):
#define RVA_FN_JOIN_SERVER        0x0263c0  // void __fastcall(project): join net_idSteamServerLobby
#define RVA_FN_CLOSE_MENUS        0x19dc50  // void __cdecl(void): close the open menu screens
#define RVA_MENU_LIST             0x40a270  //   list walked by it (checked in its first instruction)
#define RVA_CVAR_CONNECT          0x4030a0  // sam_strConnect ("+connect <id>": join at startup)
#define RVA_CVAR_JOIN_PRIVATE     0x402fa0  // sam_bJoinAsPrivate
#define RVA_CVAR_JOIN_OBSERVER    0x402fa4  // sam_bJoinAsObserver
// The project's sequencer (PRJ_CMDQUEUE) runs menu screens too, not only movies: the main menu is a
// command of it, so the queue is never empty in the menus.
#define RVA_VT_MENU_COMMAND       0x2e2454  // vftable of a menu screen command (FUN_102303f0)
#define RVA_MENU_CMD_VT_STORE     0x2301b2  //   its ctor FUN_10230180: C7 06 <vftable> (mov [esi],imm)
#define CMD_MENU_NAME             0x10      //   CString: screen class ("CMSLoading", "" = main menu)

// which-index for RVA_FN_QUEUE_LEVEL_MOVIES (from the .nfo parser)
#define MOVIE_INTRO   0
#define MOVIE_OUTRO   1
#define MOVIE_EPISODE 2

// --- object offsets ----------------------------------------------------------------------------
#define PRJ_SIM        0x008   // CSimulation*
#define PRJ_DEDICATED  0x014   // int, 1 on dedicated servers
#define PRJ_GAMETYPE   0x07c   // 1 single player, 2 coop, 4 deathmatch
#define PRJ_MAXPLAYERS 0x094   // max players of the session (host included on listen servers)
#define PRJ_SLOT_SETCURRENT   19   // vtbl: netSetCurrent/simSetCurrent/aniSetTime for this project
#define PRJ_SLOT_UNSETCURRENT 20   // vtbl: aniResetTime + netSetCurrent(0) (end of Step/Render)
#define SIM_LOADER     0x06c   // CSimulation: CResourceLoader*
#define SSL_SUSPENDED  0x008   // CSoundSyncList: 1 = suspended (also the initial value of a new list)
#define PRJ_NETIF      0x108   // CNetworkInterface*
#define PRJ_CMDQUEUE   0x11c   // command/movie sequencer: {cur, cap, arr, count, ...}
#define SRV_CLIENTS    0xa70   // CServerInterface: client records
#define SRV_CLIENT_SZ  0xa18   //   record stride; +0 = state (3 = connected)
#define SRV_NCLIENTS   17      //   indices 0..16 (record 17 is never a remote player)
#define DP_X0 0x04             // CDrawPort pixel box
#define DP_Y0 0x08
#define DP_X1 0x0c
#define DP_Y1 0x10
#define GI_GAMETYPE    0xe0    // CGameInfoEntity: 2 = coop
#define GI_PLAYERS     0x24    // CGameInfoEntity: Handle<CPlayerBrainEntity>[17]. Brains AND puppets
                               // move to the next world on a level change: a player "exists" from
                               // the first frame of the next level on.
#define HOST_PLAYER_INDEX 16   // local player index of a listen-server host

struct Engine {
  void *hCore, *hEngine, *hGame;
  u8   *gameBase;
  // Core
  void (__thiscall *CString_ctor)(CStr *self, const char *s);
  void (__thiscall *CString_dtor)(CStr *self);
  void (__cdecl *conLogF)(const char *fmt, ...);
  void *(__cdecl *hvHandleToPointer)(ulong h);
  int (__cdecl *sysIsAppForeground)(void);
  void (__cdecl *mdModuleDeclareBeg)(const char *name);
  void (__cdecl *mdModuleDeclareEnd)(void);
  // Engine
  int (__cdecl *netIsLocal)(void);
  int (__cdecl *netIsRemote)(void);
  int (__cdecl *netIsServer)(void);
  int (__cdecl *netIsHost)(void);
  int (__cdecl *netIsDemoPlayback)(void);
  void *(__cdecl *netGetCurrent)(void);
  i32 (__cdecl *cliGetClientIndex)(void);
  int (__thiscall *Sim_IsStarted)(void *sim);
  int (__thiscall *Sim_IsPaused)(void *sim);
  void (__thiscall *Sim_RequestPause)(void *sim);
  void (__thiscall *Sim_RequestUnPause)(void *sim);
  void *(__thiscall *CCurrentDrawPort_ctor)(void *self, void *dp, int b);
  void (__cdecl *gfxPopCurrentDrawPort)(void);
  void (__cdecl *gfuOrtho)(void);
  void (__cdecl *gfuSetCurrentFont)(void *font);
  void (__cdecl *gfuPutColorText3f)(const char *s, const float *pos3, u32 color);
  float (__cdecl *gfuGetStringWidth)(const char *s, const void *font);
  float (__cdecl *gfuGetStringHeight)(const char *s, const void *font);
  void (__cdecl *gfuGradient3f)(const float *box6, u32 c0, u32 c1, u32 c2, u32 c3);
  void (__cdecl *simStart_t)(const char *world);
  u8 *fnChangeWorld;                                      // CSimulation::ChangeWorld(const char*)
  void (__thiscall *Loader_Reset)(void *loader);          // Core CResourceLoader::ResetLoader
  void *(__cdecl *priGetCurrent)(void);                   // current CProjectInstance
  void *(__cdecl *wldGetSoundSyncList)(void);             // CSoundSyncList of the current world
  void (__cdecl *wldSuspendSyncList)(void);               // suspend world sounds (pause)
  void (__thiscall *SoundList_Suspend)(void *list);       // CSoundSyncList::SuspendList (sounds only)
  void *(__cdecl *wldGetCurrent)(void);                   // current CWorld, 0 while no level is loaded
  u8 *fnIterateEntities;                                  // wldIterateEntities(CEntityIterator*)
  // Steam invitations (filled by EngineResolveInvites, 0 if unavailable)
  int *net_bSteamGameserver;                              // Engine: join target is a game server, not a lobby
  u32 *net_idSteamServerLobby;                            // Engine: CSteamID (low, high) of the lobby to join
  void (__fastcall *JoinServer)(void *project);           // Sam2Game: join that lobby (server list path)
  void (__cdecl *CloseMenus)(void);
  CStr *sam_strConnect;
  int *sam_bJoinAsPrivate, *sam_bJoinAsObserver;
  // Sam2Game (exported)
  int (__cdecl *samIsCoop)(void);
  ulong *(__cdecl *samGetGameInfoEntity)(ulong *ret);   // returns a Handle by hidden pointer
  u8 *fnCreatePlayer;                                     // CGameInfoEntity::CreatePlayer
  u8 *fnSpreadFrags;                                      // CGameInfoEntity::SpreadCountOfLeftFragsChanged
  int *sam_bSkipMovies;
  // Sam2Game (internal, by RVA)
  int (__cdecl *QueueLevelMovies)(const char *level, int which, float fade);
  void (__cdecl *QueueInputLock)(int on);
  void **projectVtbl;
  void *fontBig, *fontSmall;
  int *hud_bShowPausedMessage;
};
extern Engine E;

bool EngineResolve(void);              // false if the build is not the supported one
bool EngineResolveInvites(void);       // false if the game's join path does not look as expected
void *GetGameInfo(void);               // CGameInfoEntity* of the current world (or 0)
bool CmdQueueIdle(void *project);      // no movie / sequencer command pending
bool SequencerShowsOnlyMenus(void *project); // nothing but menu screens (no movie, no loading)
void CStrSet(CStr *s, const char *txt);// construct an engine CString
void CStrFree(CStr *s);                // destroy an engine CString
u8 *ServerSimStartCallSite(void);      // verified address of the simStart_t call (or 0)
