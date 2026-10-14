# Cataclysm for Nintendo Switch (homebrew port)

A port of the original **Cataclysm** (the 2010 ASCII roguelike by Whales) to Nintendo Switch homebrew
(`.nro`), with controller support, an on-screen keyboard and an in-game controller settings menu.
It also builds and runs on Linux with SDL2.

> **Please read this first**
>
> - I am **not a programmer**. I made this port with the help of an AI assistant (Claude, by Anthropic).
> - It was **only tested in an emulator** (Ryujinx / Ryubing). It was **never tested on real Switch hardware.**
> - It is provided **as is, without any warranty**. I take **no responsibility** for crashes, lost saves,
>   damaged data, or anything else that might happen when you use it.
> - **I will not be fixing bugs or providing support.** This is a finished hobby project, not an actively
>   maintained one. If you want to improve it, please fork it and go ahead - that is the whole point of
>   publishing it.
> - It is **not affiliated with or endorsed by Nintendo.** It is homebrew for consoles that can run
>   homebrew. It contains no Nintendo SDK code.

## Credits and license

- The game is **Cataclysm** by **Whales**: <https://github.com/Whales/Cataclysm>
- The original game is licensed under **Creative Commons Attribution-ShareAlike 3.0 Unported
  (CC BY-SA 3.0)**: <https://creativecommons.org/licenses/by-sa/3.0/>
- This repository is a **modified version** of that game. The changes are listed below. As the license
  requires, this whole repository is shared under the **same license (CC BY-SA 3.0)**, with credit to the
  original author. See the `LICENSE` file.
- You are free to use, share and modify it under those terms.

### What was changed from the original

- The curses/ncurses screen code (`catacurse.h` / `catacurse.cpp`) was replaced with an SDL2 + SDL2_ttf
  renderer (an 80x25 character grid in a window).
- Game controller support, with bindings that can be changed in the game (see Controls).
- An on-screen keyboard (so every key can be typed with a controller).
- Fixes so that the 2010 code compiles and runs on modern compilers and on 64-bit ARM
  (for example: C++98 mode, missing `return` statements, pointer-in-`va_arg` bug in `setvector.cpp`,
  signed `char`, a big-stack thread for the Switch build).
- A new help page about the controller, and a hint bar on the main menu.
- A build for Nintendo Switch (`Makefile.switch`, `switch_main.c`).

## Install (Nintendo Switch / emulator)

Put these files on the SD card (for an emulator, in its virtual SD card folder):

```
sdmc:/switch/cataclysm/cataclysm.nro
sdmc:/switch/cataclysm/font.ttf        <- any monospace TrueType font, named font.ttf
sdmc:/switch/cataclysm/data/           <- the game's data folder
```

The game keeps its saves (`save/`) and its settings (`controls.cfg`) in the same folder.
If `font.ttf` is missing the game closes immediately, and writes a line to `cataclysm.log`.

## Controls

The first start creates `controls.cfg` with a default layout. Everything can be changed **in the game**:

- **L3 + R3** (press both sticks) - controller settings menu: rebind any button, stick direction or
  combination (hold LT / RT / LB / RB and press another input) to any key; stick deadzone; key repeat;
  how movement works (`auto` = d-pad or left stick, never both at once / stick only / d-pad only / both).
  Changes are saved automatically.
- **START** - on-screen keyboard. D-pad/stick to move, **A** types the key, **X** is backspace,
  **B** or **START** closes it.
- On a PC keyboard: **F6** opens the settings, **F5** reloads `controls.cfg`.
- The help menu (`?`) has a "Controller settings" page (`o`).

Default layout: stick or d-pad = move, A = enter, B = esc, X = `i`, Y = `e`, LB = `<`, RB = `>`,
minus = `?`. The face buttons may be swapped compared to what you expect on some controllers,
use the settings menu to fix that.

## Known problems

- Tested only in an emulator. Real hardware may behave differently.
- Saving the map when quitting can take a while.
- The side panels are large and the map is small (the original 80x25 layout).
- It is an old, unstable alpha game ("This alpha release is highly unstable").

## Build from source

### Nintendo Switch

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev`, `switch-sdl2`
and `switch-sdl2_ttf` packages, then:

```
make -f Makefile.switch
```

This produces `cataclysm.nro`.

### Linux

Install SDL2 and SDL2_ttf (for example `sudo pacman -S sdl2 sdl2_ttf ttf-dejavu`), then:

```
make
./cataclysm
```

## Third-party components

Built with devkitPro / libnx, SDL2, SDL2_ttf, FreeType and HarfBuzz. They keep their own licenses.
If you distribute a font together with the game, check that font's license.
