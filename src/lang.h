// Overlay texts in the supported languages (see lang.cpp).
#pragma once
#include "win32.h"

enum Lang { LANG_EN, LANG_FR, LANG_ES, LANG_DE, LANG_RU, LANG_COUNT };

enum Msg {
  MSG_TITLE_LOBBY,       // pre-lobby title
  MSG_TITLE_LOADING,     // title while the players load the level
  MSG_TITLE_CINEMATIC,   // title during the intro movies
  MSG_LEVEL,             // followed by the level name
  MSG_CONNECTED,         // client waiting in the pre-lobby
  MSG_WAITING,
  MSG_LOADS_WHEN_FULL,
  MSG_PLAYERS_FMT,       // "%d / %d"
  MSG_READY_FMT,         // "%d / %d"
  MSG_FINISHED_FMT,      // "%d / %d"
  MSG_HOST,
  MSG_PLAYER_FMT,        // "%d"
  MSG_YOU_SUFFIX,
  MSG_HOST_SUFFIX,
  MSG_ST_CONNECTING,     // player states, right column
  MSG_ST_IN_LOBBY,
  MSG_ST_LOADING,
  MSG_ST_DONE,
  MSG_ST_WATCHING,
  MSG_ST_READY,
  MSG_ST_MOVIE,
  MSG_AUTOSTART_FMT,     // "%d"
  MSG_START_NOW,         // host key, pre-lobby
  MSG_STOP_WAITING,      // host key, later phases
  MSG_KEY_ENTER,
  MSG_KEY_SPACE,
  MSG_COUNT
};

int LangFromSetting(const char *setting);   // "auto" or empty: the system language
const char *LangCode(int lang);
const char *Tr(Msg id);                      // UTF-8 text in the configured language
