// Coop lobby for Serious Sam 2 (network coop only; single player and deathmatch are untouched).
//
// Flow (server authoritative), like Serious Sam HD / Serious Sam 3:
//   1. PRELOBBY : the session is created (advertised, joinable) but NO level is loaded. Players
//                 connect and wait. When the session is full (max players, host included) or the
//                 host presses F8, the server changes world: everybody loads the level together.
//                 With max players = 1 there is nobody to wait for: the level loads at once.
//   2. BARRIER  : level loaded, world paused, CreatePlayer refused (nobody enters the map) until
//                 every connected player has finished loading (clients report every 250 ms) and
//                 is not watching a movie (outro of the previous level). From the second level of
//                 a session on, the players themselves move to the new world with the level change
//                 (brain and puppet): they are in the map already, frozen by the pause.
//   3. INTRO    : everybody, host included, plays the level EPISODE/INTRO movies locally, exactly
//                 like single player does.
//   4. PLAYING  : world unpaused, CreatePlayer accepted: every player spawns.
//
// How the pieces are wired:
//   - the server start calls Engine!simStart_t from one call site (patched): the session starts
//     normally, then the resource loader is reset so nothing loads; later CSimulation::ChangeWorld
//     starts the real level through the normal level-change path (CNMPreWorldChange to clients);
//   - a client joining a server that has no world receives an empty world name; CSimulation::
//     ChangeWorld("") is skipped instead of failing to load "";
//   - once a world exists, the server talks to clients through RPCs of CGameInfoEntity:
//     client -> server : CreatePlayer(index, "\x01SL<flags><epoch>|<name>")
//     server -> clients: SpreadCountOfLeftFragsChanged(0xC0Bxxxxx) (unused in coop).
#include "engine.h"
#include "hook.h"
#include "invite.h"
#include "lang.h"
#include "util.h"

// Hook bodies live in non-inlined functions: clang catches SEH faults only inside calls.
#define NOINLINE __declspec(noinline)

// Phases. 1..3 are also sent to clients (2 bits); PRELOBBY only exists on the server.
enum { PH_NONE = 0, PH_BARRIER = 1, PH_INTRO = 2, PH_PLAYING = 3, PH_PRELOBBY = 4 };

#define MSG_MAGIC      0xC0Bu
#define MSG_STATE      0
#define MSG_MASK_A     1   // payload bit17: 0 = present mask, 1 = connected mask
#define MSG_READY      2
#define MSG_DONE       3
#define PREFIX         "\x01SL"
#define PREFIX_LEN     3
#define PRESENT_MS     2500
#define BROADCAST_MS   400
#define REQUEST_MS     250
#define BARRIER_DEBOUNCE_MS 1000
#define SILENCE_MS     6000   // client: no lobby message for that long = the server is playing
#define UNKNOWN_HOLD_MS 1500  // client: hold untagged requests while the server's first message comes
#define NPLAYERS       17

static inline bool IsMsg(u32 v) { return (v >> 20) == MSG_MAGIC; }
static inline u32 MakeMsg(u32 type, u32 payload) {
  return (MSG_MAGIC << 20) | ((type & 3) << 18) | (payload & 0x3ffff);
}
static int PopCount(u32 m) {
  int n = 0;
  while (m) { n += m & 1; m >>= 1; }
  return n;
}
// Milliseconds from 't' to 'now', 0 if 't' is later. 'now' is often read before a timestamp set
// in the same frame: a plain unsigned "now - t" then wraps to ~4e9 and fires every timeout at once.
static u32 Since(u32 now, u32 t) {
  i32 d = (i32)(now - t);
  return d > 0 ? (u32)d : 0;
}

// ---------------------------------------------------------------------------------------------
// Original functions (trampolines / previous vtable entries)
typedef void (__thiscall *CreatePlayer_t)(void *gi, i32 idx, char *name);
typedef void (__thiscall *SpreadFrags_t)(void *gi, i32 v);
typedef int  (__thiscall *Step_t)(void *prj);
typedef void (__thiscall *Render_t)(void *prj, void *dp, float f);
typedef int  (__thiscall *WorldHook_t)(void *prj, const char *world);
typedef void (__thiscall *ChangeWorld_t)(void *sim, const char *world);
typedef void (__thiscall *ProjVoid_t)(void *prj);
typedef void (__cdecl *IterateEntities_t)(void *iterator);
static IterateEntities_t T_IterateEntities;
static CreatePlayer_t T_CreatePlayer;
static SpreadFrags_t  T_SpreadFrags;
static Step_t         T_Step;
static Render_t       T_Render;
static WorldHook_t    T_WorldLoad;
static WorldHook_t    T_WorldStop;
static ChangeWorld_t  T_ChangeWorld;
static ProjVoid_t     P_SetCurrent;     // project vtbl[19]
static ProjVoid_t     P_UnsetCurrent;   // project vtbl[20]

// ---------------------------------------------------------------------------------------------
// Server side
struct Slot {
  u32  lastSeen;
  bool seen, modded, ready, introDone;
  char name[48];
};
static struct {
  int   phase;
  bool  waitingWorld;     // world change in progress: enter the barrier once it runs
  int   epoch;            // increments on every world, carried in all messages (4 bits)
  bool  firstLevel;
  void *sessionNi;
  char  pendingWorld[260];// level to load when the pre-lobby is complete
  u32   phaseSince, allReadySince, lastBroadcast;
  int   goBroadcasts;
  int   lastCount;
  bool  wePaused;
  bool  forceStart;
  Slot  pl[NPLAYERS];
} S;

// Local side (every machine; on a listen server it is the host's own player)
static struct {
  bool known;             // the server talks the lobby protocol in this session
  bool preLobby;          // client connected to a server that has not loaded the level yet
  int  state, epoch;
  bool dedicated;
  u32  presentMask, connMask, readyMask, doneMask;
  u32  lastMsg, lastReq;
  int  introEpoch;        // epoch whose movies were queued (-1 = none)
  bool introDone;
  bool wasNetCoop;        // previous world was a network coop game (for outros)
  bool pausedMsgHidden;
  int  savedPausedMsg;
  bool keyStartDown;
  bool soundFixDone;      // world sounds properly suspended for the current world
  u32  firstReqAt;        // first player request of this world while the server is unknown (0 = none)
  u32  lastBusy;          // last time a movie / sequencer command was running here
  char myName[48];        // our player name, as the game's player controller sends it
  char world[260];
  char prevWorld[260];
} L;

