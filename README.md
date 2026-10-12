# r8 One for Sailfish OS

The [QDOS](https://github.com/quadrate-language/qdos) calculator as a Sailfish
OS app: the Android app, [r8-one-android](https://github.com/klahr/r8-one-android),
ported to Qt.

QDOS's shell runs unchanged on a thread of its own. What Android gets from the
SDL3 simulator backend comes here from `src/machine.cpp`, which follows
`sim_sdl3.c` function for function: the case and keypad are QDOS's own drawing
(`keypad_ui.c`), and the stores are laid out the same way. So the phone shows,
and keeps, what the Android app does. SDL is not used at all.

| | Android | Sailfish |
|---|---|---|
| System programs | copied out of the APK on every start | read in place, `/usr/share/harbour-r8-one/programs/system` |
| User store | app files, seeded once | `~/.local/share/rs.r8/harbour-r8-one/store`, seeded once |
| Inbox | external app storage | `~/Documents/r8 One`, reachable over USB (MTP) |
| Battery | none | the phone's own, from `/sys/class/power_supply` |
| Cover | — | the display, live |

The inbox is always reachable from a PC, so there is no card to hand over and
the shell does not offer to share it.

Needs Sailfish OS 5.1 or newer, on aarch64 or armv7hl. The app is sandboxed
and asks for the Documents permission on first start, for the inbox.

Everything it is built from is a submodule under `external/`, at the same
commits as the Android app:

| Submodule | What |
|---|---|
| `external/qdos` | the shell and the keypad drawing |
| `external/quadrate` | the Quadrate interpreter, parser and runtime |
| `external/libu8t` | Quadrate's UTF-8 tokeniser, normally a meson wrap |

## Building

With the [Sailfish SDK](https://docs.sailfishos.org/Tools/Sailfish_SDK/):

```bash
git submodule update --init
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk -c target=SailfishOS-5.1.0.11-armv7hl build
```

The RPM lands in `RPMS/`. Clean the build files between targets: `build/`
holds the native libraries for the last one. The project has to be inside the
SDK workspace.

The RPM's `%build` runs `build.sh`, which builds QDOS's core and the Quadrate
libraries with meson, then qmake links them into the app. The build must run
in-tree, as `sfdk build` does by default.

`build.sh` also runs on a desktop, which is a quick way to see that the native
half still builds after a submodule update.

## License

GPL-3.0-or-later, as QDOS. libu8t GPL-3.0, Quadrate GPL-3.0 and Apache-2.0.
