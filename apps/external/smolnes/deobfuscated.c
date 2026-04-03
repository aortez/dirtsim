#include <SDL2/SDL.h>
#include <stdint.h>

#ifndef SMOLNES_TLS
#define SMOLNES_TLS
#endif

#ifndef SMOLNES_FRAME_EXEC_BEGIN
#define SMOLNES_FRAME_EXEC_BEGIN()
#endif

#ifndef SMOLNES_FRAME_EXEC_END
#define SMOLNES_FRAME_EXEC_END()
#endif

#ifndef SMOLNES_FRAME_SUBMIT_BEGIN
#define SMOLNES_FRAME_SUBMIT_BEGIN()
#endif

#ifndef SMOLNES_FRAME_SUBMIT_END
#define SMOLNES_FRAME_SUBMIT_END()
#endif

#ifndef SMOLNES_EVENT_POLL_BEGIN
#define SMOLNES_EVENT_POLL_BEGIN()
#endif

#ifndef SMOLNES_EVENT_POLL_END
#define SMOLNES_EVENT_POLL_END()
#endif

#ifndef SMOLNES_CPU_STEP_BEGIN
#define SMOLNES_CPU_STEP_BEGIN()
#endif

#ifndef SMOLNES_CPU_STEP_END
#define SMOLNES_CPU_STEP_END()
#endif

#ifndef SMOLNES_PPU_STEP_BEGIN
#define SMOLNES_PPU_STEP_BEGIN()
#endif

#ifndef SMOLNES_PPU_STEP_END
#define SMOLNES_PPU_STEP_END()
#endif

#ifndef SMOLNES_PPU_PHASE_SET
#define SMOLNES_PPU_PHASE_SET(phase)
#endif

#ifndef SMOLNES_PPU_PHASE_CLEAR
#define SMOLNES_PPU_PHASE_CLEAR()
#endif

#ifndef SMOLNES_PPU_PHASE_SET_IF_ACTIVE
#define SMOLNES_PPU_PHASE_SET_IF_ACTIVE(phase)
#endif

#ifndef SMOLNES_PPU_PHASE_VISIBLE_PIXELS
#define SMOLNES_PPU_PHASE_VISIBLE_PIXELS 1u
#endif

#ifndef SMOLNES_PPU_PHASE_PREFETCH
#define SMOLNES_PPU_PHASE_PREFETCH 2u
#endif

#ifndef SMOLNES_PPU_PHASE_OTHER
#define SMOLNES_PPU_PHASE_OTHER 3u
#endif

#ifndef SMOLNES_PPU_PHASE_SPRITE_EVAL
#define SMOLNES_PPU_PHASE_SPRITE_EVAL 4u
#endif

#ifndef SMOLNES_PPU_PHASE_POST_VISIBLE
#define SMOLNES_PPU_PHASE_POST_VISIBLE 5u
#endif

#ifndef SMOLNES_PPU_PHASE_NON_VISIBLE_SCANLINES
#define SMOLNES_PPU_PHASE_NON_VISIBLE_SCANLINES 6u
#endif

#ifndef SMOLNES_PPU_VISIBLE_BG_ONLY_STATS
#define SMOLNES_PPU_VISIBLE_BG_ONLY_STATS(span_pixels, scalar_pixels, batched_pixels, batch_count)
#endif

#ifndef SMOLNES_APU_WRITE
#define SMOLNES_APU_WRITE(addr, value)
#endif

#ifndef SMOLNES_APU_READ
#define SMOLNES_APU_READ(addr) 0
#endif

#ifndef SMOLNES_APU_CLOCK
#define SMOLNES_APU_CLOCK(cycles)
#endif

#ifndef SMOLNES_APU_CLOCK_BEGIN
#define SMOLNES_APU_CLOCK_BEGIN()
#endif

#ifndef SMOLNES_APU_CLOCK_END
#define SMOLNES_APU_CLOCK_END()
#endif

#ifndef SMOLNES_PIXEL_OUTPUT
#define SMOLNES_PIXEL_OUTPUT(offset, color, palette) \
    do { \
        uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0]; \
        frame_buffer_palette[offset] = palette_index; \
        frame_buffer[offset] = nes_palette_rgb565[palette_index]; \
    } while (0)
#endif

#ifndef SMOLNES_PIXEL_OUTPUT_ENABLED
#define SMOLNES_PIXEL_OUTPUT_ENABLED 1
#endif

#ifndef SMOLNES_RGBA_OUTPUT_ENABLED
#define SMOLNES_RGBA_OUTPUT_ENABLED 1
#endif

#define PULL mem(++S, 1, 0, 0)
#define PUSH(x) mem(S--, 1, x, 1)

SMOLNES_TLS uint8_t *rom, *chrrom,                // Points to the start of PRG/CHR ROM
    prg[4], chr[8],                   // Current PRG/CHR banks
    prgbits = 14, chrbits = 12,       // Number of bits per PRG/CHR bank
    A, X, Y, P = 4, S = ~2, PCH, PCL, // CPU Registers
    addr_lo, addr_hi,                 // Current instruction address
    nomem,  // 1 => current instruction doesn't write to memory
    result, // Temp variable
    val,    // Current instruction value
    cross,  // 1 => page crossing occurred
    tmp,    // Temp variables
    ppumask, ppuctrl, ppustatus, // PPU registers
    ppubuf,                      // PPU buffered reads
    W,                           // Write toggle PPU register
    fine_x,                      // X fine scroll offset, 0..7
    opcode,                      // Current instruction opcode
    nmi_irq,                     // 1 => IRQ occurred
                                 // 4 => NMI occurred
    ntb,                         // Nametable byte
    ptb_lo,                      // Pattern table lowbyte
    vram[2048],                  // Nametable RAM
    palette_ram[64],             // Palette RAM
    ram[8192],                   // CPU RAM
    chrram[8192],                // CHR RAM (only used for some games)
    prgram[8192],                // PRG RAM (only used for some games)
    oam[256],                    // Object Attribute Memory (sprite RAM)
    mask[] = {128, 64, 1, 2,     // Masks used in branch instructions
              1,   0,  0, 1, 4, 0, 0, 4, 0,
              0,   64, 0, 8, 0, 0, 8}, // Masks used in SE*/CL* instructions.
    keys,                              // Joypad shift register
    mirror,                            // Current mirroring mode
    mmc1_bits, mmc1_data, mmc1_ctrl,   // Mapper 1 (MMC1) registers
    mmc3_chrprg[8], mmc3_bits,         // Mapper 4 (MMC3) registers
    mmc3_irq, mmc3_latch,              //
    chrbank0, chrbank1, prgbank,       // Current PRG/CHR bank
    rombuf[1024 * 1024],               // Buffer to read ROM file into
    *key_state;

