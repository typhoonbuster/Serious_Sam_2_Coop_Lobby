#include "hook.h"
#include "util.h"

static u8 *g_trampPool = 0;
static u32 g_trampUsed = 0;

static u8 *AllocExec(u32 n) {
  if (!g_trampPool) {
    g_trampPool = (u8 *)VirtualAlloc(0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_trampPool) return 0;
  }
  if (g_trampUsed + n > 4096) return 0;
  u8 *p = g_trampPool + g_trampUsed;
  g_trampUsed += (n + 15) & ~15u;
  return p;
}

void *DetourSehPrologue(u8 *target, void *hook) {
  static const u8 expect[5] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
  return DetourPrologue(target, hook, expect);
}

void *DetourPrologue(u8 *target, void *hook, const u8 expect[5]) {
  for (int i = 0; i < 5; i++)
    if (target[i] != expect[i]) {
      Log("ERROR: cannot detour %x (prologue changed or already hooked)", (u32)target);
      return 0;
    }
  u8 *tr = AllocExec(16);
  if (!tr) return 0;
  memcpy(tr, target, 5);                                    // stolen instructions
  tr[5] = 0xE9;                                             // jmp target+5
  *(i32 *)(tr + 6) = (i32)((target + 5) - (tr + 10));
  u32 old;
  if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
  target[0] = 0xE9;                                         // jmp hook
  *(i32 *)(target + 1) = (i32)((u8 *)hook - (target + 5));
  VirtualProtect(target, 5, old, &old);
  FlushInstructionCache(GetCurrentProcess(), target, 5);
  return tr;
}

bool PatchCallSite(u8 *site, void *fn) {
  if (site[0] != 0xff || site[1] != 0x15) {
    Log("ERROR: call site %x is not an indirect call", (u32)site);
    return false;
  }
  u32 old;
  if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &old)) return false;
  site[0] = 0xE8;                                           // call rel32
  *(i32 *)(site + 1) = (i32)((u8 *)fn - (site + 5));
  site[5] = 0x90;                                           // nop
  VirtualProtect(site, 6, old, &old);
  FlushInstructionCache(GetCurrentProcess(), site, 6);
  return true;
}

void *PatchVtableSlot(void **slot, void *expected, void *hook) {
  if (*slot != expected) {
    Log("ERROR: vtable slot %x holds %x, expected %x", (u32)slot, (u32)*slot, (u32)expected);
    return 0;
  }
  u32 old;
  if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return 0;
  void *prev = *slot;
  *slot = hook;
  VirtualProtect(slot, 4, old, &old);
  return prev;
}