static void *g_project;       // CSamProjectInstance*
static void *g_localSession;  // network interface of the current session (session id)

static bool IsServerSide() { return E.netIsServer() && !E.netIsLocal(); }
static bool IsClientSide() { return E.netIsRemote() != 0; }
static int PrjInt(int off) { return g_project ? *(int *)((u8 *)g_project + off) : 0; }
static bool IsDedicated() { return PrjInt(PRJ_DEDICATED) != 0; }
static void *Sim() { return g_project ? *(void **)((u8 *)g_project + PRJ_SIM) : 0; }
static void *NetIf() { return g_project ? *(void **)((u8 *)g_project + PRJ_NETIF) : 0; }

static const char *PhaseName(int s) {
  static const char *n[] = {"NONE", "BARRIER", "INTRO", "PLAYING", "PRELOBBY"};
  return (s >= 0 && s <= 4) ? n[s] : "?";
}

// Remote clients by state of their server record: 3 = connected, 1..2 = handshake.
static u32 ServerClientMask(bool connectingToo) {
  u32 m = 0;
  void *ni = E.netGetCurrent();
  if (!ni || !E.netIsServer()) return 0;
  for (int i = 0; i < SRV_NCLIENTS && i < HOST_PLAYER_INDEX; i++) {
    int st = *(int *)((u8 *)ni + SRV_CLIENTS + i * SRV_CLIENT_SZ);
    if (st == 3 || (connectingToo && (st == 1 || st == 2))) m |= 1u << i;
  }
  return m;
}

// ---------------------------------------------------------------------------------------------
// Movies (same calls as CSamProjectInstance does in single player)
static void QueueIntroMovies(const char *world) {
  if (!g_cfg.playIntro || *E.sam_bSkipMovies || !world || !world[0]) return;
  E.QueueInputLock(1);
  int n = E.QueueLevelMovies(world, MOVIE_EPISODE, 2.0f);
  E.QueueLevelMovies(world, MOVIE_INTRO, n >= 1 ? -1.0f : 0.5f);
  E.QueueInputLock(0);
  Log("intro: movies queued for %s (episode movies: %d)", world, n);
}
static void QueueOutroMovie(const char *world) {
  if (!g_cfg.playOutro || *E.sam_bSkipMovies || !world || !world[0]) return;
  E.QueueInputLock(1);
  int n = E.QueueLevelMovies(world, MOVIE_OUTRO, 0.5f);
  E.QueueInputLock(0);
  Log("outro: %d movie(s) queued for %s", n, world);
}
static void StartLocalIntro(int epoch) {
  L.introEpoch = epoch;
  L.introDone = false;
  QueueIntroMovies(L.world);
}

// A new world's CSoundSyncList is created "suspended" (+8 = 1) and only becomes active at its first
// update by Render. The lobby pauses the world in the very frame it starts, before that update, so
// the engine's own wldSuspendSyncList() is a no-op and the ambient sounds started by OnSimStart keep
// playing their 1-second streaming buffer in a loop. Force a real suspension (same as a normal pause:
// sfxSoundTimer(list), sfxSuspend() releases the world's sources); they resume on unpause.
static void SuspendWorldSounds(const char *why) {
  u8 *list = (u8 *)E.wldGetSoundSyncList();
  if (!list) return;
  *(int *)(list + SSL_SUSPENDED) = 0;
  E.wldSuspendSyncList();
  L.soundFixDone = true;
  Log("world sounds suspended (%s)", why);
}

// A sound started after that suspension, while the world stays paused, streams its first second and
// is never refilled either: the same loop. On a client this happens a lot, since what the server's
// world did before its pause (level start triggers, music) arrives a few frames later. Release the
// world's sources again every frame while the lobby holds the world paused. sfxSuspend() only frees
// sources owned by the world's sound list (owner clock), movies and menus keep theirs.
static void KeepWorldSoundsSuspended() {
  u8 *list = (u8 *)E.wldGetSoundSyncList();
  if (!list) return;
  *(int *)(list + SSL_SUSPENDED) = 0;
  E.SoundList_Suspend(list);
}

// ---------------------------------------------------------------------------------------------
// Server -> clients (only possible once the world exists)
static void PlayerMasks(u32 &present, u32 &ready, u32 &done) {
  u32 now = GetTickCount();
  present = ready = done = 0;
  for (int i = 0; i < NPLAYERS; i++) {
    Slot &p = S.pl[i];
    if (p.seen && Since(now, p.lastSeen) < PRESENT_MS) {
      present |= 1u << i;
      if (p.ready) ready |= 1u << i;
      if (p.introDone) done |= 1u << i;
    }
  }
}

static void Broadcast() {
  void *gi = GetGameInfo();
  if (!gi) return;
  u32 present, ready, done;
  PlayerMasks(present, ready, done);
  u32 conn = ServerClientMask(false) | present;
  int ph = S.phase >= PH_BARRIER && S.phase <= PH_PLAYING ? S.phase : PH_BARRIER;
  u32 st = (ph & 3) | ((S.epoch & 15) << 2) | (IsDedicated() ? 1 << 7 : 0);
  T_SpreadFrags(gi, (i32)MakeMsg(MSG_STATE, st));
  T_SpreadFrags(gi, (i32)MakeMsg(MSG_MASK_A, present));
  T_SpreadFrags(gi, (i32)MakeMsg(MSG_MASK_A, conn | (1u << 17)));
  T_SpreadFrags(gi, (i32)MakeMsg(MSG_READY, ready));
  T_SpreadFrags(gi, (i32)MakeMsg(MSG_DONE, done));
  S.lastBroadcast = GetTickCount();
}

static void ServerEnter(int ph) {
  Log("server: %s -> %s (epoch %d)", PhaseName(S.phase), PhaseName(ph), S.epoch);
  S.phase = ph;
  S.phaseSince = GetTickCount();
  S.allReadySince = 0;
  S.forceStart = false;
  if (ph == PH_INTRO && !IsDedicated()) StartLocalIntro(S.epoch);
  if (ph == PH_PLAYING) {
    if (S.wePaused && Sim()) E.Sim_RequestUnPause(Sim());
    S.wePaused = false;
    S.goBroadcasts = 5;
  }
  if (ph != PH_PRELOBBY) Broadcast();
}