SMOLNES_TLS uint16_t scany,          // Scanline Y
    T, V,                // "Loopy" PPU registers
    sum,                 // Sum used for ADC/SBC
    dot,                 // Horizontal position of PPU, from 0..340
    atb,                 // Attribute byte
    shift_hi, shift_lo,  // Pattern table shift registers
    cycles,              // Cycle count for current instruction
    frame_buffer[61440]; // 256x240 pixel frame buffer. Top and bottom 8 rows
                         // are not drawn.

SMOLNES_TLS uint8_t frame_buffer_palette[61440];

SMOLNES_TLS int shift_at = 0;

SMOLNES_TLS uint16_t scanline_fb_offset;

// 2C02G hardware-measured NES palette converted to RGB565.
// Source: Ricoh 2C02G PPU NTSC decode (Mesen/nesdev wiki reference).
static const uint16_t nes_palette_rgb565[64] = {
    25388,   365,  4367, 14511, 24684, 28743, 28801, 22720,
    12608,  2464,   480,   482,   456,     0,     0,     0,
    44405,  4886, 17049, 31257, 41397, 47534, 47590, 39488,
    27360, 15200,  2976,   967,   911,     0,     0,     0,
    65535, 23967, 36127, 50335, 62526, 64535, 64591, 60617,
    48453, 34246, 22058, 15953, 17945, 19049,     0,     0,
    65535, 48895, 52927, 59039, 65151, 65116, 65145, 63158,
    59092, 53013, 46902, 44857, 44860, 46518,     0,     0};

SMOLNES_TLS uint8_t scanline_sprite_pixels[256];
SMOLNES_TLS uint8_t scanline_has_sprite_pixels;

// Read a byte from CHR ROM or CHR RAM.
static inline uint8_t *get_chr_byte(uint16_t a) {
  return &chrrom[chr[a >> chrbits] << chrbits | a & ((1 << chrbits) - 1)];
}

// Read a byte from nametable RAM.
static inline uint8_t *get_nametable_byte(uint16_t a) {
  return &vram[mirror == 0   ? a % 1024                  // single bank 0
               : mirror == 1 ? a % 1024 + 1024           // single bank 1
               : mirror == 2 ? a & 2047                  // vertical mirroring
                             : a / 2 & 1024 | a % 1024]; // horizontal mirroring
}

static void evaluate_scanline_sprites(void) {
  memset(scanline_sprite_pixels, 0, sizeof(scanline_sprite_pixels));
  scanline_has_sprite_pixels = 0;
  uint16_t sprite_h = ppuctrl & 32 ? 16 : 8;
  for (uint8_t *sprite = oam; sprite < oam + 256; sprite += 4) {
    uint16_t sprite_y = scany - sprite[0] - 1;
    if (sprite_y >= sprite_h)
      continue;

    uint16_t sy = sprite_y ^ (sprite[2] & 128 ? sprite_h - 1 : 0);
    uint16_t sprite_tile = sprite[1];
    uint16_t sprite_addr =
        (ppuctrl & 32
             ? sprite_tile % 2 << 12 |
                   sprite_tile << 4 & -32 | sy * 2 & 16
             : (ppuctrl & 8) << 9 | sprite_tile << 4) |
        sy & 7;

    uint8_t pattern_lo = *get_chr_byte(sprite_addr);
    uint8_t pattern_hi = *get_chr_byte(sprite_addr + 8);

    if (sprite[2] & 64) {
      pattern_lo = (pattern_lo & 0xF0) >> 4 | (pattern_lo & 0x0F) << 4;
      pattern_lo = (pattern_lo & 0xCC) >> 2 | (pattern_lo & 0x33) << 2;
      pattern_lo = (pattern_lo & 0xAA) >> 1 | (pattern_lo & 0x55) << 1;
      pattern_hi = (pattern_hi & 0xF0) >> 4 | (pattern_hi & 0x0F) << 4;
      pattern_hi = (pattern_hi & 0xCC) >> 2 | (pattern_hi & 0x33) << 2;
      pattern_hi = (pattern_hi & 0xAA) >> 1 | (pattern_hi & 0x55) << 1;
    }

    const uint8_t sprite_attr = sprite[2];
    const uint8_t is_sprite0 = (sprite == oam);
    for (uint16_t sprite_x = 0; sprite_x < 8; ++sprite_x) {
      uint16_t screen_x = sprite[3] + sprite_x;
      if (screen_x >= 256)
        break;

      uint8_t offset = 7 - sprite_x;
      uint8_t sprite_color =
          (pattern_hi >> offset & 1) << 1 |
          (pattern_lo >> offset & 1);
      if (!sprite_color || scanline_sprite_pixels[screen_x])
        continue;
      scanline_sprite_pixels[screen_x] =
          sprite_color | ((sprite_attr & 3) << 2) |
          (sprite_attr & 32 ? 16 : 0) |
          (is_sprite0 ? 32 : 0);
      scanline_has_sprite_pixels = 1;
    }
  }
}

