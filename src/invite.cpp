// Steam invitations.
//
// The base game joins a Steam lobby only from its own server list, or at startup with
// "+connect <lobby id>" (cvar sam_strConnect, read by the project constructor FUN_1001f7c0, joined
// by its startup FUN_10026ef0). It registers no handler for Steam's GameLobbyJoinRequested_t, which
// Steam sends when the player accepts an invitation (or picks "Join game") while the game runs, and
// Steam starts the game from an invitation with "+connect_lobby <lobby id>", which the game ignores.
// Both are added here. The join itself goes through the game's own code (lobby.cpp).
//
// The game does not connect to the lobby but to its owner: CClientInterface::Connect (Engine
// 0x100f2cc0) asks SteamMatchmaking()->GetLobbyOwner(net_idSteamServerLobby). Steam only knows the
// owner of a lobby whose data it has fetched: the server list calls RequestLobbyData for every lobby
// it lists (CSteamOnlineInterface::OnLobbyList). For a lobby that comes from an invitation nobody did,
// the owner was 0 ("Connecting Steam socket to 0x0", then "session no longer available"). So the
// lobby data is requested first, and the join waits until Steam knows the owner.
#include "engine.h"
#include "util.h"

// Steamworks CCallbackBase, MSVC layout, checked against the engine's own callbacks (vtable of its
// CCallback<LobbyEnter_t> @ 0x1021775c): [0] Run(param, bIOFailure, hSteamAPICall), "ret 0x10";
// [1] Run(param); [2] GetCallbackSizeBytes(). +4 flags (bit 0 = registered), +8 callback id.
struct SteamCallback {
  void **vtbl;
  u8 flags;
  u8 pad[3];
  int id;
};
#define CB_GAME_LOBBY_JOIN_REQUESTED 333   // k_iSteamFriendsCallbacks + 33: {CSteamID lobby, friend}

// ISteamMatchmaking of the game's steam_api.dll, slots as the engine itself calls them:
// 0x10 RequestLobbyList, 0x30 GetLobbyByIndex, 0x38 JoinLobby, 0x70 RequestLobbyData,
// 0x8c GetLobbyOwner (CSteamID returned through a hidden pointer, first stack argument).
#define MM_REQUEST_LOBBY_DATA (0x70 / 4)
#define MM_GET_LOBBY_OWNER    (0x8c / 4)
typedef bool (__thiscall *RequestLobbyData_t)(void *self, u32 lo, u32 hi);
typedef void *(__thiscall *GetLobbyOwner_t)(void *self, u32 *ret, u32 lo, u32 hi);
#define OWNER_WAIT_MS 10000

static void (__cdecl *pRegisterCallback)(void *callback, int id);
static void (__cdecl *pUnregisterCallback)(void *callback);
static void *(__cdecl *pSteamMatchmaking)(void);
static void *g_cbVtbl[3];
static SteamCallback g_cbJoin;
static bool g_cbRegistered;
static volatile u32 g_pendLo, g_pendHi;
static volatile int g_pending;
static bool g_requested;     // lobby data asked to Steam for the pending lobby
static u32 g_requestedAt;

static bool IsLobbyId(u32 hi) { return (hi & 0xf00000) == 0x800000 && (hi & 0x40000) != 0; }

static void SetPending(u32 lo, u32 hi) {
  g_pendLo = lo;
  g_pendHi = hi;
  g_requested = false;
  g_pending = 1;
}

static void __thiscall Cb_Run(void *self, void *param) {
  (void)self;
  if (!param) return;
  u32 lo = ((u32 *)param)[0], hi = ((u32 *)param)[1];
  if (!IsLobbyId(hi)) return;
  SetPending(lo, hi);
  Log("Steam: invitation accepted (lobby %x%08x)", hi, lo);
}
static void __thiscall Cb_RunResult(void *self, void *param, bool ioFailure, unsigned long long call) {
  (void)ioFailure;
  (void)call;
  Cb_Run(self, param);
}
static int __thiscall Cb_Size(void *self) {
  (void)self;
  return 16;
}

// "+connect_lobby 109775241234567890" anywhere on the process command line.
static bool LobbyFromCommandLine(char *digits, int cap) {
  const char *cmd = GetCommandLineA();
  static const char key[] = "+connect_lobby";
  int klen = sizeof(key) - 1;
  for (const char *p = cmd; p && *p; p++) {
    if (StrNCmp(p, key, klen) != 0) continue;
    const char *q = p + klen;
    while (*q == ' ' || *q == '\t' || *q == '"' || *q == '=') q++;
    int n = 0;
    while (*q >= '0' && *q <= '9' && n < cap - 1) digits[n++] = *q++;
    digits[n] = 0;
    return n > 0;
  }
  return false;
}
static void DecimalToId(const char *s, u32 *lo, u32 *hi) {
  unsigned long long v = 0;
  for (; *s >= '0' && *s <= '9'; s++) v = (v << 3) + (v << 1) + (unsigned)(*s - '0');
  *lo = (u32)v;
  *hi = (u32)(v >> 32);
}

