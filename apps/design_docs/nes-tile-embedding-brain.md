# NES Tile Embedding Brain

## Motivation

The current RNN v2 brain observes the NES screen via palette-index histograms: each cell in
a 21x21 grid holds a distribution over 10 color clusters. This works well when the palette
is consistent (Mario beats 1-1), but fails when the game changes palettes (1-2 uses a
different underground palette). The network learned to associate specific colors with terrain
features, so a palette change breaks those associations.

Tile data solves this. The NES PPU renders backgrounds from a 32x30 grid of tile IDs stored
in nametable RAM. The actual tile graphics (8x8 pixel patterns) are stored in CHR ROM/RAM
and are palette-independent. By hashing the CHR pattern bytes for each tile, we derive a
token that represents what the tile looks like regardless of palette or CHR bank state. For
SMB, pattern-hashed tile tokens remove palette dependence entirely while staying stable
across any tile-ID remapping. More generally, they are invariant to palette and CHR bank
remapping, though genuinely new CHR patterns still need training exposure.

## NES Tile Architecture

The PPU uses a fixed tile-based rendering pipeline. This is universal across all NES games.

```
Pattern Tables (CHR ROM/RAM)         Nametables (VRAM, 2KB)
┌──────────────────────────┐         ┌────────────────────────────┐
│ 256 tiles per bank       │         │ 32x30 grid of tile IDs     │
│ 8x8 pixels, 2bpp each   │         │ Each byte = index into     │
│ 16 bytes per tile        │◀────────│ pattern table              │
│ Two banks (left/right)   │  lookup │                            │
└──────────────────────────┘         │ + 64-byte attribute table  │
                                     │   (palette per 2x2 group)  │
                                     └────────────────────────────┘

OAM (256 bytes)                      Scroll Registers
┌──────────────────────────┐         ┌────────────────────────────┐
│ 64 sprites, 4 bytes each │         │ V register (15-bit)        │
│ Y, tile ID, attrs, X     │         │  - coarse X/Y scroll       │
│ Player, enemies, items   │         │  - nametable select        │
└──────────────────────────┘         │  - fine Y scroll           │
                                     │ fine_x (3-bit)             │
                                     └────────────────────────────┘
```

**What lives where for SMB:**
- Ground, pipes, bricks, question blocks, stairs: nametable (background tiles).
- Mario, enemies, mushrooms, fireballs: OAM (sprites).

## Design

### What Changes

Replace the palette-histogram visual input (21x21x10 = 4,410 floats) with a tile-embedding
visual input derived from the nametable.

### What Stays the Same

- RNN v2 hidden layers (H1=64, H2=32, recurrent with learned leak alpha).
- Controller feedback inputs (previous control x/y, previous A/B).
- Body state inputs (facing, self-view position, velocity, on_ground).
- `special_senses[32]` for RAM-derived features (enemy proximity, speed, powerup, etc.).
- Output layer (4 outputs: x, y, A, B).
- Evolutionary training (genome = flat weight array, mutation-only).

### Observation

The visible NES screen first produces a 32x28 screen-space grid of tile tokens, derived
from the nametable and pattern table. The player-relative observation copies those tokens
into a larger 63x55 grid so every visible tile can be represented no matter where the
player is on screen.

The visible NES screen is 256x224 pixels = 32x28 tiles. The nametable stores tile IDs for
each position. Each tile ID is resolved through the current CHR bank state to its 16-byte
pattern, then hashed to produce a stable token (see Tile Tokenization below). Using the
scroll registers (V, fine_x) and the player's screen position from RAM, visible screen
tiles are re-indexed relative to the player:

```
     Screen-space tokens, Mario at tile column 16, tile row 20
     ┌──────────────────────────────────────┐
     │          32 columns                  │
     │     ┌─────────┐                      │
     │     │  Mario  │                      │
     │     │ (16,20) │ 28 rows              │
     │     └─────────┘                      │
     │                                      │
     └──────────────────────────────────────┘
             ↓ re-index relative to Mario
     ┌────────────────────────────────────────────────────────┐
     │  63 columns, 55 rows                                  │
     │  anchor column 31, row 27 = Mario                     │
     │  relative column = 31 + screen_column - player_column │
     │  relative row    = 27 + screen_row    - player_row    │
     └────────────────────────────────────────────────────────┘
```

Cells in the 63x55 relative grid that do not correspond to a visible screen tile map to a
reserved "void" token (token 0). This is larger than the visible screen, but it preserves
all visible tiles even if the player is at an edge or corner.

