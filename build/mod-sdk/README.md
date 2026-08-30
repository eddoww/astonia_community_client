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
       -Wl,--allow-shlib-undefined -o bmod.so mymod.c

Windows (MSYS2/MinGW). Linking `lib/moac.a` (or `moac.lib` with MSVC) is
mandatory — a DLL with unresolved client symbols loads fine but crashes the
game on its first API call:

    cc -shared -DUSE_MIMALLOC=0 -Isrc -Iinclude -o bmod.dll mymod.c lib/moac.a

macOS:

    cc -shared -fPIC -DUSE_MIMALLOC=0 -Isrc -Iinclude \
       -Wl,-undefined,dynamic_lookup -o bmod.dylib mymod.c

There is no import library on Linux/macOS: the mod is loaded into the client
process and the dynamic loader resolves the client's exported symbols at load
time — that is what the allow-undefined linker flags are for.

## SDL3 headers

`amod/amod.h` uses `SDL_Keycode` from `<SDL3/SDL_keycode.h>`.
`include/SDL3/SDL_keycode.h` is an ABI-identical one-typedef shim so mods
build without the SDL3 development headers. If you prefer building against
the real SDL3 headers, drop `-Iinclude`.

## Notes

- The client loads mods from `bin/{a..f}mod.<dll|so|dylib>` next to the game
  binary. `amod` is reserved for the system mod; user mods go in
  `bmod`..`fmod`. Every mod exports `amod_*` symbols regardless of its slot —
  see `src/amod/amod.h` for the entry points and per-slot capabilities.
- The headers are C11 and also compile as C++.
- The headers and import libraries match the client release this SDK was
  packaged with; rebuild your mod against the SDK of the release you target.