// Called right after the server started its session (patched call to Engine!simStart_t).
static NOINLINE void AfterServerSimStart(const char *world) {
  void *prj = E.priGetCurrent();
  if (!prj) return;
  g_project = prj;
  int gameType = PrjInt(PRJ_GAMETYPE), maxPlayers = PrjInt(PRJ_MAXPLAYERS);
  bool ded = IsDedicated();
  Log("server started: %s (game type %d, max players %d%s)", world ? world : "", gameType,
      maxPlayers, ded ? ", dedicated" : "");
  if (!g_cfg.enabled || !g_cfg.preLobby || gameType != 2 || !world || !world[0]) return;
  int alreadyIn = ded ? 0 : 1;   // the host occupies one slot of a listen server
  if (maxPlayers <= alreadyIn) {
    Log("server: session full with the host alone, loading the level now");
    return;
  }
  void *sim = Sim();
  if (!sim) return;
  E.Loader_Reset(*(void **)((u8 *)sim + SIM_LOADER));   // the level is NOT loaded yet
  StrCopy(S.pendingWorld, world, sizeof(S.pendingWorld));
  StrCopy(L.world, world, sizeof(L.world));
  S.waitingWorld = false;
  S.lastCount = -1;
  S.phase = PH_NONE;
  ServerEnter(PH_PRELOBBY);
}

static void __cdecl Stub_ServerSimStart(const char *world) {
  E.simStart_t(world);   // may throw a C++ exception: let the game handle it as usual
  __try {
    AfterServerSimStart(world);
  } __except (1) {
    Log("EXCEPTION after server start");
  }
}

static void ServerWorldLoadBegin() {
  void *ni = NetIf();
  bool newSession = ni != S.sessionNi;
  S.sessionNi = ni;
  S.epoch = (S.epoch + 1) & 15;
  S.firstLevel = newSession;
  S.waitingWorld = true;
  S.phase = PH_NONE;
  S.wePaused = false;
  for (int i = 0; i < NPLAYERS; i++) {
    if (newSession) S.pl[i].name[0] = 0;
    S.pl[i].seen = S.pl[i].ready = S.pl[i].introDone = false;
  }
  Log("server: world load begins (epoch %d, %s)", S.epoch,
      newSession ? "new session" : "level change");
}

static NOINLINE void ServerTick() {
  u32 now = GetTickCount();
  void *sim = Sim();

  if (S.phase == PH_PRELOBBY) {
    int count = PopCount(ServerClientMask(false)) + (IsDedicated() ? 0 : 1);
    int maxPlayers = PrjInt(PRJ_MAXPLAYERS);
    if (count != S.lastCount) {
      Log("server: lobby %d / %d players", count, maxPlayers);
      S.lastCount = count;
    }
    bool timeout = g_cfg.lobbyTimeout > 0 && Since(now, S.phaseSince) > (u32)g_cfg.lobbyTimeout * 1000;
    bool full = count >= maxPlayers && count > 0;
    if (full || S.forceStart || timeout) {
      Log("server: lobby complete (%d / %d%s%s), loading %s", count, maxPlayers,
          S.forceStart ? ", started by the host" : "", timeout ? ", timeout" : "", S.pendingWorld);
      S.phase = PH_NONE;
      S.forceStart = false;
      if (sim) T_ChangeWorld(sim, S.pendingWorld);   // normal level change: clients load too
    }
    return;
  }

  if (S.waitingWorld) {
    if (!sim || !E.Sim_IsStarted(sim) || !GetGameInfo()) return;
    S.waitingWorld = false;
    if (!g_cfg.enabled || !E.samIsCoop() || E.netIsDemoPlayback()) {
      Log("server: not a network coop game, lobby disabled for this world");
      S.phase = PH_NONE;
      return;
    }
    if (!S.firstLevel && !g_cfg.waitOnLevelChange) {
      S.phase = PH_NONE;
      return;
    }
    if (g_cfg.pauseDuringLobby && !E.Sim_IsPaused(sim)) {
      E.Sim_RequestPause(sim);
      S.wePaused = true;
    }
    if (E.Sim_IsPaused(sim)) SuspendWorldSounds("server pause");
    ServerEnter(PH_BARRIER);
  }
  if (S.phase == PH_NONE) return;

  // The host's own player (index 16) is refreshed from local state every frame. The host is local:
  // it has loaded as soon as the server's world runs, even while a movie stops its player requests.
  if (!IsDedicated()) {
    Slot &h = S.pl[HOST_PLAYER_INDEX];
    h.seen = true;
    h.lastSeen = now;
    h.modded = true;
    h.ready = CmdQueueIdle(g_project);
    h.introDone = L.introEpoch == S.epoch && L.introDone;
  }
  u32 conn = ServerClientMask(false);
  for (int i = 0; i < HOST_PLAYER_INDEX; i++)   // a free slot may be reused by somebody else
    if (!(conn & (1u << i))) S.pl[i].modded = false;
  u32 present, ready, done;
  PlayerMasks(present, ready, done);
  bool everybodyIn = (conn & ~present) == 0;   // nobody still loading (or watching a movie)

  if (S.phase == PH_BARRIER) {
    bool cond = everybodyIn && present && (present & ~ready) == 0;
    if (cond) {
      if (!S.allReadySince) S.allReadySince = now;
    } else {
      S.allReadySince = 0;
    }
    bool timeout = g_cfg.loadTimeout > 0 && Since(now, S.phaseSince) > (u32)g_cfg.loadTimeout * 1000;
    if (S.forceStart || (cond && Since(now, S.allReadySince) >= BARRIER_DEBOUNCE_MS) || timeout) {
      if (timeout) Log("server: load timeout, starting without the missing players");
      ServerEnter(PH_INTRO);
    }
  } else if (S.phase == PH_INTRO) {
    // Players stop asking for their player while a movie plays: absent then means "still in the
    // movie" (or still loading), never "done".
    bool allDone = everybodyIn && (present & ~done) == 0;
    bool timeout = g_cfg.introTimeout > 0 && Since(now, S.phaseSince) > (u32)g_cfg.introTimeout * 1000;
    if (S.forceStart || allDone || timeout) {
      if (timeout) Log("server: intro timeout");
      ServerEnter(PH_PLAYING);
    }
  }

  if (S.phase == PH_BARRIER || S.phase == PH_INTRO) {
    if (Since(now, S.lastBroadcast) >= BROADCAST_MS) Broadcast();
  } else if (S.phase == PH_PLAYING && S.goBroadcasts > 0 && Since(now, S.lastBroadcast) >= BROADCAST_MS) {
    // Repeat "go" a few times. On the host the broadcast also runs the frags-counter code for its
    // own player (which exists during the whole lobby from the second level on): it only shows a
    // count from 1 to 5, and our values are all negative (bit 31 set), so it shows nothing.
    S.goBroadcasts--;
    Broadcast();
  }
}