bool InviteInstall(void) {
  if (!g_cfg.steamInvites) {
    Log("Steam invites disabled in SS2CoopLobby.ini");
    return false;
  }
  if (!EngineResolveInvites()) return false;
  void *sa = GetModuleHandleA("steam_api.dll");
  if (sa) {
    *(void **)&pRegisterCallback = GetProcAddress(sa, "SteamAPI_RegisterCallback");
    *(void **)&pUnregisterCallback = GetProcAddress(sa, "SteamAPI_UnregisterCallback");
    *(void **)&pSteamMatchmaking = GetProcAddress(sa, "SteamMatchmaking");
  }
  if (!pSteamMatchmaking) {
    Log("Steam invites: steam_api.dll has no SteamMatchmaking, invites disabled");
    return false;
  }
  if (pRegisterCallback && pUnregisterCallback) {
    g_cbVtbl[0] = (void *)Cb_RunResult;
    g_cbVtbl[1] = (void *)Cb_Run;
    g_cbVtbl[2] = (void *)Cb_Size;
    g_cbJoin.vtbl = g_cbVtbl;
    g_cbJoin.flags = 0;
    g_cbJoin.id = 0;
    pRegisterCallback(&g_cbJoin, CB_GAME_LOBBY_JOIN_REQUESTED);
    g_cbRegistered = true;
  } else {
    Log("Steam invites: steam_api.dll callbacks not found (only invitations that start the game work)");
  }
  // Started by an invitation: joined like an invitation accepted in game, once the main menu is up
  // (the game's own "+connect" would not know the lobby owner either).
  char digits[24];
  if (LobbyFromCommandLine(digits, sizeof(digits))) {
    u32 lo, hi;
    DecimalToId(digits, &lo, &hi);
    if (!IsLobbyId(hi)) {
      Log("Steam invites: +connect_lobby %s is not a lobby, ignored", digits);
    } else {
      SetPending(lo, hi);
      Log("Steam: started by an invitation, joining lobby %s from the main menu", digits);
    }
  }
  Log("Steam invites ready");
  return true;
}

void InviteCleanup(void) {
  if (g_cbRegistered && pUnregisterCallback) pUnregisterCallback(&g_cbJoin);
  g_cbRegistered = false;
}

static void *Matchmaking(void) { return pSteamMatchmaking ? pSteamMatchmaking() : 0; }

static bool LobbyOwner(u32 lo, u32 hi, u32 *olo, u32 *ohi) {
  void *mm = Matchmaking();
  u32 id[2] = {0, 0};
  if (mm) ((GetLobbyOwner_t)(*(void ***)mm)[MM_GET_LOBBY_OWNER])(mm, id, lo, hi);
  *olo = id[0];
  *ohi = id[1];
  return id[1] != 0;   // k_steamIDNil while Steam does not know the lobby
}

static bool TakePending(u32 *lo, u32 *hi) {
  g_pending = 0;
  g_requested = false;
  *lo = g_pendLo;
  *hi = g_pendHi;
  return true;
}

// Called every frame while only menus are on screen. True when the pending invitation must be
// joined now (the owner is known, or Steam did not tell it in time: the game then shows its own
// "session no longer available").
bool InviteReadyToJoin(u32 *lo, u32 *hi) {
  if (!g_pending) return false;
  u32 now = GetTickCount();
  u32 olo, ohi;
  if (LobbyOwner(g_pendLo, g_pendHi, &olo, &ohi)) {
    Log("Steam: the invitation's game is hosted by %x%08x", ohi, olo);
    return TakePending(lo, hi);
  }
  if (!g_requested) {
    void *mm = Matchmaking();
    bool ok = mm && ((RequestLobbyData_t)(*(void ***)mm)[MM_REQUEST_LOBBY_DATA])(mm, g_pendLo, g_pendHi);
    g_requested = true;
    g_requestedAt = now;
    if (!ok) {
      Log("Steam: Steam refused to look up the invitation's lobby, trying to join anyway");
      return TakePending(lo, hi);
    }
    Log("Steam: asking Steam who hosts the invitation's lobby");
    return false;
  }
  if ((i32)(now - g_requestedAt) > OWNER_WAIT_MS) {
    Log("Steam: no answer about the invitation's lobby after %d s (game over?), trying anyway",
        OWNER_WAIT_MS / 1000);
    return TakePending(lo, hi);
  }
  return false;
}