static inline void step_background_fetch_pipeline_for_dot(uint16_t bg_pattern_base,
                                                          uint16_t current_dot,
                                                          uint16_t *local_V,
                                                          uint16_t *local_atb,
                                                          uint16_t *local_shift_hi,
                                                          uint16_t *local_shift_lo,
                                                          int *local_shift_at,
                                                          uint8_t *local_ntb,
                                                          uint8_t *local_ptb_lo) {
  switch (current_dot & 7) {
  case 1:
    *local_ntb = *get_nametable_byte(*local_V);
    break;
  case 3:
    *local_atb = (*get_nametable_byte(*local_V & 0xc00 | 0x3c0 | *local_V >> 4 & 0x38 |
                                      *local_V / 4 & 7) >>
                  (*local_V >> 5 & 2 | *local_V / 2 & 1) * 2) %
                 4 * 0x5555;
    break;
  case 5: {
    const int temp = bg_pattern_base | *local_ntb << 4 | *local_V >> 12;
    *local_ptb_lo = *get_chr_byte(temp);
    break;
  }
  case 7: {
    const int temp = bg_pattern_base | *local_ntb << 4 | *local_V >> 12;
    const uint8_t ptb_hi = *get_chr_byte(temp | 8);
    *local_V = (*local_V & 31) == 31 ? *local_V & ~31 ^ 1024 : *local_V + 1;
    *local_shift_hi |= ptb_hi;
    *local_shift_lo |= *local_ptb_lo;
    *local_shift_at |= *local_atb;
    break;
  }
  }
}

static inline void step_background_fetch_pipeline_for_dot_aligned(
    uint16_t bg_pattern_base,
    uint16_t current_dot,
    uint16_t *local_V,
    uint16_t *local_atb,
    uint32_t *local_shift_hi_aligned,
    uint32_t *local_shift_lo_aligned,
    uint64_t *local_shift_at_aligned,
    uint8_t *local_ntb,
    uint8_t *local_ptb_lo,
    uint8_t fine_x) {
  switch (current_dot & 7) {
  case 1:
    *local_ntb = *get_nametable_byte(*local_V);
    break;
  case 3:
    *local_atb = (*get_nametable_byte(*local_V & 0xc00 | 0x3c0 | *local_V >> 4 & 0x38 |
                                      *local_V / 4 & 7) >>
                  (*local_V >> 5 & 2 | *local_V / 2 & 1) * 2) %
                 4 * 0x5555;
    break;
  case 5: {
    const int temp = bg_pattern_base | *local_ntb << 4 | *local_V >> 12;
    *local_ptb_lo = *get_chr_byte(temp);
    break;
  }
  case 7: {
    const int temp = bg_pattern_base | *local_ntb << 4 | *local_V >> 12;
    const uint8_t ptb_hi = *get_chr_byte(temp | 8);
    *local_V = (*local_V & 31) == 31 ? *local_V & ~31 ^ 1024 : *local_V + 1;
    *local_shift_hi_aligned |= (uint32_t)ptb_hi << fine_x;
    *local_shift_lo_aligned |= (uint32_t)(*local_ptb_lo) << fine_x;
    *local_shift_at_aligned |= (uint64_t)(uint32_t)(*local_atb) << (fine_x * 2);
    break;
  }
  }
}

static inline void render_visible_span_background_only(uint16_t span_count,
                                                       uint8_t fine_x,
                                                       uint16_t bg_pattern_base) {
  const uint16_t total_span_count = span_count;
  uint16_t current_dot = dot;
  uint16_t local_V = V;
  uint16_t local_atb = atb;
  uint32_t local_shift_hi_aligned = (uint32_t)shift_hi << fine_x;
  uint32_t local_shift_lo_aligned = (uint32_t)shift_lo << fine_x;
  uint64_t local_shift_at_aligned = (uint64_t)(uint32_t)shift_at << (fine_x * 2);
  uint8_t local_ntb = ntb;
  uint8_t local_ptb_lo = ptb_lo;
  uint16_t prefix_count = (8 - (current_dot & 7)) & 7;
  uint16_t scalar_pixels = 0;
  uint16_t batched_pixels = 0;
  uint16_t batched_calls = 0;
  if (prefix_count > span_count)
    prefix_count = span_count;
  scalar_pixels += prefix_count;

  while (prefix_count > 0) {
    uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                    local_shift_lo_aligned >> 15 & 1,
            palette = local_shift_at_aligned >> 28 & 12;

    if (SMOLNES_PIXEL_OUTPUT_ENABLED) {
      const uint16_t output_offset = scanline_fb_offset + current_dot;
      const uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0];
      frame_buffer_palette[output_offset] = palette_index;
      if (SMOLNES_RGBA_OUTPUT_ENABLED)
        frame_buffer[output_offset] = nes_palette_rgb565[palette_index];
    }

    local_shift_hi_aligned <<= 1;
    local_shift_lo_aligned <<= 1;
    local_shift_at_aligned <<= 2;
    step_background_fetch_pipeline_for_dot_aligned(
        bg_pattern_base, current_dot, &local_V, &local_atb,
        &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
        &local_ntb, &local_ptb_lo, fine_x);

    ++current_dot;
    --prefix_count;
    --span_count;
  }

  if (!SMOLNES_PIXEL_OUTPUT_ENABLED) {
    while (span_count >= 8) {
      batched_pixels += 8;
      batched_calls++;
      for (uint16_t pixel = 0; pixel < 8; ++pixel) {
        uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                        local_shift_lo_aligned >> 15 & 1,
                palette = local_shift_at_aligned >> 28 & 12;

        local_shift_hi_aligned <<= 1;
        local_shift_lo_aligned <<= 1;
        local_shift_at_aligned <<= 2;
      }

      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 1, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 3, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 5, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 7, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);

      current_dot += 8;
      span_count -= 8;
    }
  } else if (SMOLNES_RGBA_OUTPUT_ENABLED) {
    while (span_count >= 8) {
      batched_pixels += 8;
      batched_calls++;
      const uint16_t base_offset = scanline_fb_offset + current_dot;
      for (uint16_t pixel = 0; pixel < 8; ++pixel) {
        uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                        local_shift_lo_aligned >> 15 & 1,
                palette = local_shift_at_aligned >> 28 & 12;

        const uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0];
        frame_buffer_palette[base_offset + pixel] = palette_index;
        frame_buffer[base_offset + pixel] = nes_palette_rgb565[palette_index];

        local_shift_hi_aligned <<= 1;
        local_shift_lo_aligned <<= 1;
        local_shift_at_aligned <<= 2;
      }

      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 1, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 3, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 5, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 7, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);

      current_dot += 8;
      span_count -= 8;
    }
  } else {
    while (span_count >= 8) {
      batched_pixels += 8;
      batched_calls++;
      const uint16_t base_offset = scanline_fb_offset + current_dot;
      for (uint16_t pixel = 0; pixel < 8; ++pixel) {
        uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                        local_shift_lo_aligned >> 15 & 1,
                palette = local_shift_at_aligned >> 28 & 12;

        frame_buffer_palette[base_offset + pixel] =
            palette_ram[(color) ? (palette) | (color) : 0];

        local_shift_hi_aligned <<= 1;
        local_shift_lo_aligned <<= 1;
        local_shift_at_aligned <<= 2;
      }

      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 1, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 3, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 5, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 7, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);

      current_dot += 8;
      span_count -= 8;
    }
  }

  scalar_pixels += span_count;
  while (span_count > 0) {
    uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                    local_shift_lo_aligned >> 15 & 1,
            palette = local_shift_at_aligned >> 28 & 12;

    if (SMOLNES_PIXEL_OUTPUT_ENABLED) {
      const uint16_t output_offset = scanline_fb_offset + current_dot;
      const uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0];
      frame_buffer_palette[output_offset] = palette_index;
      if (SMOLNES_RGBA_OUTPUT_ENABLED)
        frame_buffer[output_offset] = nes_palette_rgb565[palette_index];
    }

    local_shift_hi_aligned <<= 1;
    local_shift_lo_aligned <<= 1;
    local_shift_at_aligned <<= 2;
    step_background_fetch_pipeline_for_dot_aligned(
        bg_pattern_base, current_dot, &local_V, &local_atb,
        &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
        &local_ntb, &local_ptb_lo, fine_x);

    ++current_dot;
    --span_count;
  }

  V = local_V;
  atb = local_atb;
  shift_hi = local_shift_hi_aligned >> fine_x;
  shift_lo = local_shift_lo_aligned >> fine_x;
  shift_at = local_shift_at_aligned >> (fine_x * 2);
  ntb = local_ntb;
  ptb_lo = local_ptb_lo;
  SMOLNES_PPU_VISIBLE_BG_ONLY_STATS(
      total_span_count, scalar_pixels, batched_pixels, batched_calls);
}

