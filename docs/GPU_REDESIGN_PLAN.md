# GPU Rendering Redesign — Shader Effects, Base Textures, Real Batching

Status: **phase 1 landed** on `feature/gpu-shader-effects` (behind the
`gpu_shader_effects` experimental sub-flag, default OFF).

The shipped GPU path (`gpu_rendering`, on `main`) is parity-only: every
(sprite × effect combination) is baked on the CPU into its own texture and
drawn with its own pipeline bind. This document describes the actual
redesign — base textures uploaded once, the effect pipeline in the fragment
shader, instanced batching through a texture atlas — what phase 1 delivered,
what was measured, and what phases 2/3 should contain.

Related docs on PrismaPhonic's fork (`claude/sdl-gpu-shader-redesign-ScTrY`):
`SDL3_GPU_SHADER_REDESIGN.md` and `CURRENT_RENDERING_ARCHITECTURE.md` were
used as design input; §6 lists where this implementation deliberately
diverges from them.

---

## 1. Architecture

```
                 flag OFF (default)                    flag ON (gpu_shader_effects)
render_sprite_fx ──────────────────────┐   ┌──────────────────────────────────────
                                       │   │
  sdl_tx_load(sprite × 17 fx params)   │   │  sdl_tx_load(sprite, NEUTRAL)  ── one
  → CPU bake (sdl_make: colorize,      │   │  → CPU bake is a plain copy    cache
    balance, shine, light, sink,       │   │  → atlas page region upload    entry
    freeze) → per-combo texture        │   │       (sdl_gpu_atlas.c)        per
  → sdl_blit → gpu_draw_texture        │   │                                sprite
    (1 pipeline bind + draw each)      │   │  sdl_blit_fx(base entry, fx params)
                                       │   │  → gpu_fx_instance_t appended to the
                                       │   │    frame's instance buffer
                                       │   │  → instanced draw per (pass × page)
                                       │   │    run; effects run in sprite_fx.frag
```

### 1.1 The correctness contract (build the checker first)

The way this program fails is **effect mismatch**: the CPU pipeline
(`sdl_effects.c` + the bake loop in `sdl_image.c:sdl_make`) is a port of the
original DirectDraw client and two decades of art were calibrated against
its exact quirks (the `lightmulti[]` curve, the colorbalance `cg`
double-scaling typo — see the repo `CLAUDE.md`). So the checker exists
before the client integration:

- `tests/test_shaderfx_compare.c` renders the same synthetic sprite + effect
  combination through the real `sdl_make()` **and** through a real SDL_GPU
  draw with the production `sprite_fx` shaders, then diffs every pixel.
- `res/shaders/sprite_fx.frag` is an **exact port**, not an approximation:
  integer effects use integer GLSL math (bit-exact), double-based effects
  (colorize, shine) use float32 (≤ 1 LSB observed). Effect order matches the
  bake loop: colorize → colorbalance → shine → 5-way lighting → sink →
  freeze. The pix2 colorize neighbour test is implemented with bounded
  `texelFetch` reads.
- The checker gates the effect matrix (§4); anything that fails stays on
  the CPU-bake fallback (mixed pipeline by design).

### 1.2 Base textures

A "base" cache entry is the sprite with **no effects and neutral lighting**
(`ml=ll=rl=ul=dl=15`, which is the identity through `light_calc`, everything
else 0). It reuses the existing texture cache, loaders, worker threads and
LRU untouched — the shader path simply requests that combination, so:

- one cache entry per sprite instead of one per (sprite × lighting × tint ×
  …) combination — the cache-thrash source is gone;
- `sdl_pre_add` rewrites prefetch requests to the base combination when the
  path is active, so the prefetcher warms the right entries;
- `drop_alpha` sprites remain correct: no effect changes alpha, so dropping
  at bake time commutes with the shader-side effect chain.

### 1.3 Per-draw effect data (128-byte instances)

`gpu_fx_instance_t` (`src/sdl/sdl_gpu_shaderfx.h`, mirrored as the std430
struct in the shaders) carries the RAW `RenderFX`/`sdl_texture` parameter
values — all quirk-scaling happens inside the shader so C and GLSL cannot
drift apart on conversion. Sprite-relative texel origin + size travel with
the instance so clipped draws still light/sink correctly, and atlas
placement is transparent to the math.

### 1.4 Batching without fences (the sdl_gpu_batch.c fix)

