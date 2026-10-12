# r8 One

An RPN calculator for Sailfish OS whose input language is
[Quadrate](https://github.com/quadrate-language/quadrate), a stack language. The keypad
that adds two numbers also declares functions, loops and runs whole programs.

It runs the [QDOS](https://github.com/quadrate-language/qdos) shell, with the
same display and keypad as the calculator hardware it was made for.

- RPN stack with undo, and a line for typing Quadrate
- Shift for inverse functions and symbols, ALPHA for names
- Graphs, from RPN or a formula such as `x^2 + 3x - sin(x)/2`
- Programs that ship with the app, and your own
- The display, live on the cover

Needs Sailfish OS 5.1 or newer, on aarch64 or armv7hl. The app is sandboxed
and asks for the Documents permission on first start, for the inbox.

## Programs

| | Where |
|---|---|
| System programs | `/usr/share/harbour-r8-one/programs/system`, read-only |
| Your programs | `~/.local/share/rs.r8/harbour-r8-one/store` |
| Inbox | `~/Documents/r8 One`, reachable from a PC over USB |

Copy a program into the inbox from a PC and it shows up in the app.

## Building

The calculator core and the Quadrate interpreter are git submodules under
`external/`, so clone with them:

```bash
git clone --recursive git@github.com:klahr/r8-one-sailfish.git
```

Building needs the
[Sailfish SDK](https://docs.sailfishos.org/Tools/Sailfish_SDK/), with the
project inside the SDK workspace:

```bash
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk -c target=SailfishOS-5.1.0.11-armv7hl build
```

The RPM ends up in `RPMS/`. Clean `build/` between targets.

## License

GPL-3.0-or-later. Quadrate is GPL-3.0 and Apache-2.0, libu8t GPL-3.0.
