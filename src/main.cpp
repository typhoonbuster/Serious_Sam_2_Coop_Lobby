// SS2CoopLobby: Serious Engine 2 module entry points.
// Loaded by Core.dll when a .mod file lists "SS2CoopLobby" in its MODULES section:
// LoadLibrary("Bin/SS2CoopLobby.dll"), then SS2CoopLobby_Startup_t(CResourceFile*) is called.
#include "engine.h"
#include "util.h"

#include "invite.h"

bool LobbyInstall(void);

static void *g_hModule;
static bool g_started;

// ---------------------------------------------------------------------------------------------
// Module beacon. After <Name>_Startup_t returns, the engine obtains the resource "Beacon" from
// "Bin/<Name>.module" and shows "No resource with name Beacon..." if it is missing. Stock modules
// (see Sam2GameGui_Startup_t_internal) register a 0x30-byte CModuleBeacon whose vtable is Core's
// one except slot 0 (the scalar deleting destructor), which lives in the module itself.
static void *g_beaconVtbl[14];
static void (__thiscall *pBeaconDtor)(void *self);
static void (__cdecl *pMemDelete)(void *p);

static void *__thiscall BeaconDeletingDtor(void *self, unsigned flags) {
  pBeaconDtor(self);
  if (flags & 1) pMemDelete(self);
  return self;
}

static bool RegisterBeacon(void *resourceFile) {
  void *core = GetModuleHandleA("Core.dll");
  void *eng = GetModuleHandleA("Engine.dll");
  if (!core || !eng || !resourceFile) return false;
  typedef void (__cdecl *DeclBeg_t)(const char *);
  typedef void (__cdecl *Void_t)(void);
  typedef void (__thiscall *TypeOf_t)(void *self, void *safeDataType);
  typedef void *(__cdecl *MemNew_t)(i32 size, void *dataType);
  typedef void (__thiscall *Ctor_t)(void *self);
  typedef void (__thiscall *Register_t)(void *rf, i32 id, const char *name, void *res);
  DeclBeg_t declBeg = (DeclBeg_t)GetProcAddress(core, "?mdModuleDeclareBeg@SeriousEngine@@YAXPBD@Z");
  Void_t declEnd = (Void_t)GetProcAddress(core, "?mdModuleDeclareEnd@SeriousEngine@@YAXXZ");
  Void_t vmBatch = (Void_t)GetProcAddress(eng, "?vmExecuteBatchedCommands@SeriousEngine@@YAXXZ");
  TypeOf_t typeOf = (TypeOf_t)GetProcAddress(core, "??5CModuleBeacon@SeriousEngine@@QAEXAAVCSafeDataType@1@@Z");
  MemNew_t memNew = (MemNew_t)GetProcAddress(core, "?memNewRC_internal@SeriousEngine@@YAPAXJPAVCDataType@1@@Z");
  Ctor_t ctor = (Ctor_t)GetProcAddress(core, "??0CModuleBeacon@SeriousEngine@@QAE@XZ");
  Register_t reg = (Register_t)GetProcAddress(core, "?RegisterModuleResource@CResourceFile@SeriousEngine@@QAEXJPBDPAVCResource@2@@Z");
  void **baseVtbl = (void **)GetProcAddress(core, "??_7CModuleBeacon@SeriousEngine@@6B@");
  *(void **)&pBeaconDtor = GetProcAddress(core, "??1CModuleBeacon@SeriousEngine@@UAE@XZ");
  *(void **)&pMemDelete = GetProcAddress(core, "?memDelete_internal@SeriousEngine@@YAXPAX@Z");
  if (!declBeg || !declEnd || !vmBatch || !typeOf || !memNew || !ctor || !reg || !baseVtbl ||
      !pBeaconDtor || !pMemDelete) {
    Log("ERROR: module beacon symbols not found");
    return false;
  }
  declBeg("SS2CoopLobby");
  declEnd();
  vmBatch();
  u32 safeDataType[4] = {0, 0, 0, 0};
  typeOf(0, safeDataType);   // same call as the stock modules: yields the CModuleBeacon data type
  void *beacon = memNew(0x30, (void *)safeDataType[0]);
  if (!beacon) return false;
  ctor(beacon);
  for (int i = 0; i < 14; i++) g_beaconVtbl[i] = baseVtbl[i];
  g_beaconVtbl[0] = (void *)BeaconDeletingDtor;
  *(void ***)beacon = g_beaconVtbl;
  reg(resourceFile, 1000, "Beacon", beacon);
  return true;
}

extern "C" __declspec(dllexport) void __cdecl SS2CoopLobby_Startup_t(void *resourceFile) {
  if (g_started) return;
  g_started = true;
  LogInit(g_hModule);
  Log("SS2CoopLobby " __DATE__ " " __TIME__ " starting");
  // Always register the beacon first: without it the engine refuses the module at startup,
  // even when the lobby itself stays disabled.
  if (RegisterBeacon(resourceFile)) Log("module beacon registered");
  else Log("ERROR: could not register the module beacon");
  ConfigLoad(g_hModule);
  if (!EngineResolve()) {
    Log("mod inactive (see errors above). The game runs unmodified.");
    return;
  }
  if (!g_cfg.enabled) {
    Log("disabled in SS2CoopLobby.ini");
    return;
  }
  if (!LobbyInstall()) return;
  Log("SS2CoopLobby ready.");
  InviteInstall();   // optional: the lobby works without it
}

extern "C" __declspec(dllexport) void __cdecl SS2CoopLobby_Cleanup(void *resourceFile) {
  (void)resourceFile;
  InviteCleanup();   // Steam must not call into an unloaded module
  Log("SS2CoopLobby cleanup");
}

// Every hook body is guarded by __try/__except, so that a bug in the mod cannot crash the game.
// clang implements SEH synchronously: only faults raised inside *calls* made from a __try block are
// caught, so every hook keeps its risky work in called (non-inlined) functions.

extern "C" int __stdcall _DllMainCRTStartup(void *hinst, u32 reason, void *reserved) {
  (void)reserved;
  if (reason == 1 /*DLL_PROCESS_ATTACH*/) g_hModule = hinst;
  return 1;
}