static inline void render_visible_span(uint16_t span_count,
                                       uint8_t fine_x,
                                       uint16_t bg_pattern_base) {
  if (!scanline_has_sprite_pixels) {
    render_visible_span_background_only(span_count, fine_x, bg_pattern_base);
    return;
  }

  uint16_t current_dot = dot;
  uint16_t local_V = V;
  uint16_t local_atb = atb;
  uint32_t local_shift_hi_aligned = (uint32_t)shift_hi << fine_x;
  uint32_t local_shift_lo_aligned = (uint32_t)shift_lo << fine_x;
  uint64_t local_shift_at_aligned = (uint64_t)(uint32_t)shift_at << (fine_x * 2);
  uint8_t local_ntb = ntb;
  uint8_t local_ptb_lo = ptb_lo;
  uint16_t prefix_count = (8 - (current_dot & 7)) & 7;
  if (prefix_count > span_count)
    prefix_count = span_count;

  while (prefix_count > 0) {
    uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                    local_shift_lo_aligned >> 15 & 1,
            palette = local_shift_at_aligned >> 28 & 12;

    const uint8_t sprite = scanline_sprite_pixels[current_dot];
    if (sprite) {
      if (!(sprite & 16 && color)) {
        color = sprite & 3;
        palette = 16 | (sprite & 12);
      }
      if (sprite & 32 && color)
        ppustatus |= 64;
    }

    if (SMOLNES_PIXEL_OUTPUT_ENABLED) {
      const uint16_t output_offset = scanline_fb_offset + current_dot;
      const uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0];
      frame_buffer_palette[output_offset] = palette_index;
      if (SMOLNES_RGBA_OUTPUT_ENABLED)
        frame_buffer[output_offset] = nes_palette_rgb565[palette_index];
    }

    local_shift_hi_aligned <<= 1;
    local_shift_lo_aligned <<= 1;
    local_shift_at_aligned <<= 2;
    step_background_fetch_pipeline_for_dot_aligned(
        bg_pattern_base, current_dot, &local_V, &local_atb,
        &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
        &local_ntb, &local_ptb_lo, fine_x);

    ++current_dot;
    --prefix_count;
    --span_count;
  }

  if (!SMOLNES_PIXEL_OUTPUT_ENABLED) {
    while (span_count >= 8) {
      for (uint16_t pixel = 0; pixel < 8; ++pixel) {
        uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                        local_shift_lo_aligned >> 15 & 1,
                palette = local_shift_at_aligned >> 28 & 12;

        const uint8_t sprite = scanline_sprite_pixels[current_dot + pixel];
        if (sprite) {
          if (!(sprite & 16 && color)) {
            color = sprite & 3;
            palette = 16 | (sprite & 12);
          }
          if (sprite & 32 && color)
            ppustatus |= 64;
        }

        local_shift_hi_aligned <<= 1;
        local_shift_lo_aligned <<= 1;
        local_shift_at_aligned <<= 2;
      }

      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 1, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 3, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 5, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 7, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);

      current_dot += 8;
      span_count -= 8;
    }
  } else if (SMOLNES_RGBA_OUTPUT_ENABLED) {
    while (span_count >= 8) {
      const uint16_t base_offset = scanline_fb_offset + current_dot;
      for (uint16_t pixel = 0; pixel < 8; ++pixel) {
        uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                        local_shift_lo_aligned >> 15 & 1,
                palette = local_shift_at_aligned >> 28 & 12;

        const uint8_t sprite = scanline_sprite_pixels[current_dot + pixel];
        if (sprite) {
          if (!(sprite & 16 && color)) {
            color = sprite & 3;
            palette = 16 | (sprite & 12);
          }
          if (sprite & 32 && color)
            ppustatus |= 64;
        }

        const uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0];
        frame_buffer_palette[base_offset + pixel] = palette_index;
        frame_buffer[base_offset + pixel] = nes_palette_rgb565[palette_index];

        local_shift_hi_aligned <<= 1;
        local_shift_lo_aligned <<= 1;
        local_shift_at_aligned <<= 2;
      }

      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 1, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 3, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 5, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 7, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);

      current_dot += 8;
      span_count -= 8;
    }
  } else {
    while (span_count >= 8) {
      const uint16_t base_offset = scanline_fb_offset + current_dot;
      for (uint16_t pixel = 0; pixel < 8; ++pixel) {
        uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                        local_shift_lo_aligned >> 15 & 1,
                palette = local_shift_at_aligned >> 28 & 12;

        const uint8_t sprite = scanline_sprite_pixels[current_dot + pixel];
        if (sprite) {
          if (!(sprite & 16 && color)) {
            color = sprite & 3;
            palette = 16 | (sprite & 12);
          }
          if (sprite & 32 && color)
            ppustatus |= 64;
        }

        frame_buffer_palette[base_offset + pixel] =
            palette_ram[(color) ? (palette) | (color) : 0];

        local_shift_hi_aligned <<= 1;
        local_shift_lo_aligned <<= 1;
        local_shift_at_aligned <<= 2;
      }

      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 1, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 3, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 5, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);
      step_background_fetch_pipeline_for_dot_aligned(
          bg_pattern_base, current_dot + 7, &local_V, &local_atb,
          &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
          &local_ntb, &local_ptb_lo, fine_x);

      current_dot += 8;
      span_count -= 8;
    }
  }

  while (span_count > 0) {
    uint8_t color = local_shift_hi_aligned >> 14 & 2 |
                    local_shift_lo_aligned >> 15 & 1,
            palette = local_shift_at_aligned >> 28 & 12;

    const uint8_t sprite = scanline_sprite_pixels[current_dot];
    if (sprite) {
      if (!(sprite & 16 && color)) {
        color = sprite & 3;
        palette = 16 | (sprite & 12);
      }
      if (sprite & 32 && color)
        ppustatus |= 64;
    }

    if (SMOLNES_PIXEL_OUTPUT_ENABLED) {
      const uint16_t output_offset = scanline_fb_offset + current_dot;
      const uint8_t palette_index = palette_ram[(color) ? (palette) | (color) : 0];
      frame_buffer_palette[output_offset] = palette_index;
      if (SMOLNES_RGBA_OUTPUT_ENABLED)
        frame_buffer[output_offset] = nes_palette_rgb565[palette_index];
    }

    local_shift_hi_aligned <<= 1;
    local_shift_lo_aligned <<= 1;
    local_shift_at_aligned <<= 2;
    step_background_fetch_pipeline_for_dot_aligned(
        bg_pattern_base, current_dot, &local_V, &local_atb,
        &local_shift_hi_aligned, &local_shift_lo_aligned, &local_shift_at_aligned,
        &local_ntb, &local_ptb_lo, fine_x);

    ++current_dot;
    --span_count;
  }

  V = local_V;
  atb = local_atb;
  shift_hi = local_shift_hi_aligned >> fine_x;
  shift_lo = local_shift_lo_aligned >> fine_x;
  shift_at = local_shift_at_aligned >> (fine_x * 2);
  ntb = local_ntb;
  ptb_lo = local_ptb_lo;
}

