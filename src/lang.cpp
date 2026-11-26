// Overlay texts.
//
// The engine draws char strings as UTF-8 (gfuPutColorText3f decodes them with strReadWChar).
// The overlay uses the game's HUD fonts: SeriousZurich_HD.fnt for the title, and
// SeriousVerdana_MENU.fnt for the rest, which lacks a few glyphs. Every text must avoid:
//   French  Â Ç Ê Ë Î Ï Ô Ù Û Ÿ œ Œ ÿ   (lower-case accented letters are fine)
//   Russian Ъ Ё ё                        (Е/е stand for Ё/ё, as is common practice)
#include "lang.h"
#include "util.h"

static const char *const kCodes[LANG_COUNT] = {"en", "fr", "es", "de", "ru"};

// One row per language, in the order of enum Msg.
static const char *const kText[LANG_COUNT][MSG_COUNT] = {
    {
        // English
        "COOP LOBBY",
        "LOADING PLAYERS",
        "CINEMATIC",
        "Level:",
        "Connected to the host.",
        "Waiting for the other players...",
        "The level loads when the lobby is full.",
        "Players: %d / %d",
        "Players ready: %d / %d",
        "Cinematic finished: %d / %d",
        "Host",
        "Player %d",
        "  (you)",
        "  (host)",
        "connecting...",
        "in the lobby",
        "loading...",
        "done",
        "watching",
        "ready",
        "movie",
        "Automatic start in %d s",
        "start the game now",
        "stop waiting",
        "Enter",
        "Space",
    },
    {
        // French
        "SALON COOP",
        "CHARGEMENT DES JOUEURS",
        "CINÉMATIQUE",
        "Niveau :",
        "Connecté à l'hôte.",
        "En attente des autres joueurs...",
        "Le niveau se chargera quand le salon sera complet.",
        "Joueurs : %d / %d",
        "Joueurs prêts : %d / %d",
        "Cinématique terminée : %d / %d",
        "Hôte",
        "Joueur %d",
        "  (vous)",
        "  (hôte)",
        "connexion...",
        "dans le salon",
        "chargement...",
        "terminé",
        "regarde la cinématique",
        "prêt",
        "cinématique",
        "Lancement automatique dans %d s",
        "lancer la partie maintenant",
        "ne plus attendre",
        "Entrée",
        "Espace",
    },
    {
        // Spanish
        "SALA COOPERATIVA",
        "CARGANDO JUGADORES",
        "CINEMÁTICA",
        "Nivel:",
        "Conectado al anfitrión.",
        "Esperando a los demás jugadores...",
        "El nivel se cargará cuando la sala esté completa.",
        "Jugadores: %d / %d",
        "Jugadores listos: %d / %d",
        "Cinemática terminada: %d / %d",
        "Anfitrión",
        "Jugador %d",
        "  (tú)",
        "  (anfitrión)",
        "conectando...",
        "en la sala",
        "cargando...",
        "terminado",
        "viendo la cinemática",
        "listo",
        "cinemática",
        "Inicio automático en %d s",
        "empezar la partida ahora",
        "dejar de esperar",
        "Intro",
        "Espacio",
    },
    {
        // German
        "KOOP-LOBBY",
        "SPIELER LADEN",
        "ZWISCHENSEQUENZ",
        "Level:",
        "Mit dem Host verbunden.",
        "Warte auf die anderen Spieler...",
        "Das Level wird geladen, sobald die Lobby voll ist.",
        "Spieler: %d / %d",
        "Spieler bereit: %d / %d",
        "Zwischensequenz beendet: %d / %d",
        "Host",
        "Spieler %d",
        "  (du)",
        "  (Host)",
        "verbindet...",
        "in der Lobby",
        "lädt...",
        "fertig",
        "schaut zu",
        "bereit",
        "Video",
        "Automatischer Start in %d s",
        "Spiel jetzt starten",
        "nicht mehr warten",
        "Eingabe",
        "Leertaste",
    },
    {
        // Russian
        "КООП-ЛОББИ",
        "ЗАГРУЗКА ИГРОКОВ",
        "ВИДЕОРОЛИК",
        "Уровень:",
        "Подключено к хосту.",
        "Ожидание других игроков...",
        "Уровень загрузится, когда лобби заполнится.",
        "Игроки: %d / %d",
        "Игроков готово: %d / %d",
        "Ролик просмотрели: %d / %d",
        "Хост",
        "Игрок %d",
        "  (вы)",
        "  (хост)",
        "подключение...",
        "в лобби",
        "загрузка...",
        "досмотрел",
        "смотрит ролик",
        "готов",
        "ролик",
        "Автозапуск через %d с",
        "начать игру сейчас",
        "больше не ждать",
        "Enter",
        "Пробел",
    },
};

static int Lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int LangFromCode(const char *s) {
  for (int i = 0; i < LANG_COUNT; i++) {
    if (Lower(s[0]) == kCodes[i][0] && Lower(s[1]) == kCodes[i][1] &&
        !((s[2] >= 'a' && s[2] <= 'z') || (s[2] >= 'A' && s[2] <= 'Z')))
      return i;
  }
  return -1;
}

// "auto": the user's language setting (Windows', or the locale Proton passes on Linux). The game
// itself does not follow it: the Steam release is in English.
static int LangFromSystem(void) {
  switch (GetUserDefaultLCID() & 0x3ff) {   // primary language id
    case 0x0c: return LANG_FR;
    case 0x0a: return LANG_ES;
    case 0x07: return LANG_DE;
    case 0x19: return LANG_RU;
    default:   return LANG_EN;
  }
}

int LangFromSetting(const char *setting) {
  int lang = (setting && setting[0]) ? LangFromCode(setting) : -1;
  return lang >= 0 ? lang : LangFromSystem();
}

const char *LangCode(int lang) { return (lang >= 0 && lang < LANG_COUNT) ? kCodes[lang] : "?"; }

const char *Tr(Msg id) {
  int lang = (g_cfg.language >= 0 && g_cfg.language < LANG_COUNT) ? g_cfg.language : LANG_EN;
  return (id >= 0 && id < MSG_COUNT) ? kText[lang][id] : "";
}