// ---------------------------------------------------------------------------------------------
// Clients: messages from the server
static NOINLINE void ClientOnMsg(u32 v) {
  u32 type = (v >> 18) & 3, p = v & 0x3ffff;
  L.lastMsg = GetTickCount();
  if (!L.known) Log("client: the server runs the lobby");
  L.known = true;
  L.preLobby = false;
  switch (type) {
    case MSG_STATE: {
      int st = p & 3, ep = (p >> 2) & 15;
      if (st != L.state || ep != L.epoch) Log("client: %s (epoch %d)", PhaseName(st), ep);
      L.state = st;
      L.epoch = ep;
      L.dedicated = (p >> 7) & 1;
      break;
    }
    case MSG_MASK_A:
      if (p & (1u << 17)) L.connMask = p & 0x1ffff;
      else L.presentMask = p & 0x1ffff;
      break;
    case MSG_READY: L.readyMask = p & 0x1ffff; break;
    case MSG_DONE:  L.doneMask = p & 0x1ffff; break;
  }
}

// ---------------------------------------------------------------------------------------------
// Local tick (every machine in a network game)
static bool KeyPressed(int vk, bool &wasDown) {
  bool down = vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
  bool pressed = down && !wasDown;
  wasDown = down;
  return pressed && E.sysIsAppForeground();
}

static int CurPhase() {
  if (IsServerSide()) return S.phase;
  if (IsClientSide()) {
    if (L.preLobby) return PH_PRELOBBY;
    if (L.known) return L.state;
  }
  return PH_NONE;
}

// Client request header: "\x01SL" + flags('A'+bits) + epoch('a'+e) + '|' + name.
// Flags: 1 = loaded and not watching a movie, 2 = intro of this epoch finished.
static void ClientTag(CStr *out, const char *name) {
  int flags = 0;
  if (CmdQueueIdle(g_project)) flags |= 1;
  if (L.state == PH_INTRO && L.introEpoch == L.epoch && L.introDone) flags |= 2;
  char buf[96];
  buf[0] = PREFIX[0]; buf[1] = PREFIX[1]; buf[2] = PREFIX[2];
  buf[3] = (char)('A' + flags);
  buf[4] = (char)('a' + (L.epoch & 15));
  buf[5] = '|';
  StrCopy(buf + 6, name ? name : "", sizeof(buf) - 6);
  CStrSet(out, buf);
}

// Client: report our state to the server ourselves, through the same CreatePlayer RPC. The game's
// player controller only asks while it has no player: from the second level on, the players come
// along with the level change, so it asks once at most and the server would never learn that we
// finished loading or watching the intro.
static void ClientSendStatus(u32 now) {
  void *gi = GetGameInfo();
  int idx = E.cliGetClientIndex();
  if (!gi || idx < 0 || idx >= HOST_PLAYER_INDEX) return;
  L.lastReq = now;
  CStr s;
  ClientTag(&s, L.myName);
  T_CreatePlayer(gi, idx, s.p);   // the original sends the RPC and destroys the string
}

static NOINLINE void LocalTick() {
  bool server = IsServerSide();
  int ph = CurPhase();
  int epoch = server ? S.epoch : L.epoch;
  u32 now = GetTickCount();
  if (!CmdQueueIdle(g_project)) L.lastBusy = now;

  // Remember whether this world is a network coop game (decides outros on the next change).
  if (E.Sim_IsStarted(Sim()) && GetGameInfo())
    L.wasNetCoop = !E.netIsLocal() && E.samIsCoop() && g_cfg.enabled;

  // Clients receive the server pause before or right when their world starts: same sound fix.
  // Then, as long as the lobby keeps the world paused, sounds started since are released too.
  bool paused = E.Sim_IsPaused(Sim()) && GetGameInfo();
  if (paused && !L.soundFixDone && (server ? (S.phase == PH_BARRIER || S.phase == PH_INTRO) : true))
    SuspendWorldSounds(server ? "paused" : "client paused by the server");
  else if (paused && L.soundFixDone && (ph == PH_BARRIER || ph == PH_INTRO))
    KeepWorldSoundsSuspended();

  // The server repeats its state every 400 ms during the lobby, also while it plays its movies: a
  // long silence means it is playing and its "go" messages got lost. (Whether our own player exists
  // tells nothing: from the second level on it exists before the lobby even starts.) Our own movies
  // may hold the messages back, so the silence only counts from the end of our last movie.
  if (!server && L.known && (ph == PH_BARRIER || ph == PH_INTRO)) {
    u32 last = (i32)(L.lastBusy - L.lastMsg) > 0 ? L.lastBusy : L.lastMsg;
    if (Since(now, last) > SILENCE_MS) {
      Log("client: no message from the server for %d s, leaving the lobby", SILENCE_MS / 1000);
      L.state = ph = PH_PLAYING;
    }
  }
  // Clients start their movies when the server says so (the host does it in ServerEnter).
  if (!server && ph == PH_INTRO && L.introEpoch != epoch) StartLocalIntro(epoch);
  if (L.introEpoch >= 0 && L.introEpoch == epoch && !L.introDone && CmdQueueIdle(g_project)) {
    L.introDone = true;
    Log("intro finished locally (epoch %d)", epoch);
  }
  if (!server && L.known && (ph == PH_BARRIER || ph == PH_INTRO) && Since(now, L.lastReq) >= REQUEST_MS)
    ClientSendStatus(now);

  // Host: start now without waiting for the remaining players / movies.
  bool ks = KeyPressed(g_cfg.keyStart, L.keyStartDown);
  if (server && !IsDedicated() && ks && (ph == PH_PRELOBBY || ph == PH_BARRIER || ph == PH_INTRO)) {
    Log("host forces the start (%s)", PhaseName(ph));
    S.forceStart = true;
  }

  // Hide the engine "PAUSED" text while our overlay explains what is going on.
  bool hide = ph == PH_BARRIER || ph == PH_INTRO;
  if (hide && !L.pausedMsgHidden) {
    L.savedPausedMsg = *E.hud_bShowPausedMessage;
    *E.hud_bShowPausedMessage = 0;
    L.pausedMsgHidden = true;
  } else if (!hide && L.pausedMsgHidden) {
    *E.hud_bShowPausedMessage = L.savedPausedMsg;
    L.pausedMsgHidden = false;
  }
}