The bypassed first-generation batcher submitted an upload command buffer
and **blocked on a fence** at every flush — that is why it lost to the
unbatched path and was disabled. The redesign:

- instances are memcpy'd into a persistently-mapped transfer buffer as
  draws are recorded (no GPU work mid-frame);
- draws are recorded immediately into the current render pass (painter's
  order is preserved — flush barriers run before every direct draw, pass
  switch and frame end), addressing their instance range via a
  base-instance uniform (`SV_InstanceID`/D3D12-safe);
- at frame end ONE upload command buffer carries a single copy for the
  whole frame and is submitted **before** the main render command buffer.
  SDL_GPU guarantees command buffers execute in submission order and
  auto-inserts the copy→read barrier. Zero waits, zero fences;
- a 3-deep ring of instance/transfer buffers covers frames in flight
  (swapchain acquire bounds them to 2), transfer maps use `cycle=true`.

### 1.5 Base-texture atlas (what makes batching real)

Per-sprite textures force a flush per texture change, so batching alone
buys nothing (measured: 2567 sprites → 1280 draws, ~2 sprites/draw). The
atlas (`src/sdl/sdl_gpu_atlas.c`) packs base textures into shared
2048×2048 pages (shelf packer, height-quantized shelves, thread-safe,
lazily created): the whole Cameron town scene fits ONE page and renders in
**37** instanced draws.

Exactness survives packing because the fragment shader addresses texels
with `texelFetch` (no sampler filtering → no bleed) and the colorize
neighbour test is bounded to the sprite's own rect.

`sdl_blit` (parity/fallback path) is atlas-aware, so an atlased entry drawn
outside the batch (e.g. GUI icons at `scale != 100`) renders its own region
— this was the one real integration bug found (blank hotbar icons).

### 1.6 Fallbacks — the mixed pipeline is the design

A draw falls back to the CPU-baked combo texture when any of these hold:

| condition | why |
|---|---|
| flag off / GPU renderer off / pipeline missing | zero-behavior-change guarantee |
| `scale != 100` | the CPU colorizes the four source taps **before** bilinear resampling; a sampler cannot reproduce tap-order |
| base texture still uploading | never stall a frame on a texture |
| instance budget exhausted (16384/frame) | never drop a sprite |
| no active GPU frame | mod/init-time draws |

Everything else (all effect combos in §4) goes through the shader.

---

## 2. What phase 1 landed (this branch)

| commit | content |
|---|---|
| `test(gpu): CPU/GPU effect-pipeline comparison harness…` | checker + exact-port `sprite_fx` shaders + glslc build rule |
| `feat(gpu): shader-effects sprite path behind gpu_shader_effects…` | flag plumbing, `sdl_gpu_shaderfx.c` (ring batcher), `render_sprite_fx` routing, prefetch rewrite, flush barriers, stats |
| `feat(gpu): base-texture atlas…` | `sdl_gpu_atlas.c`, stage-3 atlas insert, atlas-aware `sdl_blit`, eviction guards |
| `docs: GPU redesign plan` | this document |

Flag surface: `GO_SHADERFX` (`-o` bit 23) or `"gpu_shader_effects": true`
in the extra options file; requires `gpu_rendering`. Runtime activation is
`gpu_shaderfx_ready()` — false means every draw behaves exactly as before.

`ASTONIA_GPU_STATS=1` logs per-second: total draws, fx draws, fx sprites,
atlas pages, CPU render span, wall ms/frame.

---

## 3. Measurements (localtest Cameron town, 1280×720, RX 6950 XT / RADV,
isolated kwin_wayland virtual display, fps cap lifted, same scene standing
still as Newmage)

| metric (per frame, 1 s averages) | parity path | shader-fx + atlas |
|---|---:|---:|
| total draw calls | **3 870** | **1 340** |
| world-sprite draw calls | 2 567 (1 each) | **37** (instanced, ~70 sprites/draw) |
| batched sprites | — | 2 567 |
| atlas pages | — | 1 |
| CPU render span (frame begin→submit) | 42.3–45.4 ms | 41.5–42.6 ms |
| wall ms/frame | ~48 | ~47 |

Honest reading:

- The **draw-call wall is gone**: 2567 → 37 for the world pass. Texture
  binds/pipeline binds for sprites collapse accordingly.