static inline void run_prefetch_dot(uint16_t bg_pattern_base) {
  if (dot < 336) {
    shift_hi <<= 1;
    shift_lo <<= 1;
    shift_at <<= 2;
  }
  step_background_fetch_pipeline_for_dot(
      bg_pattern_base, dot, &V, &atb, &shift_hi, &shift_lo, &shift_at, &ntb,
      &ptb_lo);
}

// If `write` is non-zero, writes `val` to the address `hi:lo`, otherwise reads
// a value from the address `hi:lo`.
uint8_t mem(uint8_t lo, uint8_t hi, uint8_t val, uint8_t write) {
  uint16_t addr = hi << 8 | lo;

  switch (hi >>= 4) {
  case 0: case 1: // $0000...$1fff RAM
    return write ? ram[addr] = val : ram[addr];

  case 2: case 3: // $2000..$2007 PPU (mirrored)
    lo &= 7;

    // read/write $2007
    if (lo == 7) {
      tmp = ppubuf;
      uint8_t *rom =
          // Access CHR ROM or CHR RAM
          V < 8192 ? write && chrrom != chrram ? &tmp : get_chr_byte(V)
          // Access nametable RAM
          : V < 16128 ? get_nametable_byte(V)
                      // Access palette RAM
                      : palette_ram + (uint8_t)((V & 19) == 16 ? V ^ 16 : V);
      write ? *rom = val : (ppubuf = *rom);
      V += ppuctrl & 4 ? 32 : 1;
      V %= 16384;
      return tmp;
    }

    if (write)
      switch (lo) {
      case 0: // $2000 ppuctrl
        ppuctrl = val;
        T = T & 0xf3ff | val % 4 << 10;
        break;

      case 1: // $2001 ppumask
        ppumask = val;
        break;

      case 5: // $2005 ppuscroll
        T = (W ^= 1)
          ? fine_x = val & 7, T & ~31 | val / 8
          : T & 0x8c1f | val % 8 << 12 | val * 4 & 0x3e0;
        break;

      case 6: // $2006 ppuaddr
        T = (W ^= 1)
          ? T & 0xff | val % 64 << 8
          : (V = T & ~0xff | val);
      }

    if (lo == 2) { // $2002 ppustatus
      tmp = ppustatus & 0xe0;
      ppustatus &= 0x7f;
      W = 0;
      return tmp;
    }
    break;

  case 4:
    // APU registers: $4000-$4013, $4015 (write+read), $4017 (write).
    if (write && lo <= 19) { SMOLNES_APU_WRITE(addr, val); }
    if (write && lo == 21) { SMOLNES_APU_WRITE(addr, val); }
    if (write && lo == 23) { SMOLNES_APU_WRITE(addr, val); }
    if (!write && lo == 21) { return SMOLNES_APU_READ(addr); }
    if (write && lo == 20) // $4014 OAM DMA
      for (uint16_t i = 256; i--;)
        oam[i] = mem(i, val, 0, 0);
    // $4016 Joypad 1
    for (tmp = 0, hi = 8; hi--;)
      tmp = tmp * 2 + key_state[(uint8_t[]){
                          SDL_SCANCODE_X,      // A
                          SDL_SCANCODE_Z,      // B
                          SDL_SCANCODE_TAB,    // Select
                          SDL_SCANCODE_RETURN, // Start
                          SDL_SCANCODE_UP,     // Dpad Up
                          SDL_SCANCODE_DOWN,   // Dpad Down
                          SDL_SCANCODE_LEFT,   // Dpad Left
                          SDL_SCANCODE_RIGHT,  // Dpad Right
                      }[hi]];
    if (lo == 22) {
      if (write) {
        keys = tmp;
      } else {
        tmp = keys & 1;
        keys /= 2;
        return tmp;
      }
    }
    return 0;

  case 6: case 7: // $6000...$7fff PRG RAM
    addr &= 8191;
    return write ? prgram[addr] = val : prgram[addr];

  default: // $8000...$ffff ROM
    // handle mapper writes
    if (write)
      switch (rombuf[6] >> 4) {
      case 7: // mapper 7
        mirror = !(val / 16);
        prg[0] = val % 8 * 2;
        prg[1] = prg[0] + 1;
        break;

      case 4: { // mapper 4
        uint8_t addr1 = addr & 1;
        switch (hi >> 1) {
          case 4: // Bank select/bank data
            *(addr1 ? &mmc3_chrprg[mmc3_bits & 7] : &mmc3_bits) = val;
            tmp = mmc3_bits >> 5 & 4;
            for (int i = 4; i--;) {
              chr[0 + i + tmp] = mmc3_chrprg[i / 2] & ~!(i % 2) | i % 2;
              chr[4 + i - tmp] = mmc3_chrprg[2 + i];
            }
            tmp = mmc3_bits >> 5 & 2;
            prg[0 + tmp] = mmc3_chrprg[6];
            prg[1] = mmc3_chrprg[7];
            prg[3] = rombuf[4] * 2 - 1;
            prg[2 - tmp] = prg[3] - 1;
            break;
          case 5: // Mirroring
            if (!addr1) {
              mirror = 2 + val % 2;
            }
            break;
          case 6:  // IRQ Latch
            if (!addr1) {
              mmc3_latch = val;
            }
            break;
          case 7:  // IRQ Enable
            mmc3_irq = addr1;
            break;
        }
        break;
      }

      case 3: // mapper 3
        chr[0] = val % 4 * 2;
        chr[1] = chr[0] + 1;
        break;

      case 2: // mapper 2
        prg[0] = val & 31;
        break;

      case 1: // mapper 1
        if (val & 0x80) {
          mmc1_bits = 5;
          mmc1_data = 0;
          mmc1_ctrl |= 12;
        } else if (mmc1_data = mmc1_data / 2 | val << 4 & 16, !--mmc1_bits) {
          mmc1_bits = 5;
          tmp = addr >> 13;
          *(tmp == 4 ? mirror = mmc1_data & 3, &mmc1_ctrl
          : tmp == 5 ? &chrbank0
          : tmp == 6 ? &chrbank1
                     : &prgbank) = mmc1_data;

          // Update CHR banks.
          chr[0] = chrbank0 & ~!(mmc1_ctrl & 16);
          chr[1] = mmc1_ctrl & 16 ? chrbank1 : chrbank0 | 1;

          // Update PRG banks.
          tmp = mmc1_ctrl / 4 % 4 - 2;
          prg[0] = !tmp ? 0 : tmp == 1 ? prgbank : prgbank & ~1;
          prg[1] = !tmp ? prgbank : tmp == 1 ? rombuf[4] - 1 : prgbank | 1;
        }
      }
    return rom[(prg[hi - 8 >> prgbits - 12] & (rombuf[4] << 14 - prgbits) - 1)
                   << prgbits |
               addr & (1 << prgbits) - 1];
  }

  return ~0;
}