// ---------------------------------------------------------------------------------------------
// Hooks
// Returns false when the request says nothing yet about the current level.
static bool ParseRequest(Slot &p, const char *name, int epoch) {
  // "\x01SL" + flags('A'+bits) + epoch('a'+e) + '|' + name
  if (name && StrNCmp(name, PREFIX, PREFIX_LEN) == 0 && name[PREFIX_LEN] && name[PREFIX_LEN + 1] &&
      name[PREFIX_LEN + 2] == '|') {
    int f = name[PREFIX_LEN] - 'A';
    int e = name[PREFIX_LEN + 1] - 'a';
    // Flags only count for the current level. A client keeps the previous epoch until our first
    // message of the new level reaches it, and requests it sent during the level change are only
    // executed once the server has loaded: their "ready" is about the previous level.
    bool current = e == (epoch & 15);
    p.modded = true;
    p.ready = (f & 1) != 0 && current;
    p.introDone = (f & 2) != 0 && current;
    const char *real = name + PREFIX_LEN + 3;
    if (*real) StrCopy(p.name, real, sizeof(p.name));   // status reports may not know the name
    return current;
  }
  if (p.modded) {
    // Untagged request from a client already seen with the mod in this session (older versions of
    // the mod did not tag requests sent in game): sent before the level change, not ready yet.
    p.ready = p.introDone = false;
    return false;
  }
  // Unmodded client: never blocks the lobby, it simply waits for the start.
  p.ready = p.introDone = true;
  StrCopy(p.name, name ? name : "", sizeof(p.name));
  return true;
}

static const char *StripPrefix(const char *name) {
  if (name && StrNCmp(name, PREFIX, PREFIX_LEN) == 0) {
    for (const char *q = name; *q; q++)
      if (*q == '|') return q + 1;
  }
  return 0;
}

// Returns true when the call must be swallowed. May replace *name (frees the old string).
static NOINLINE bool OnCreatePlayer(i32 idx, CStr *name) {
  if (!g_cfg.enabled || E.netIsLocal()) return false;
  u32 now = GetTickCount();
  if (!IsClientSide()) {
    // Executing on the server: a player wants to enter the map.
    if (idx < 0 || idx >= NPLAYERS) return false;
    if (S.waitingWorld || S.phase == PH_BARRIER || S.phase == PH_INTRO) {
      Slot &p = S.pl[idx];
      bool wasPresent = p.seen && Since(now, p.lastSeen) < PRESENT_MS;
      bool current = true;
      if (idx == HOST_PLAYER_INDEX && !IsDedicated()) {
        const char *real = StripPrefix(name->p);
        StrCopy(p.name, real ? real : name->p, sizeof(p.name));
        p.modded = true;   // flags are refreshed from local state in ServerTick()
      } else {
        current = ParseRequest(p, name->p, S.epoch);
      }
      p.seen = true;
      p.lastSeen = now;
      if (!wasPresent)
        Log("server: player %d (%s) %s", idx, p.name,
            !p.modded ? "has loaded and waits [no mod]"
                      : current ? "has loaded and waits" : "is still changing level");
      return true;   // refuse for now: the player asks again next frame
    }
    const char *real = StripPrefix(name->p);
    if (real) {   // prefixed request after the start: strip the header
      CStr clean;
      CStrSet(&clean, real);
      CStrFree(name);
      *name = clean;
    }
    return false;
  }
  // Executing on a client: this sends the RPC to the server. Once the server is known to run the
  // lobby, every request carries our header, in game too (the server strips it then): requests sent
  // during a level change are executed by the server once it has loaded the next level, and an
  // untagged one would make it take us for a client without the mod.
  if (name->p && StrNCmp(name->p, PREFIX, PREFIX_LEN) != 0)
    StrCopy(L.myName, name->p, sizeof(L.myName));   // reused by our own status reports
  if (!L.known) {
    // The server's first message can arrive a few frames after our world started: hold the
    // requests a moment rather than look like a client without the mod. (A server without the mod
    // only sees us spawn a little later.)
    if (!L.firstReqAt) L.firstReqAt = now ? now : 1;
    return Since(now, L.firstReqAt) < UNKNOWN_HOLD_MS;
  }
  bool waiting = L.state != PH_PLAYING;
  if (waiting && Since(now, L.lastReq) < REQUEST_MS) return true;   // throttle while waiting
  L.lastReq = now;
  CStr tagged;
  ClientTag(&tagged, name->p);
  CStrFree(name);
  *name = tagged;
  return false;
}

static void __thiscall H_CreatePlayer(void *gi, i32 idx, char *rawName) {
  CStr name;
  name.p = rawName;   // by-value CString: we own it now
  bool swallow = false;
  __try {
    swallow = OnCreatePlayer(idx, &name);
  } __except (1) {
    Log("EXCEPTION in CreatePlayer hook");
    swallow = false;
  }
  if (swallow) {
    CStrFree(&name);
    return;
  }
  T_CreatePlayer(gi, idx, name.p);   // the original destroys the string
}

static NOINLINE void MsgImpl(u32 v) {
  if (!E.netIsHost()) ClientOnMsg(v);
}
static void __thiscall H_SpreadFrags(void *gi, i32 v) {
  if (IsMsg((u32)v)) {
    __try {
      MsgImpl((u32)v);
    } __except (1) {
      Log("EXCEPTION in message hook");
    }
    return;
  }
  T_SpreadFrags(gi, v);
}

