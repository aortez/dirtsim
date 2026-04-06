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

The visual observation is a 32x28 grid of tile tokens, derived from the nametable and
pattern table.

The visible NES screen is 256x224 pixels = 32x28 tiles. The nametable stores tile IDs for
each position. Each tile ID is resolved through the current CHR bank state to its 16-byte
pattern, then hashed to produce a stable token (see Tile Tokenization below). Using the
scroll registers (V, fine_x) and the player's screen position from RAM, the grid is indexed
relative to the player:

```
     Mario at tile column 16, tile row 20
     ┌──────────────────────────────────────┐
     │          32 columns                  │
     │     ┌─────────┐                      │
     │     │  Mario  │                      │
     │     │ (16,20) │ 28 rows              │
     │     └─────────┘                      │
     │                                      │
     └──────────────────────────────────────┘
             ↓ re-index relative to Mario
     ┌──────────────────────────────────────┐
     │  column 0 = 16 tiles left of Mario  │
     │  column 31 = 15 tiles right of Mario│
     │  row 0 = 20 tiles above Mario       │
     │  row 27 = 7 tiles below Mario       │
     └──────────────────────────────────────┘
```

Tiles outside the nametable map to a reserved "void" token (token 0).

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
Tile tokens [28][32]
    │
    ▼ embedding lookup (tile_embedding[512][D])
Embedded grid [28][32][D]
    │
    ▼ flatten
Visual input (28 * 32 * D = 3,584 floats at D=4)
    │
    ├── + body state (6 floats)
    ├── + control feedback (4 floats)
    ├── + special_senses (32 floats)
    ├── + energy, health (2 floats)
    │
    ▼
Input buffer (3,628 floats)
    │
    ▼ W_xh1 (3,628 x 64) + W_h1h1 (64 x 64) + b_h1
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
| Visual input | 4,410 (21x21x10 histogram) | 3,584 (32x28x4 embedded) |
| Embedding table | -- | 2,048 (512 x 4) |
| Scalar inputs | 44 | 44 |
| W_xh1 | 285,056 (4,454 x 64) | 232,192 (3,628 x 64) |
| Rest of network | ~7,494 | ~7,494 |
| **Total genome** | **~292,550** | **~241,734** |

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

## Future Work

### Sprite Overlay

Add sprite occupancy and/or sprite tile embeddings to the spatial grid, giving the network
direct spatial awareness of enemy positions rather than routing through special_senses.