// Read a byte at address `PCH:PCL` and increment PC.
uint8_t read_pc() {
  val = mem(PCL, PCH, 0, 0);
  !++PCL && ++PCH;
  return val;
}

// Set N (negative) and Z (zero) flags of `P` register, based on `val`.
uint8_t set_nz(uint8_t val) { return P = P & 125 | val & 128 | !val * 2; }

int main(int argc, char **argv) {
  SDL_RWops *rom_file;
  size_t rom_bytes;

  if (argc < 2 || argv[1] == 0 || !argv[1][0]) {
    return 1;
  }

  rom_file = SDL_RWFromFile(argv[1], "rb");
  if (rom_file == 0) {
    return 1;
  }

  // Zero-fill so short reads do not reuse stale bytes from prior runs.
  SDL_memset(rombuf, 0, sizeof(rombuf));
  rom_bytes = SDL_RWread(rom_file, rombuf, 1, sizeof(rombuf));
  SDL_RWclose(rom_file);
  if (rom_bytes < 16) {
    return 1;
  }

  // Start PRG0 after 16-byte header.
  rom = rombuf + 16;
  // PRG1 is the last bank. `rombuf[4]` is the number of 16k PRG banks.
  prg[1] = rombuf[4] - 1;
  // CHR0 ROM is after all PRG data in the file. `rombuf[5]` is the number of
  // 8k CHR banks. If it is zero, assume the game uses CHR RAM.
  chrrom = rombuf[5] ? rom + (rombuf[4] << 14) : chrram;
  // CHR1 is the last 4k bank.
  chr[1] = rombuf[5] ? rombuf[5] * 2 - 1 : 1;
  // Bit 0 of `rombuf[6]` is 0=>horizontal mirroring, 1=>vertical mirroring.
  mirror = 3 - rombuf[6] % 2;
  if (rombuf[6] / 16 == 4) {
    mem(0, 128, 0, 1); // Update to default mmc3 banks
    prgbits--;         // 8kb PRG banks
    chrbits -= 2;      // 1kb CHR banks
  }

  // Start at address in reset vector, at $FFFC.
  PCL = mem(~3, ~0, 0, 0);
  PCH = mem(~2, ~0, 0, 0);

  SDL_Init(SDL_INIT_VIDEO);
  key_state = (uint8_t*)SDL_GetKeyboardState(0);
  // Create window 1024x840. The framebuffer is 256x240, but we don't draw the
  // top or bottom 8 rows. Scaling up by 4x gives 1024x960, but that looks
  // squished because the NES doesn't have square pixels. So shrink it by 7/8.
  void *renderer = SDL_CreateRenderer(
      SDL_CreateWindow("smolnes", 0, 0, 1024, 840, SDL_WINDOW_SHOWN), -1,
      SDL_RENDERER_PRESENTVSYNC);
  void *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STREAMING, 256, 224);
  SMOLNES_FRAME_EXEC_BEGIN();