// When a player joins the lobby (no level loaded yet), the server sends it the entities of the
// current level: CServerInterface::ConnectClient calls SendRemapList and builds the initial entity
// states, both through wldIterateEntities, which uses the current world without checking it: crash.
// With no world there is nothing to iterate. The engine sends both lists again to every connected
// client once the level is loaded (CServerInterface::PostWorldChange), as for a client that joins
// during a level load. A client in the lobby goes through the same function when the level starts.
static bool g_noWorldIterLogged;
static void __cdecl H_IterateEntities(void *iterator) {
  if (!E.wldGetCurrent()) {
    if (!g_noWorldIterLogged) {
      g_noWorldIterLogged = true;
      Log("no level loaded yet: entity iteration skipped (player joining the lobby)");
    }
    return;
  }
  T_IterateEntities(iterator);
}

// A client that joins a server which has not loaded its level yet receives an empty world name.
// Loading "" would fail and disconnect: skip it and wait for the server's CNMPreWorldChange.
static NOINLINE bool OnChangeWorld(const char *world) {
  if (!g_cfg.enabled || (world && world[0])) {
    L.preLobby = false;
    return false;
  }
  void *prj = E.priGetCurrent();
  if (prj) {
    g_project = prj;
    void *ni = NetIf();
    if (ni != g_localSession) {   // new connection: forget the previous session
      g_localSession = ni;
      L.known = false;
      L.epoch = -1;
      L.state = PH_NONE;
      L.firstReqAt = 0;
    }
  }
  if (!L.preLobby) Log("client: connected to a lobby, waiting for the host to load the level");
  L.preLobby = true;
  return true;
}
static void __thiscall H_ChangeWorld(void *sim, const char *world) {
  bool skip = false;
  __try {
    skip = OnChangeWorld(world);
  } __except (1) {
    Log("EXCEPTION in ChangeWorld hook");
    skip = false;
  }
  if (!skip) T_ChangeWorld(sim, world);
}

static NOINLINE void StepImpl(void *prj) {
  g_project = prj;
  if (!NetIf() && (g_localSession || S.sessionNi)) {
    // Back in the menus: whatever comes next is a new session (the next network interface may get
    // the same address from the allocator, so comparing pointers alone would not tell).
    g_localSession = 0;
    S.sessionNi = 0;
    L.known = false;
    L.epoch = -1;
    L.preLobby = false;
    L.firstReqAt = 0;
  }
  if (!Sim()) return;
  P_SetCurrent(prj);   // the original Step ends with netSetCurrent(0): restore the context
  if (!E.netIsLocal() && E.netGetCurrent()) {
    if (IsServerSide()) ServerTick();
    LocalTick();
  }
  P_UnsetCurrent(prj);
}

// A Steam invitation was accepted while the game runs: join like the game's server list does
// (FUN_101cb1b0): lobby id, not a game server, neither private nor observer, then its join routine
// and closing the menus. Outside of the SEH guard on purpose: the join may throw a C++ exception,
// which must reach the game's own handlers as when joining from its server list.
static void JoinInvitedLobby(void *prj, u32 lo, u32 hi) {
  P_SetCurrent(prj);   // same context as the menu code that normally calls the join
  if (E.netIsRemote() && E.net_idSteamServerLobby[0] == lo && E.net_idSteamServerLobby[1] == hi) {
    Log("Steam: already in the game of this invitation");
    P_UnsetCurrent(prj);
    return;
  }
  Log("Steam: joining the game of the invitation (lobby %x%08x)", hi, lo);
  *E.net_bSteamGameserver = 0;
  E.net_idSteamServerLobby[0] = lo;
  E.net_idSteamServerLobby[1] = hi;
  *E.sam_bJoinAsPrivate = 0;
  *E.sam_bJoinAsObserver = 0;
  E.JoinServer(prj);
  E.CloseMenus();
  P_UnsetCurrent(prj);
}

// An invitation waits for the end of a movie (boot movies, cinematics) or of a loading screen.
// Menu screens are commands of the same sequencer as movies (the main menu is one): the menus are
// precisely where the queue is never empty, so it must not simply wait for an empty queue.
// Then it waits until Steam knows who owns the lobby, since the game connects to that owner.
static NOINLINE bool InviteReady(void *prj, u32 *lo, u32 *hi) {
  return E.JoinServer && SequencerShowsOnlyMenus(prj) && InviteReadyToJoin(lo, hi);
}

static int __thiscall H_Step(void *prj) {
  int r = T_Step(prj);
  if (r) {
    bool join = false;
    u32 lo = 0, hi = 0;
    __try {
      StepImpl(prj);
      join = InviteReady(prj, &lo, &hi);
    } __except (1) {
      Log("EXCEPTION in Step hook");
    }
    if (join) JoinInvitedLobby(prj, lo, hi);
  }
  return r;
}

static NOINLINE void WorldLoadImpl(void *prj, const char *world) {
  g_project = prj;
  if (E.netIsLocal()) return;
  // Outro of the level we just left (single player plays it from the same callback).
  if (L.wasNetCoop && L.prevWorld[0] && world && StrCmp(L.prevWorld, world) != 0)
    QueueOutroMovie(L.prevWorld);
  L.prevWorld[0] = 0;
  StrCopy(L.world, world, sizeof(L.world));
  // Fresh lobby state for the new world; the server tells us the rest.
  L.preLobby = false;
  L.state = PH_NONE;
  L.introEpoch = -1;
  L.introDone = false;
  L.soundFixDone = false;
  L.firstReqAt = 0;
  g_noWorldIterLogged = false;
  L.presentMask = L.connMask = L.readyMask = L.doneMask = 0;
  void *ni = NetIf();
  if (ni != g_localSession) {   // new connection / new server: forget everything
    g_localSession = ni;
    L.known = false;
    L.epoch = -1;
  }
  if (IsServerSide()) ServerWorldLoadBegin();
  Log("world load begins: %s", world ? world : "(null)");
}
static int __thiscall H_WorldLoad(void *prj, const char *world) {
  __try {
    WorldLoadImpl(prj, world);
  } __except (1) {
    Log("EXCEPTION in world-load hook");
  }
  return T_WorldLoad(prj, world);
}

