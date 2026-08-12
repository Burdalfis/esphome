#!/usr/bin/env python3

from pathlib import Path

SRC = Path("esphome/components/mipi_spi/suzanne_blender_uv_perspective_64.h")
DST = Path("esphome/components/mipi_spi/suzanne_blender_uv_perspective_64_sweep.h")

s = SRC.read_text(encoding="utf-8")

s = s.replace(
    "namespace esphome::mipi_spi::demo3d {\n",
    '''namespace esphome::mipi_spi::demo3d {\n\n// Automatic perspective-correction benchmark. Each setting is held for 256\n// render calls so, with the demo's uint8_t phase counters advancing once per\n// call, every setting sees one complete and identical orientation cycle.\nstatic constexpr std::array<int, 4> SUZANNE_PERSPECTIVE_SWEEP_BLOCKS = {2, 4, 8, 16};\nstatic constexpr uint32_t SUZANNE_PERSPECTIVE_SWEEP_FRAMES_PER_BLOCK = 256;\nstatic int SUZANNE_PERSPECTIVE_SWEEP_LAST_BLOCK = 2;\nstatic uint32_t SUZANNE_PERSPECTIVE_SWEEP_LAST_PASS = 0;\n\nstatic inline int get_suzanne_perspective_sweep_block_pixels() {\n  return SUZANNE_PERSPECTIVE_SWEEP_LAST_BLOCK;\n}\n\nstatic inline uint32_t get_suzanne_perspective_sweep_pass() {\n  return SUZANNE_PERSPECTIVE_SWEEP_LAST_PASS;\n}\n''',
    1,
)

s = s.replace(
    "render_suzanne_blender_uv_perspective_64(DisplayT *display, uint8_t phase_x, uint8_t phase_y)",
    "render_suzanne_blender_uv_perspective_64_sweep(DisplayT *display, uint8_t phase_x, uint8_t phase_y)",
    1,
)

needle = '''  const int stride = static_cast<int>(display->get_framebuffer_stride());\n  if (screen_w <= 0 || screen_h <= 0 || stride <= 0)\n    return stats;\n'''
insert = '''  const int stride = static_cast<int>(display->get_framebuffer_stride());\n  if (screen_w <= 0 || screen_h <= 0 || stride <= 0)\n    return stats;\n\n  // Advance only after selecting the current setting, so every setting owns\n  // exactly 256 complete render calls. The pass counter increments after all\n  // four block sizes have completed once.\n  static uint32_t sweep_frame = 0;\n  const uint32_t sweep_slot = sweep_frame / SUZANNE_PERSPECTIVE_SWEEP_FRAMES_PER_BLOCK;\n  const size_t sweep_index = static_cast<size_t>(sweep_slot % SUZANNE_PERSPECTIVE_SWEEP_BLOCKS.size());\n  const int perspective_block_pixels = SUZANNE_PERSPECTIVE_SWEEP_BLOCKS[sweep_index];\n  SUZANNE_PERSPECTIVE_SWEEP_LAST_BLOCK = perspective_block_pixels;\n  SUZANNE_PERSPECTIVE_SWEEP_LAST_PASS =\n      sweep_frame / (SUZANNE_PERSPECTIVE_SWEEP_FRAMES_PER_BLOCK * SUZANNE_PERSPECTIVE_SWEEP_BLOCKS.size());\n  sweep_frame++;\n'''
if needle not in s:
    raise RuntimeError("Could not find renderer setup insertion point")
s = s.replace(needle, insert, 1)

count = s.count("PERSPECTIVE_BLOCK_PIXELS")
if count != 1:
    raise RuntimeError(f"Expected one runtime block-size use, found {count}")
s = s.replace("PERSPECTIVE_BLOCK_PIXELS", "perspective_block_pixels")

s = s.replace(
    "Suzanne Blender UV 64x64 perspective demo expects a 16-bit framebuffer",
    "Suzanne Blender UV 64x64 perspective sweep expects a 16-bit framebuffer",
    1,
)

DST.write_text(s, encoding="utf-8")
print(f"Wrote {DST}")
