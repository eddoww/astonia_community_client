# Astonia Community Client — Mod SDK

Everything needed to build a native client mod out of tree, without a
checkout of the client repository.

## Contents

    README.md                  this file
    src/                       client API headers (mirrors the client's src/)
      astonia.h                exported client API
      dll.h                    DLL_EXPORT/DLL_IMPORT plumbing
      game/memory.h            allocator selection, pulled in by astonia.h
      amod/amod.h              the mod API — start here
      amod/amod_structs.h      structs shared between client and mods
      amod/amod_options.h      option rows mods can add to the Options screen
    include/
      SDL3/SDL_keycode.h       one-typedef shim, see "SDL3 headers" below
    lib/                       Windows import libraries (absent from the
      moac.a                     header-only Linux CI artifact)
      moac.lib                   same archive, named for MSVC

## Building a mod

Compile with `-Isrc -Iinclude` and `-DUSE_MIMALLOC=0`. The define matters:
`game/memory.h` defaults to the mimalloc allocator and would try to
`#include <mimalloc.h>`, which you don't have — `USE_MIMALLOC=0` selects the
plain libc allocator instead.

Linux:

    cc -shared -fPIC -fvisibility=hidden -DUSE_MIMALLOC=0 -Isrc -Iinclude \
       -Wl,--allow-shlib-undefined -o mymod.so mymod.c

Windows (MSYS2/MinGW). Linking `lib/moac.a` (or `moac.lib` with MSVC) is
mandatory — a DLL with unresolved client symbols loads fine but crashes the
game on its first API call:

    cc -shared -DUSE_MIMALLOC=0 -Isrc -Iinclude -o mymod.dll mymod.c lib/moac.a

macOS:

    cc -shared -fPIC -DUSE_MIMALLOC=0 -Isrc -Iinclude \
       -Wl,-undefined,dynamic_lookup -o mymod.dylib mymod.c

There is no import library on Linux/macOS: the mod is loaded into the client
process and the dynamic loader resolves the client's exported symbols at load
time — that is what the allow-undefined linker flags are for.

## Shipping a mod

The client scans one directory per mod under the player's user data folder
and loads whatever library it finds there — the filename is yours to choose,
there are no `bmod`..`fmod` slots any more and no limit on how many mods can
be installed:

    <userdir>/mods/
      my-mod/
        mod.json          required — this is what marks the folder as a mod
        mymod.so          any name; one platform library per folder
        assets/…          your own files, not scanned

`<userdir>` is `~/.local/share/Astonia/` on Linux, `%APPDATA%\Astonia\` on
Windows and `~/Library/Application Support/Astonia/` on macOS, or whatever
path the launcher passes via `--userdir`.

`mod.json`:

    {
      "id": "my-mod",
      "name": "My Mod",
      "version": "1.0.0",
      "author": "you",
      "description": "what it does"
    }

Only the file itself is required. `id` defaults to the folder name, `name` to
`id`, `version` to `"unknown"`. Add `"entry": "mymod"` (no extension) if the
folder contains more than one library for a platform — the client refuses to
guess which of them is the mod.

A mod folder may also hold `*.lua` scripts; they are loaded into the Lua
scripting sandbox under the same identity, so one mod can ship both.

Implement `amod_set_mod_dir(const char *dir)` to receive your own folder
before `amod_init()` and load your assets from there rather than relative to
the game's working directory.

The player's enable/disable state lives in `<userdir>/mods/mods.json`, written
by the client (Options ▸ Gameplay ▸ Installed Mods) and by the launcher. Do
not write it from a mod.

## Privileged mod

One mod — the game's own system mod, loaded from `bin/amod.<dll|so|dylib>`
next to the binary — may additionally override client functions, claim the
server's mod packet stream and replace the client's game data tables. That
privilege comes from the fixed path, so nothing installed under `mods/` can
take it. Everything else in `src/amod/amod.h` is available to every mod.

## SDL3 headers

`amod/amod.h` uses `SDL_Keycode` from `<SDL3/SDL_keycode.h>`.
`include/SDL3/SDL_keycode.h` is an ABI-identical one-typedef shim so mods
build without the SDL3 development headers. If you prefer building against
the real SDL3 headers, drop `-Iinclude`.

## Notes

- The headers are C11 and also compile as C++.
- The headers and import libraries match the client release this SDK was
  packaged with; rebuild your mod against the SDK of the release you target.