loop:
  SMOLNES_CPU_STEP_BEGIN();
  cycles = nomem = 0;
  if (nmi_irq)
    goto nmi_irq;

  opcode = read_pc();
  uint8_t opcodelo5 = opcode & 31;
  switch (opcodelo5) {
  case 0:
    if (opcode & 0x80) { // LDY/CPY/CPX imm
      read_pc();
      nomem = 1;
      goto nomemop;
    }

    switch (opcode >> 5) {
    case 0: { // BRK or nmi_irq
      !++PCL && ++PCH;
    nmi_irq:
      PUSH(PCH);
      PUSH(PCL);
      PUSH(P | 32);
      // BRK/IRQ vector is $ffff, NMI vector is $fffa
      uint16_t veclo = ~1 - (nmi_irq & 4);
      PCL = mem(veclo, ~0, 0, 0);
      PCH = mem(veclo + 1, ~0, 0, 0);
      nmi_irq = 0;
      cycles++;
      break;
    }

    case 1: // JSR
      result = read_pc();
      PUSH(PCH);
      PUSH(PCL);
      PCH = read_pc();
      PCL = result;
      break;

    case 2: // RTI
      P = PULL & ~32;
      PCL = PULL;
      PCH = PULL;
      break;

    case 3: // RTS
      PCL = PULL;
      PCH = PULL;
      !++PCL && ++PCH;
      break;
    }

    cycles += 4;
    break;

  case 16: // BPL, BMI, BVC, BVS, BCC, BCS, BNE, BEQ
    read_pc();
    if (!(P & mask[opcode >> 6]) ^ opcode / 32 & 1) {
      cross = PCL + (int8_t)val >> 8;
      PCH += cross;
      PCL += val;
      cycles += cross ? 2 : 1;
    }
    break;

  case 8: case 24:
    switch (opcode >>= 4) {
    case 0: // PHP
      PUSH(P | 48);
      cycles++;
      break;

    case 2: // PLP
      P = PULL & ~16;
      cycles += 2;
      break;

    case 4: // PHA
      PUSH(A);
      cycles++;
      break;

    case 6: // PLA
      set_nz(A = PULL);
      cycles += 2;
      break;

    case 8: // DEY
      set_nz(--Y);
      break;

    case 9: // TYA
      set_nz(A = Y);
      break;

    case 10: // TAY
      set_nz(Y = A);
      break;

    case 12: // INY
      set_nz(++Y);
      break;

    case 14: // INX
      set_nz(++X);
      break;

    default: // CLC, SEC, CLI, SEI, CLV, CLD, SED
      P = P & ~mask[opcode + 3] | mask[opcode + 4];
      break;
    }
    break;

  case 10: case 26:
    switch (opcode >> 4) {
    case 8: // TXA
      set_nz(A = X);
      break;

    case 9: // TXS
      S = X;
      break;

    case 10: // TAX
      set_nz(X = A);
      break;

    case 11: // TSX
      set_nz(X = S);
      break;

    case 12: // DEX
      set_nz(--X);
      break;

    case 14: // NOP
      break;

    default: // ASL/ROL/LSR/ROR A
      nomem = 1;
      val = A;
      goto nomemop;
    }
    break;

  case 1: // X-indexed, indirect
    read_pc();
    val += X;
    addr_lo = mem(val, 0, 0, 0);
    addr_hi = mem(val + 1, 0, 0, 0);
    cycles += 4;
    goto opcode;

  case 2: case 9: // Immediate
    read_pc();
    nomem = 1;
    goto nomemop;

  case 17: // Zeropage, Y-indexed
    addr_lo = mem(read_pc(), 0, 0, 0);
    addr_hi = mem(val + 1, 0, 0, 0);
    cycles++;
    goto add_x_or_y;

  case 4: case 5: case 6:     // Zeropage               +1
  case 20: case 21: case 22:  // Zeropage, X-indexed    +2
    addr_lo = read_pc();
    cross = opcodelo5 > 6;
    if (cross) {
      addr_lo += (opcode & 214) == 150 ? Y : X;  // LDX/STX use Y
    }
    addr_hi = 0;
    cycles -= !cross;
    goto opcode;

  case 12: case 13: case 14: // Absolute               +2
  case 25:                   // Absolute, Y-indexed    +2/3
  case 28: case 29: case 30: // Absolute, X-indexed    +2/3
    addr_lo = read_pc();
    addr_hi = read_pc();
    if (opcodelo5 < 25) goto opcode;
  add_x_or_y:
    val = opcodelo5 < 28 | opcode == 190 ? Y : X;
    cross = addr_lo + val > 255;
    addr_hi += cross;
    addr_lo += val;
    cycles +=
        ((opcode & 224) == 128 | opcode % 16 == 14 & opcode != 190) | cross;
  opcode:
    cycles += 2;
    if (opcode != 76 & (opcode & 224) != 128) {
      val = mem(addr_lo, addr_hi, 0, 0);
    }

  nomemop:
    result = 0;
    switch (opcode & 227) {
    case 1: set_nz(A |= val); break;  // ORA
    case 33: set_nz(A &= val); break; // AND
    case 65: set_nz(A ^= val); break; // EOR
    case 225: // SBC
      val = ~val;
      // fallthrough
    case 97: // ADC
      sum = A + val + P % 2;
      P = P & ~65 | sum > 255 | ((A ^ sum) & (val ^ sum) & 128) / 2;
      set_nz(A = sum);
      break;

    case 34: // ROL
      result = P & 1;
      // fallthrough
    case 2: // ASL
      result |= val * 2;
      P = P & ~1 | val / 128;
      goto memop;

    case 98: // ROR
      result = P << 7;
      // fallthrough
    case 66: // LSR
      result |= val / 2;
      P = P & ~1 | val & 1;
      goto memop;

    case 194: // DEC
      result = val - 1;
      goto memop;

    case 226: // INC
      result = val + 1;
      // fallthrough

    memop:
      set_nz(result);
      // Write result to A or back to memory.
      nomem ? A = result : (cycles += 2, mem(addr_lo, addr_hi, result, 1));
      break;

    case 32: // BIT
      P = P & 61 | val & 192 | !(A & val) * 2;
      break;

    case 64: // JMP
      PCL = addr_lo;
      PCH = addr_hi;
      cycles--;
      break;

    case 96: // JMP indirect
      PCL = val;
      PCH = mem(addr_lo + 1, addr_hi, 0, 0);
      cycles++;
      break;

    default: {
      uint8_t opcodehi3 = opcode / 32;
      uint8_t *reg = opcode % 4 == 2 | opcodehi3 == 7 ? &X
                     : opcode % 4 == 1                ? &A
                                                      : &Y;
      if (opcodehi3 == 4) {  // STY/STA/STX
        mem(addr_lo, addr_hi, *reg, 1);
      } else if (opcodehi3 != 5) {  // CPY/CMP/CPX
        P = P & ~1 | *reg >= val;
        set_nz(*reg - val);
      } else {  // LDY/LDA/LDX
        set_nz(*reg = val);
      }
      break;
    }
    }
  }

  // Update PPU, which runs 3 times faster than CPU. Each CPU instruction
  // takes at least 2 cycles.
  SMOLNES_CPU_STEP_END();
  SMOLNES_APU_CLOCK_BEGIN();
  SMOLNES_APU_CLOCK(cycles + 2);
  SMOLNES_APU_CLOCK_END();
  SMOLNES_PPU_STEP_BEGIN();
  const uint8_t rendering_enabled = ppumask & 24;
  const uint16_t bg_pattern_base = ppuctrl << 8 & 4096;
  uint32_t smolnes_ppu_phase = SMOLNES_PPU_PHASE_OTHER;
  if (rendering_enabled) {
    if (scany < 240) {
      if (dot < 256)
        smolnes_ppu_phase = SMOLNES_PPU_PHASE_VISIBLE_PIXELS;
      else if (dot >= 320)
        smolnes_ppu_phase = SMOLNES_PPU_PHASE_PREFETCH;
      else
        smolnes_ppu_phase = SMOLNES_PPU_PHASE_POST_VISIBLE;
    } else {
      smolnes_ppu_phase = SMOLNES_PPU_PHASE_NON_VISIBLE_SCANLINES;
    }
  }
  SMOLNES_PPU_PHASE_SET_IF_ACTIVE(smolnes_ppu_phase);
  for (tmp = cycles * 3 + 6; tmp--;) {
    if (rendering_enabled) {
      if (scany < 240) {
        if (dot < 256) {
          if (dot == 0) {
            SMOLNES_PPU_PHASE_SET_IF_ACTIVE(SMOLNES_PPU_PHASE_SPRITE_EVAL);
            scanline_fb_offset = scany * 256;
            evaluate_scanline_sprites();
            SMOLNES_PPU_PHASE_SET_IF_ACTIVE(SMOLNES_PPU_PHASE_VISIBLE_PIXELS);
          }
          uint16_t visible_span = tmp + 1;
          if (visible_span > 256 - dot)
            visible_span = 256 - dot;
          render_visible_span(visible_span, fine_x, bg_pattern_base);
          dot += visible_span - 1;
          tmp -= visible_span - 1;
        } else if (dot == 256) {
          SMOLNES_PPU_PHASE_SET_IF_ACTIVE(SMOLNES_PPU_PHASE_POST_VISIBLE);
          V = ((V & 7 << 12) != 7 << 12 ? V + 4096
               : (V & 0x3e0) == 928     ? V & 0x8c1f ^ 2048
               : (V & 0x3e0) == 0x3e0   ? V & 0x8c1f
                                        : V & 0x8c1f | V + 32 & 0x3e0) &
                  ~0x41f |
              T & 0x41f;
        } else if (dot < 320) {
          const uint16_t post_visible_start_dot = dot;
          uint16_t post_visible_span = tmp + 1;
          if (post_visible_span > 320 - dot)
            post_visible_span = 320 - dot;

          if (dot <= 261 && dot + post_visible_span > 261 && mmc3_irq
              && (scany + 1) % 262 < 241) {
            dot = 261;
            tmp -= 261 - post_visible_start_dot;
          } else {
            dot += post_visible_span - 1;
            tmp -= post_visible_span - 1;
          }
        } else if (dot >= 320) {
          if (dot == 320)
            SMOLNES_PPU_PHASE_SET_IF_ACTIVE(SMOLNES_PPU_PHASE_PREFETCH);
          run_prefetch_dot(bg_pattern_base);
        }
      }

      // Check for MMC3 IRQ.
      if ((scany + 1) % 262 < 241 && dot == 261 && mmc3_irq && !mmc3_latch--)
        nmi_irq = 1;

      // Reset vertical VRAM address to T value.
      if (scany == 261 && dot - 280 < 25u)
        V = V & 0x841f | T & 0x7be0;
    }

    if (dot == 1) {
      if (scany == 241) {
        if (ppuctrl & 128)
          nmi_irq = 4;
        ppustatus |= 128;
        SMOLNES_PPU_PHASE_CLEAR();
        SMOLNES_PPU_STEP_END();
        SMOLNES_FRAME_EXEC_END();
        SMOLNES_FRAME_SUBMIT_BEGIN();
        SDL_UpdateTexture(texture, 0, frame_buffer + 2048, 512);
        SDL_RenderCopy(renderer, texture, 0, 0);
        SDL_RenderPresent(renderer);
        SMOLNES_FRAME_SUBMIT_END();
        SMOLNES_EVENT_POLL_BEGIN();
        for (SDL_Event event; SDL_PollEvent(&event);)
          if (event.type == SDL_QUIT) {
            SMOLNES_EVENT_POLL_END();
            return 0;
          }
        SMOLNES_EVENT_POLL_END();
        SMOLNES_PPU_STEP_BEGIN();
        SMOLNES_FRAME_EXEC_BEGIN();
      }

      if (scany == 261)
        ppustatus = 0;
    }

    if (++dot == 341) {
      dot = 0;
      scany++;
      scany %= 262;
    }
  }
  SMOLNES_PPU_PHASE_CLEAR();
  SMOLNES_PPU_STEP_END();
  goto loop;
}