static NOINLINE void WorldStopImpl(void *prj, const char *oldWorld) {
  g_project = prj;
  if (!E.netIsLocal()) StrCopy(L.prevWorld, oldWorld, sizeof(L.prevWorld));
}
static int __thiscall H_WorldStop(void *prj, const char *oldWorld) {
  __try {
    WorldStopImpl(prj, oldWorld);
  } __except (1) {
    Log("EXCEPTION in world-stop hook");
  }
  return T_WorldStop(prj, oldWorld);
}

// ---------------------------------------------------------------------------------------------
// Overlay
static void VkName(int vk, char *out) {
  if (vk >= 0x70 && vk <= 0x87) wsprintfA(out, "F%d", vk - 0x6f);
  else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) wsprintfA(out, "%c", vk);
  else if (vk == 0x0d) wsprintfA(out, "%s", Tr(MSG_KEY_ENTER));
  else if (vk == 0x20) wsprintfA(out, "%s", Tr(MSG_KEY_SPACE));
  else wsprintfA(out, "VK%d", vk);
}

static float g_y, g_x0, g_lineH;
static bool g_dpPushed;   // a CCurrentDrawPort is pushed (pop it if drawing faults)
static void Text(const char *s, float x, u32 color) {
  float pos[3] = {x, g_y, 0.0f};
  E.gfuPutColorText3f(s, pos, color);
}
static void Line(const char *s, u32 color) {
  Text(s, g_x0, color);
  g_y += g_lineH;
}

static NOINLINE void DrawOverlay(void *dp) {
  int ph = CurPhase();
  if (ph != PH_PRELOBBY && ph != PH_BARRIER && ph != PH_INTRO) return;
  if (!CmdQueueIdle(g_project)) return;   // a movie is on screen
  bool server = IsServerSide();
  bool dedicated = server ? IsDedicated() : L.dedicated;
  u32 now = GetTickCount();

  // View model: who is connected / loaded / ready / done.
  u32 present = 0, conn = 0, ready = 0, done = 0, connecting = 0;
  if (server) {
    PlayerMasks(present, ready, done);
    conn = ServerClientMask(false) | present;
    connecting = ServerClientMask(true) & ~conn;
    if (!dedicated) conn |= 1u << HOST_PLAYER_INDEX;
  } else if (ph != PH_PRELOBBY) {
    present = L.presentMask; conn = L.connMask | present; ready = L.readyMask; done = L.doneMask;
  }
  int me = server ? (dedicated ? -1 : HOST_PLAYER_INDEX) : E.cliGetClientIndex();
  int maxPlayers = server ? PrjInt(PRJ_MAXPLAYERS) : 0;

  i32 w = *(i32 *)((u8 *)dp + DP_X1) - *(i32 *)((u8 *)dp + DP_X0);
  i32 h = *(i32 *)((u8 *)dp + DP_Y1) - *(i32 *)((u8 *)dp + DP_Y0);
  if (w <= 0 || h <= 0) return;

  char cdp[64];
  E.CCurrentDrawPort_ctor(cdp, dp, 1);
  g_dpPushed = true;
  E.gfuOrtho();

  E.gfuSetCurrentFont(E.fontSmall);
  g_lineH = E.gfuGetStringHeight("Ay", E.fontSmall) * 1.25f;
  if (g_lineH < 8.0f) g_lineH = 16.0f;
  int rows = PopCount(conn | connecting);
  float pw = w * 0.62f, pheight = g_lineH * (10 + rows) + 24.0f;
  float px = (w - pw) * 0.5f, py = h * 0.12f;
  float box[6] = {px, py, 0, px + pw, py + pheight, 0};
  E.gfuGradient3f(box, 0xD0000000, 0xD0000000, 0xD0101830, 0xD0101830);

  // Title
  E.gfuSetCurrentFont(E.fontBig);
  const char *title = ph == PH_PRELOBBY ? Tr(MSG_TITLE_LOBBY)
                    : ph == PH_BARRIER  ? Tr(MSG_TITLE_LOADING)
                                        : Tr(MSG_TITLE_CINEMATIC);
  float tw = E.gfuGetStringWidth(title, E.fontBig);
  float th = E.gfuGetStringHeight(title, E.fontBig);
  g_y = py + 10.0f;
  Text(title, px + (pw - tw) * 0.5f, 0xFFFFD040);
  g_y += (th > g_lineH ? th : g_lineH) + 6.0f;

  E.gfuSetCurrentFont(E.fontSmall);
  g_x0 = px + 24.0f;
  char buf[160], lvl[96];
  StrFileBase(lvl, L.world, sizeof(lvl));
  if (lvl[0]) {
    wsprintfA(buf, "%s %s", Tr(MSG_LEVEL), lvl);
    Line(buf, 0xFFC0C0C0);
  }

  if (ph == PH_PRELOBBY && !server) {
    Line(Tr(MSG_CONNECTED), 0xFFFFFFFF);
    Line(Tr(MSG_WAITING), 0xFFA0A0A0);
    g_y += g_lineH * 0.5f;
    Line(Tr(MSG_LOADS_WHEN_FULL), 0xFFA0A0A0);
    g_dpPushed = false;
    E.gfxPopCurrentDrawPort();
    return;
  }

  int nConn = PopCount(conn), nPresent = PopCount(present);
  int nReady = PopCount(present & ready), nDone = PopCount(present & done);
  if (ph == PH_PRELOBBY) {
    wsprintfA(buf, Tr(MSG_PLAYERS_FMT), nConn, maxPlayers);
  } else if (ph == PH_BARRIER) {
    wsprintfA(buf, Tr(MSG_READY_FMT), nReady, nConn);
  } else {
    wsprintfA(buf, Tr(MSG_FINISHED_FMT), nDone,
              nPresent);
  }
  Line(buf, 0xFFFFFFFF);
  g_y += g_lineH * 0.5f;

  // Player list
  for (int i = 0; i < NPLAYERS; i++) {
    u32 bit = 1u << i;
    if (!((conn | connecting) & bit)) continue;
    char who[80];
    if (server && S.pl[i].name[0])
      wsprintfA(who, "%s", S.pl[i].name);
    else if (i == HOST_PLAYER_INDEX)
      wsprintfA(who, "%s", Tr(MSG_HOST));
    else
      wsprintfA(who, Tr(MSG_PLAYER_FMT), i + 1);
    if (i == me) StrCat(who, Tr(MSG_YOU_SUFFIX), sizeof(who));
    if (i == HOST_PLAYER_INDEX && i != me) StrCat(who, Tr(MSG_HOST_SUFFIX), sizeof(who));
    Text(who, g_x0, i == me ? 0xFFFFFF80 : 0xFFFFFFFF);
    const char *stt;
    u32 col;
    if (connecting & bit) { stt = Tr(MSG_ST_CONNECTING); col = 0xFFA0A0A0; }
    else if (ph == PH_PRELOBBY) { stt = Tr(MSG_ST_IN_LOBBY); col = 0xFF40FF40; }
    else if (!(present & bit)) { stt = Tr(MSG_ST_LOADING); col = 0xFFFFA040; }
    else if (ph == PH_INTRO) {
      if (done & bit) { stt = Tr(MSG_ST_DONE); col = 0xFF40FF40; }
      else { stt = Tr(MSG_ST_WATCHING); col = 0xFFA0A0A0; }
    } else if (ready & bit) { stt = Tr(MSG_ST_READY); col = 0xFF40FF40; }
    else { stt = Tr(MSG_ST_MOVIE); col = 0xFFFFA040; }
    Text(stt, px + pw * 0.62f, col);
    g_y += g_lineH;
  }
  g_y += g_lineH * 0.5f;

  // Footer
  char kStart[16];
  VkName(g_cfg.keyStart, kStart);
  if (ph == PH_PRELOBBY) {
    Line(Tr(MSG_LOADS_WHEN_FULL), 0xFFA0A0A0);
    if (g_cfg.lobbyTimeout > 0) {
      int left = g_cfg.lobbyTimeout - (int)((Since(now, S.phaseSince)) / 1000);
      if (left < 0) left = 0;
      wsprintfA(buf, Tr(MSG_AUTOSTART_FMT), left);
      Line(buf, 0xFFA0A0A0);
    }
  } else {
    Line(Tr(MSG_WAITING), 0xFFA0A0A0);
  }
  if (server && !dedicated) {
    wsprintfA(buf, "[%s] %s", kStart,
              ph == PH_PRELOBBY ? Tr(MSG_START_NOW)
                                : Tr(MSG_STOP_WAITING));
    Line(buf, 0xFF80C0FF);
  }

  g_dpPushed = false;
  E.gfxPopCurrentDrawPort();
}