### Tile Tokenization

Raw tile IDs (0-255) are not used directly as tokens because they are unstable across CHR
bank switches. Games using mappers with bank switching (MMC1, MMC3, etc.) can change which
pattern data a tile ID points to at any time.

Instead, each tile ID is resolved to its actual CHR pattern and hashed:

```
nametable tile ID
    │
    ▼ resolve through current CHR bank state
16-byte CHR pattern (two 8-byte bitplanes)
    │
    ▼ hash
tile token (0 to TILE_VOCAB_SIZE-1)
    │
    ▼ embedding lookup
D-dimensional float vector
```

A typical NES game uses 100-300 visually unique tile patterns across all its CHR banks. A
vocabulary of 512 entries covers any game comfortably. Token 0 is reserved for "void" (tiles
outside the nametable).

**Implementation:** Maintain a 256-entry remap table (`tile_id → token`) for the current
bank state. Recompute it when CHR banks change (infrequent — level transitions, area
changes). Per-frame cost is a single array lookup per tile position.

### Tile Embedding

Each tile token (0 to TILE_VOCAB_SIZE-1) is mapped to a D-dimensional float vector via a
learned embedding table. The table is part of the genome and evolved alongside all other
weights.

```
tile_embedding[TILE_VOCAB_SIZE][D]    (TILE_VOCAB_SIZE = 512, D = 4 initially)

token 42  → [0.2, -0.8, 0.1, 0.5]   ← learned: "solid ground"
token 187 → [0.3, -0.7, 0.0, 0.6]   ← learned: "also solid"
token 3   → [-0.9, 0.4, 0.8, -0.2]  ← learned: "empty sky"
```

The network discovers tile semantics from training reward alone. No game-specific tile
classification is needed. Because tokens are derived from CHR patterns, the same visual
tile always gets the same token regardless of tile ID, CHR bank state, or palette.

### Network Architecture

```
Tile tokens [55][63]
    │
    ▼ embedding lookup (tile_embedding[512][D])
Embedded grid [55][63][D]
    │
    ▼ flatten
Visual input (55 * 63 * D = 13,860 floats at D=4)
    │
    ├── + body state (6 floats)
    ├── + control feedback (4 floats)
    ├── + special_senses (32 floats)
    ├── + energy, health (2 floats)
    │
    ▼
Input buffer (13,904 floats)
    │
    ▼ W_xh1 (13,904 x 64) + W_h1h1 (64 x 64) + b_h1
H1 state (64, recurrent, learned leak alpha, learned leaky ReLU)
    │
    ▼ W_h1h2 (64 x 32) + W_h2h2 (32 x 32) + b_h2
H2 state (32, recurrent, learned leak alpha, learned leaky ReLU)
    │
    ▼ W_h2o (32 x 4) + b_o
Output (4: x, y, A, B)
```

### Genome Layout

The embedding table is a separate segment so mutation can target tile semantics independently
from spatial connection weights.

```cpp
return GenomeLayout{
    .segments = {
        { "tile_embedding", TILE_VOCAB_SIZE * EMBED_DIM },
        { "input_h1", EMBEDDED_INPUT_SIZE * H1_SIZE },
        { "h1_recurrent", W_H1H1_SIZE + B_H1_SIZE + ALPHA1_LOGIT_SIZE + 1 },
        { "h1_to_h2", W_H1H2_SIZE },
        { "h2_recurrent", W_H2H2_SIZE + B_H2_SIZE + ALPHA2_LOGIT_SIZE + 1 },
        { "output", W_H2O_SIZE + B_O_SIZE },
    },
};
```

### Parameter Count

| Component | Current (V2) | Tile Embedding |
|-----------|-------------|----------------|
| Visual input | 4,410 (21x21x10 histogram) | 13,860 (63x55x4 embedded) |
| Embedding table | -- | 2,048 (512 x 4) |
| Scalar inputs | 44 | 44 |
| W_xh1 | 285,056 (4,454 x 64) | 889,856 (13,904 x 64) |
| Rest of network | ~7,494 | ~7,494 |
| **Total genome** | **~292,550** | **~899,398** |

## Enemies

For v1, enemies are not represented in the tile map. Enemy information comes through the
existing `special_senses` channel, which carries RAM-derived data including nearest enemy
dx/dy, enemy presence flags, and other game-specific state from
`NesSuperMarioBrosRamExtractor`.

The tile map provides static terrain structure. The special senses provide dynamic actor
information. The recurrent state lets the network integrate both over time.

## Data Extraction

