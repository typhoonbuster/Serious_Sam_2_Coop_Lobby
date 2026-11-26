# Serious Sam 2 Coop Lobby

A mod for the Steam version of Serious Sam 2 that adds an online coop lobby, in the spirit of
Serious Sam HD and Serious Sam 3:

- **A lobby before loading.** A coop session is advertised and can be joined, but the level only
  loads once the session is full (max players, host included). The host can start earlier with F8.
- **Cinematics for everybody.** Once every player has loaded the level, everybody watches the
  level's episode and intro movies, as in single player, then all players enter the map together.
  On level changes: the outro, a wait for the players still loading, then the next level's intro.
- **Steam invitations.** Accepting an invitation, or "Join game" from the friends list, joins the
  game. The base game ignores them.
- **Five languages** for the lobby panel: English, French, Spanish, German and Russian.

The mod is loaded by the base game itself (mod name "SeriousSam2"), so profiles, progress and
Steam achievements remain those of the base game. Single player and deathmatch are unchanged.

## Compatibility

Serious Sam 2 on Steam, version 2.224 (Steam build 24849618). Every player needs the mod.

The mod checks the exact game build when it starts. On any other version it stays inactive, writes
the reason in its log, and the game runs unmodified.

## Installation

Close the game, then use one of the three downloads of the latest release:

- **Windows installer**: `SS2CoopLobby-Setup-<version>.exe`.
- **Linux installer** (the game runs through Steam Play): `SS2CoopLobby-Setup-<version>.sh`, to
  run with `sh SS2CoopLobby-Setup-<version>.sh`. It asks its questions in a window when zenity or
  kdialog is installed, otherwise in the terminal.
- **Archive** for a manual installation: `SS2CoopLobby-<version>.zip`. Its `INSTALL.txt` explains
  it in English, French, Spanish, German and Russian.

Both installers ask for the Serious Sam 2 folder (in Steam: right-click Serious Sam 2 > Manage >
Browse local files) and ask for administrator rights only if that folder needs them. They install
`Bin/SS2CoopLobby.dll`, add `Bin/SS2CoopLobby.ini` unless one exists already, and create
`Content/SeriousSam2.mod`, or add the module to an existing one (keeping the previous file as
`SeriousSam2.mod.bak`).

To update, replace `Bin/SS2CoopLobby.dll`. To uninstall, delete `Bin/SS2CoopLobby.dll`,
`Bin/SS2CoopLobby.ini`, `Bin/SS2CoopLobby.log` and `Content/SeriousSam2.mod` (or, if an installer
modified an existing `SeriousSam2.mod`, put back `SeriousSam2.mod.bak`).

## Settings

`Bin/SS2CoopLobby.ini`. The lobby settings are the host's: `PreLobby`, `LobbyTimeout`,
`PauseDuringLobby`, `WaitOnLevelChange`, `LoadTimeout`, `IntroTimeout` and `KeyStart`. The others
apply to each player's own game.

| Key | Default | Meaning |
|-----|---------|---------|
| `Enabled` | 1 | 0 runs the original game |
| `PreLobby` | 1 | wait for a full session before loading the level |
| `LobbyTimeout` | 0 | seconds before loading anyway (0 = wait forever) |
| `PauseDuringLobby` | 1 | freeze the world until everybody has entered the map |
| `WaitOnLevelChange` | 1 | also wait for everybody and play the intro on level changes |
| `LoadTimeout` | 120 | seconds to wait for players still loading (0 = forever) |
| `IntroTimeout` | 300 | seconds to wait for everybody to finish the cinematic (0 = forever) |
| `PlayIntro`, `PlayOutro` | 1 | play the level movies in coop |
| `Overlay` | 1 | show the lobby panel |
| `Language` | auto | panel language: `auto` (the system's language; the game itself stays in English), `en`, `fr`, `es`, `de` or `ru` |
| `KeyStart` | 119 | host key to start without waiting (Windows virtual-key code; 119 = F8) |
| `SteamInvites` | 1 | join games from Steam invitations |
| `Verbose` | 0 | detailed log |

The mod writes its log to `Bin/SS2CoopLobby.log`.

## Building

Requirements: GNU make and LLVM (`clang`, `lld-link`, `llvm-dlltool`, `llvm-objdump`), tested
with LLVM 23. No Windows SDK, no C runtime and no file from the game are needed.

```
make
```

`out/` then holds the three files of the installation, in the game's own layout.

Releases are built by GitHub Actions (`.github/workflows/release.yml`): the mod, its Windows
installer (NSIS), its Linux installer (makeself) and the zip. Running the workflow by hand attaches
them to the run; pushing a tag such as `v1.0.0` publishes them as a release.

## How it works

The mod is an engine module: the game loads every module listed in `Content/SeriousSam2.mod`. At
startup it hooks a few engine and game functions: the server start, world changes, player
creation, a broadcast message the game only uses in deathmatch (the mod's channel to the clients),
and the project's step and render. Lobby state travels in the players' own spawn requests and in
that broadcast, so the network protocol of the game is unchanged.

## Known limitations

- Players without the mod can still join a public session from the server list and take a slot,
  but the mod does not work for them.
- In the panel, the host sees the players' names; the other players see their numbers.

## License

GNU General Public License, version 2: see [LICENSE](LICENSE).

This project is not affiliated with Croteam or Devolver Digital. Serious Sam is a trademark of its
owners.