static NOINLINE void PopDrawPortSafe() { E.gfxPopCurrentDrawPort(); }
static NOINLINE void RenderImpl(void *prj, void *dp) {
  g_project = prj;
  if (!Sim()) return;
  P_SetCurrent(prj);   // the original Render ends with netSetCurrent(0): restore the context
  if (!E.netIsLocal() && E.netGetCurrent()) DrawOverlay(dp);
  P_UnsetCurrent(prj);
}
static void __thiscall H_Render(void *prj, void *dp, float f) {
  T_Render(prj, dp, f);
  if (!g_cfg.overlay || !dp) return;
  __try {
    RenderImpl(prj, dp);
  } __except (1) {
    Log("EXCEPTION in overlay; overlay disabled");
    g_cfg.overlay = 0;
    if (g_dpPushed) {
      g_dpPushed = false;
      PopDrawPortSafe();
    }
  }
}

// ---------------------------------------------------------------------------------------------
bool LobbyInstall(void) {
  memset(&S, 0, sizeof(S));
  memset(&L, 0, sizeof(L));
  L.introEpoch = -1;
  L.epoch = -1;
  u8 *b = E.gameBase;
  void **vt = E.projectVtbl;
  P_SetCurrent = (ProjVoid_t)vt[PRJ_SLOT_SETCURRENT];
  P_UnsetCurrent = (ProjVoid_t)vt[PRJ_SLOT_UNSETCURRENT];
  if ((u32)P_SetCurrent - (u32)b != 0x1fa40 || (u32)P_UnsetCurrent - (u32)b != 0x1fa80) {
    Log("ERROR: unexpected project vtable slots 19/20");
    return false;
  }
  u8 *call = ServerSimStartCallSite();
  if (!call) {
    Log("ERROR: server start call site not recognized");
    return false;
  }
  // push ebp; mov ebp,esp; push esi; push edi
  static const u8 changeWorldPro[5] = {0x55, 0x8b, 0xec, 0x56, 0x57};
  // Detours first (they can fail without side effects on the vtable).
  T_CreatePlayer = (CreatePlayer_t)DetourSehPrologue(E.fnCreatePlayer, (void *)H_CreatePlayer);
  T_SpreadFrags = (SpreadFrags_t)DetourSehPrologue(E.fnSpreadFrags, (void *)H_SpreadFrags);
  T_ChangeWorld = (ChangeWorld_t)DetourPrologue(E.fnChangeWorld, (void *)H_ChangeWorld, changeWorldPro);
  T_IterateEntities = (IterateEntities_t)DetourSehPrologue(E.fnIterateEntities, (void *)H_IterateEntities);
  if (!T_CreatePlayer || !T_SpreadFrags || !T_ChangeWorld || !T_IterateEntities) {
    Log("ERROR: detours failed, lobby not installed");
    return false;
  }
  T_Step      = (Step_t)PatchVtableSlot(&vt[8], b + RVA_FN_PROJECT_STEP, (void *)H_Step);
  T_Render    = (Render_t)PatchVtableSlot(&vt[9], b + RVA_FN_PROJECT_RENDER, (void *)H_Render);
  T_WorldLoad = (WorldHook_t)PatchVtableSlot(&vt[15], b + RVA_FN_PROJECT_WORLDLOAD, (void *)H_WorldLoad);
  T_WorldStop = (WorldHook_t)PatchVtableSlot(&vt[18], b + RVA_FN_PROJECT_WORLDSTOP, (void *)H_WorldStop);
  if (!T_Step || !T_Render || !T_WorldLoad || !T_WorldStop) {
    Log("ERROR: vtable patch failed");
    return false;
  }
  if (!PatchCallSite(call, (void *)Stub_ServerSimStart)) {
    Log("ERROR: could not patch the server start");
    return false;
  }
  Log("lobby installed: server start, ChangeWorld, CreatePlayer, frags RPC, entity iteration, "
      "project slots 8/9/15/18");
  return true;
}