The nametable data, CHR pattern data, scroll registers, and PPU control need to be exposed
from the SmolNES runtime backend. These are existing global variables in `deobfuscated.c`.
Extraction is a memcpy under the existing snapshot lock, alongside the current CPU RAM and
palette frame copies.

New runtime backend additions:
- `vram[2048]`: nametable RAM (background tile IDs + attribute tables).
- `chrrom` + `chr[8]` + `chrbits`: CHR pattern data and current bank mapping. Needed to
  resolve tile IDs to their 16-byte patterns for hashing.
- `V` (uint16_t): scroll position register.
- `fine_x` (uint8_t): fine X scroll offset.
- `ppuctrl` (uint8_t): pattern table selection, sprite size.

The tile-ID-to-token remap table (256 entries) is computed from the CHR bank state. It can
be recomputed whenever CHR banks change, or lazily rebuilt each frame (256 tile lookups +
hashes is negligible). The per-frame observation then just indexes this table for each of
the 896 visible tile positions.

## Implementation Status

### Completed

- Added PPU snapshot extraction from the SmolNES runtime backend.
- Added `NesTileFrame`, which extracts visible 32x28 background tile IDs, pattern hashes,
  and grayscale pattern pixels.
- Added `NesTileTokenizer`, which maps stable tile-pattern hashes to bounded token IDs.
- Added `NesTileTokenFrame`, which converts a screen-space tile frame into screen-space
  tile tokens using a persistent tokenizer.
- Added `NesPlayerRelativeTileFrame`, which maps the visible 32x28 token frame into a
  lossless 63x55 player-relative token frame.
- Added focused unit tests for tile extraction, tokenization, token-frame conversion, and
  lossless player-relative remapping.
- Added a disabled SMB diagnostic test that writes PNG comparisons for normal pixels,
  grayscale pattern pixels, screen-space tokens, and player-relative tokens.

### Current Diagnostic

Run the SMB diagnostic with:

```bash
cd apps
./build-debug/bin/dirtsim-tests-diagnostic \
  --gtest_also_run_disabled_tests \
  --gtest_filter='NesSuperMarioBrosTileProbeTest.DISABLED_SavesTileComparisonPngs'
```

It writes:

```text
/tmp/nes_smb_tile_probe_300.png
/tmp/nes_smb_tile_probe_400.png
/tmp/nes_smb_tile_probe_500.png
/tmp/nes_smb_tile_probe_700.png
/tmp/nes_smb_tile_probe_899.png
```

Each PNG currently has four panels:

- Normal RGB565 video frame.
- Palette-independent grayscale background pattern pixels.
- Screen-space tile-token rendering.
- Expanded 63x55 player-relative tile-token rendering.

### Next Step

Split the NES tile observation and brain path away from generic duck sensory data. This
keeps non-NES scenarios on the existing `DuckSensoryData` and
`DuckNeuralNetRecurrentBrainV2` path while allowing NES scenarios to use tile-specific
inputs and genome layouts.

Planned pieces:

- Add a NES-only observation type that carries the 63x55 token frame and scalar NES inputs.
- Add a NES-only recurrent tile brain variant with an embedding-table genome segment.
- Register the new brain kind separately in `TrainingBrainRegistry`.
- Wire NES training to select the NES tile brain explicitly, without changing the existing
  palette RNN v2 brain.
- Keep the palette RNN v2 path available for comparison runs and for non-NES scenarios.

### Tests For Next Step

- Genome layout reports the expected named segments and total size.
- Random genome generation exactly matches the layout size.
- Incompatible genome sizes fail compatibility checks.
- Token embedding lookup maps token 0 to the void embedding and non-zero tokens to their
  expected embedding rows.
- A hand-authored tiny genome produces deterministic controller outputs from a synthetic
  NES tile observation.
- NES training registry can construct the new brain kind without affecting existing brain
  kinds.

### Open Decisions

- Initial implementation will use the dense option: `63 * 55 * 4 = 13,860` embedded visual
  inputs directly into H1.
- Initial embedding dimension is 4.
- Token vocabulary size remains 512 with token 0 reserved for void.
- Sprites are not represented in the tile grid for v1; dynamic actors remain in
  `special_senses`.
- If dense training is too slow or unstable, revisit shared encoders or region pooling as a
  later brain variant rather than blocking the first end-to-end implementation.

## Future Work

### Sprite Overlay

Add sprite occupancy and/or sprite tile embeddings to the spatial grid, giving the network
direct spatial awareness of enemy positions rather than routing through special_senses.