- **Frame time barely moved** — because the sprite path was NOT the
  dominant CPU cost on this scene. A perf profile of the live client shows
  a flat distribution dominated by **text rendering** (FreeType glyph
  loading + per-glyph file seeks + the text-entry hash walk in
  `sdl_tx_load`, together ~15–20%), `sprite_config_lookup_animated` in the
  tick path (~3%), and allocator churn. Those are exactly the ~1300
  remaining direct draws. The parity GPU path underperforming
  SDL_Renderer was never (only) about sprite draw calls.
- The structural win still stands: per-draw texture uploads on cache miss
  disappear (the effect-combo cache explosion is gone), memory for the
  texture cache stops scaling with effect combinations, and effects can
  now animate per-frame for free (lighting changes no longer re-bake).

Numbers to re-measure on a weather-heavy/crowded scene and at 4× scale,
where CPU bakes (42 ms spikes were observed on area load) and cache misses
hurt most — the 197 ms first-frame spike in the logs is the bake burst the
redesign eliminates on the fx path.

---

## 4. Effect-combo match matrix (tests/test_shaderfx_compare.c, RADV)

43 cases, synthetic 40×40 sprites (colorize-sensitive bands, greys,
threshold-straddling ramps, random + transparent holes), sdl_scale 1 and 2.

| effect group | cases | result |
|---|---|---|
| identity / uniform lighting 0..15 (+GO_LIGHTER/+GO_LIGHTER2) | 8 | **bit-exact** |
| 5-way directional lighting (incl. scale 2) | 4 | **bit-exact** |
| colorize old algo (c1/c2/c3 single+mixed, 0x8000 shine bit) | 6 | 3 bit-exact, 3 within **1 LSB** |
| colorize pix2 / sprite ≥ 220000 (incl. neighbour test, scale 2) | 4 | **bit-exact** |
| colorbalance (gilded 50/50, negatives, lightness ±, sat, full) | 6 | **bit-exact** (typo replicated) |
| shine 25/60/100 | 3 | **bit-exact** |
| freeze 1/4/7 | 3 | **bit-exact** |
| sink | 2 | **bit-exact** |
| combined (item tint, char colorize, frozen+lit, everything, scale 2) | 7 | **bit-exact** |

Total: **40 bit-exact, 3 within 1 LSB, 0 failed.** The ≤1 LSB jitter is
double→float truncation at the final `(int)` casts in the old colorize
collect step; tolerance is 2 LSB.

Deferred / not shader-supported (documented CPU fallbacks):

- `scale != 100` (resample-order, §1.6) — **fallback, by design**.
- shine > 100 via multi-channel embedded colorize shine: the CPU wraps
  negative channel values through the `IRGBA` cast; the shader clamps to 0.
  Unreachable in the tested matrix (single-channel 0x8000 is exact); if a
  real asset ever hits it, the checker will show it.

---

## 5. Rollout plan

1. **Now (this branch):** flag default OFF. Merge after review; nothing
   changes for players.
2. **Internal test:** enable `gpu_shader_effects` alongside `gpu_rendering`
   for dev/internal-test builds; watch `ASTONIA_GPU_STATS` and screenshot
   comparisons per area (weather, spells, freeze effects, colorized gear).
3. **Phase 2 (below), then re-measure**; only consider default-ON for the
   GPU path after text rendering is fixed — that is where the visible win
   is.

---

## 6. Divergences from PrismaPhonic's SDL3_GPU_SHADER_REDESIGN.md

The doc's architecture direction (upload base once, effects per draw,
instancing, atlas later) is what phase 1 implements. Its concrete designs
were treated as input, not gospel:

