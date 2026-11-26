// Steam invitations (see invite.cpp).
#pragma once
#include "win32.h"

bool InviteInstall(void);                  // registers the Steam callback, reads "+connect_lobby"
void InviteCleanup(void);                  // unregisters it (before the module is unloaded)
bool InviteReadyToJoin(u32 *lo, u32 *hi);  // lobby to join now (asks Steam for its owner first)
