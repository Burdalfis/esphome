#!/usr/bin/env python3

from pathlib import Path
import re

SRC = Path("esphome/components/mipi_spi/suzanne_blender_uv_perspective.h")
DST = Path("esphome/components/mipi_spi/suzanne_blender_uv_perspective_64.h")

s = SRC.read_text(encoding="utf-8")

# Give every namespace-level helper a unique name so the 64x64 and 128x128
# renderers can be included together for future A/B tests if desired.
s = s.replace("suzanne_blender_expected_uv_count", "suzanne_blender64_expected_uv_count")
s = s.replace("make_suzanne_blender_uv_texture", "make_suzanne_blender_uv_texture_64")
s = s.replace("SUZANNE_BLENDER_UV_TEXTURE", "SUZANNE_BLENDER_UV_TEXTURE_64")
s = s.replace("render_suzanne_blender_uv_perspective", "render_suzanne_blender_uv_perspective_64")

# Keep Blender's existing Q7 128-atlas UV coordinates exactly as-is. Only the
# actual sampled texture shrinks to 64x64; the sampler divides the recovered
# 128-space texel coordinate by two at the final lookup.
s = re.sub(r"\bSUZANNE_BLENDER_TEXTURE_SIZE\b", "SUZANNE_BLENDER_TEXTURE_SIZE_64", s)
s = s.replace(
    "namespace esphome::mipi_spi::demo3d {\n\n",
    "namespace esphome::mipi_spi::demo3d {\n\n"
    "static constexpr int SUZANNE_BLENDER_TEXTURE_SIZE_64 = 64;\n\n",
    1,
)

# Scale the diagnostic artwork spatial frequencies by two so it has the same
# normalized appearance as the 128x128 version.
s = s.replace("x >> 4", "x >> 3")
s = s.replace("y >> 4", "y >> 3")
s = s.replace("(x & 7) == 0", "(x & 3) == 0")
s = s.replace("(y & 7) == 0", "(y & 3) == 0")
s = s.replace("(x & 31) < 2", "(x & 15) == 0")
s = s.replace("(y & 31) < 2", "(y & 15) == 0")

# u/v are still reconstructed as Q16 values in the original 128-texel domain.
# One extra shift bit performs nearest-neighbour 2:1 downsampling at lookup.
s = s.replace("u >> 16, 0, SUZANNE_BLENDER_TEXTURE_SIZE_64 - 1", "u >> 17, 0, SUZANNE_BLENDER_TEXTURE_SIZE_64 - 1")
s = s.replace("v >> 16, 0, SUZANNE_BLENDER_TEXTURE_SIZE_64 - 1", "v >> 17, 0, SUZANNE_BLENDER_TEXTURE_SIZE_64 - 1")

s = s.replace("128x128 indexed UV diagnostic texture", "64x64 indexed UV diagnostic texture")
s = s.replace("8/32-texel grid lines", "4/16-texel grid lines")
s = s.replace(
    "Suzanne Blender UV perspective demo expects a 16-bit framebuffer",
    "Suzanne Blender UV 64x64 perspective demo expects a 16-bit framebuffer",
)

# Sanity checks: exactly one renderer function and the expected final sampler.
assert "render_suzanne_blender_uv_perspective_64" in s
assert "render_suzanne_blender_uv_perspective(" not in s
assert "u >> 17, 0, SUZANNE_BLENDER_TEXTURE_SIZE_64 - 1" in s
assert "v >> 17, 0, SUZANNE_BLENDER_TEXTURE_SIZE_64 - 1" in s
assert "std::array<uint8_t, SUZANNE_BLENDER_TEXTURE_SIZE_64 * SUZANNE_BLENDER_TEXTURE_SIZE_64>" in s

DST.write_text(s, encoding="utf-8")
print(f"Wrote {DST}")