| PrismaPhonic doc | this implementation | why |
|---|---|---|
| Fragment shader with *approximate* effects (bilinear 5-way light blend, colorize as channel remix, freeze = cyan mix, shine = positional specular, sink = alpha fade) | exact integer/float port of `sdl_effects.c` + the bake-loop geometry | the CPU output is the calibrated spec; the doc's math would visibly re-tint two decades of art (its own success metric — "pixel-perfect, diff = 0" — is unreachable with its shader) |
| per-draw uniform buffer updates (64 B) per sprite | 128 B instances in a per-frame storage buffer, instanced draws | uniform pushes per sprite keep the draw count at 1/sprite; instancing is what removes draws |
| separate new BaseTexture cache keyed (sprite, scale) | reuse of the existing `sdlt[]` cache with the neutral combination as the key | the existing cache already has loaders, worker threads, LRU, invariant tests; a parallel cache would duplicate all of it |
| texture atlas as "advanced optimization, layers/texture arrays" | required in phase 1, plain 2D pages + shelf packer | without it batching measured ~2 sprites/draw; arrays don't fit wildly varying sprite sizes |
| worker threads upload via a shared GL-style context, hot-reload thread, mat4 camera/model per sprite | per-thread command buffers (SDL_GPU model); no matrices (pixel-space quad expansion, matches parity vertex path) | simpler and matches how the shipped GPU code already works |
| "remove CPU effect code" in phase 3 | CPU pipeline stays forever as fallback + checker reference | mixed pipeline is the design; the checker needs the CPU truth |
| projected 60%→95% cache hit rate, 2–5 ms/sprite CPU effects | measured: the effect-combo cache entries disappear entirely for eligible draws; bake bursts (~190 ms on area entry) eliminated on the fx path | measured numbers replace projections (§3) |

Also: `CURRENT_RENDERING_ARCHITECTURE.md` describes the pre-GPU-port SDL2
pipeline; its display-list/bake description remains accurate and matches
what was found in `sdl_image.c`/`sdl_texture.c` on `main`.

---

## 7. Phase 2 / Phase 3

**Phase 2 — make the frame time move (text + residual draws):**
- Glyph atlas + run batching for TTF text (FreeType shows per-frame glyph
  *file* I/O in the profile — cache rendered glyphs, batch text quads
  through the same instanced pipeline with a plain-texture mode flag).
  This is the top measured cost.
- Route the remaining direct sprite draws (GUI, mod textures) through the
  batch with a "no-effect" instance mode; keep prims/lines direct.
- Atlas region reclamation (per-shelf free lists) + `gpu_batch_dump`-style
  diagnostics; retire the dead `sdl_gpu_batch.c`.
- DXIL/Metal builds of `sprite_fx` (the loader already falls back
  gracefully when only SPIR-V exists): run `dxc` for DXIL, SPIRV-Cross for
  MSL, extend `Makefile.shaders` targets; verify D3D12 base-instance
  behavior via the uniform (already designed for it).
- Run the checker matrix on those backends before enabling the flag there.

**Phase 3 — retire parity-path costs and unlock features:**
- `scale != 100`: pre-scale base textures on upload (CPU, once per sprite
  per scale) so scaled draws join the shader path with identical taps.
- Per-frame animated lighting/tints server-side (effects are now free to
  vary per frame — no re-bake); evaluate dynamic light maps in-shader.
- Sort/merge heuristics: tiny state-sort within a display-list layer to cut
  the 37 draws further (page runs already do most of it).
- Kill remaining per-frame allocator churn (`mi_malloc` in the profile) in
  the text path.
- Consider default-ON for `gpu_shader_effects` wherever `gpu_rendering` is
  on, then fold both into one option.

---

## 8. Risks

- **Effect drift**: any future change to `sdl_effects.c` must update
  `sprite_fx.frag` and the checker together (the header says so). CI should
  run `tests/test_shaderfx_compare.c` on a GPU runner when one exists; it
  SKIPs cleanly on GPU-less machines, so it's already in `make test`.
- **Backend coverage**: only SPIR-V/Vulkan is validated. The flag
  self-disables elsewhere (no DXIL/MSL binaries yet), so Windows/macOS GPU
  users silently stay on parity until phase 2 — intended, but worth a
  release note.
- **Atlas leak on eviction**: shelf regions are not reclaimed (§1.5). Worst
  case the atlas fills (16 pages × 16 MB) and new sprites fall back to
  standalone textures — correctness unaffected, batching degrades. Fix in
  phase 2.
- **Draw-alpha parity gap (pre-existing)**: the parity GPU path ignores
  `fx->alpha` (`sdl_tex_alpha` only mods SDL_Textures); the fx path honors
  it. Enabling the flag *fixes* translucent spell sprites relative to
  parity GPU mode — flag-off behavior is unchanged, but A/B screenshots of
  alpha-heavy scenes will differ from parity (and match the CPU renderer).
- **Shared instance budget**: 16384 instances/frame (2 MB/slot ×3). Extreme
  scenes overflow to the CPU-baked path per sprite — correct but slower;
  the stats line exposes it.
- **kwin-virtual measurements**: frame-time numbers in §3 were taken on a
  virtual display; absolute values will differ on real hardware/vsync, the
  draw-count ratios will not.
