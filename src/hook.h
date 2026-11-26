// Code patching helpers (x86).
#pragma once
#include "win32.h"

// Replace the 5-byte MSVC prologue "push ebp; mov ebp,esp; push -1" of 'target' with a JMP to
// 'hook'. Returns a trampoline that behaves like the original function, or 0 on failure.
void *DetourSehPrologue(u8 *target, void *hook);

// Same for any function whose first 5 bytes equal 'expect' and contain only whole instructions
// without relative addressing.
void *DetourPrologue(u8 *target, void *hook, const u8 expect[5]);

// Replace a 6-byte "call dword ptr [imm32]" with "call rel32; nop" to 'fn' (same stack contract).
bool PatchCallSite(u8 *site, void *fn);

// Swap a vtable slot (only if it currently holds 'expected'). Returns the previous pointer.
void *PatchVtableSlot(void **slot, void *expected, void *hook);
