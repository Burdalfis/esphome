#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "fixed_mesh_renderer.h"
#include "suzanne_fixed_mesh.h"
#include "suzanne_perspective.h"

namespace esphome::mipi_spi::demo3d {

static_assert(FIXED_MESH_PERSPECTIVE_BLOCK_PIXELS == PERSPECTIVE_BLOCK_PIXELS,
              "Generic and legacy perspective block settings must stay aligned");
static_assert(SUZANNE_BLENDER_UV_FRAC_BITS == 7,
              "Suzanne fixed material expects the Blender atlas in Q7 texel units");

// 128x128 indexed UV diagnostic texture. This remains entirely in flash.
static constexpr std::array<uint8_t, SUZANNE_BLENDER_TEXTURE_SIZE * SUZANNE_BLENDER_TEXTURE_SIZE>
make_suzanne_blender_uv_texture() {
  std::array<uint8_t, SUZANNE_BLENDER_TEXTURE_SIZE * SUZANNE_BLENDER_TEXTURE_SIZE> texture{};
  for (int y = 0; y < SUZANNE_BLENDER_TEXTURE_SIZE; y++) {
    for (int x = 0; x < SUZANNE_BLENDER_TEXTURE_SIZE; x++) {
      int index = 28 + (x * 70) / (SUZANNE_BLENDER_TEXTURE_SIZE - 1) +
                  (y * 30) / (SUZANNE_BLENDER_TEXTURE_SIZE - 1);
      if ((((x >> 4) ^ (y >> 4)) & 1) != 0)
        index += 40;
      if ((x & 7) == 0 || (y & 7) == 0)
        index = 205;
      if ((x & 31) < 2 || (y & 31) < 2)
        index = 250;
      texture[static_cast<size_t>(y) * SUZANNE_BLENDER_TEXTURE_SIZE + x] =
          static_cast<uint8_t>(index);
    }
  }
  return texture;
}

static constexpr auto SUZANNE_BLENDER_UV_TEXTURE = make_suzanne_blender_uv_texture();

static constexpr FixedMeshIndexedMaterial SUZANNE_BLENDER_FIXED_MATERIAL = {
    SUZANNE_BLENDER_UV_TEXTURE.data(),
    SUZANNE_BLENDER_TEXTURE_SIZE,
    SUZANNE_BLENDER_TEXTURE_SIZE,
    SUZANNE_BLENDER_UV_FRAC_BITS,
    TEXTURED_GOURAUD_LIT_PALETTE_LE.data(),
    TEXTURED_GOURAUD_LIT_PALETTE_BE.data(),
};

static FixedMeshStats SUZANNE_FIXED_MESH_LAST_STATS{};
static inline const FixedMeshStats &get_suzanne_fixed_mesh_stats() {
  return SUZANNE_FIXED_MESH_LAST_STATS;
}

// Compatibility wrapper for the existing demo/YAML API. All actual geometry,
// clipping, projection, perspective correction, Z buffering and dirty-region
// handling now live in FixedMeshRenderer.
template<typename DisplayT>
PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y,
                                                        int32_t camera_z = 500) {
  static FixedMeshRenderer<DisplayT> renderer;
  PerspectiveStats stats{};
  if (!renderer.begin_frame(display))
    return stats;

  FixedMeshDrawParams params{};
  params.phase_x = phase_x;
  params.phase_y = phase_y;
  params.translate_z = camera_z;
  params.focal = 220;
  params.near_depth = 256;
  renderer.draw_mesh(SUZANNE_FIXED_MESH, SUZANNE_BLENDER_FIXED_MATERIAL, params);
  const FixedMeshStats mesh_stats = renderer.end_frame();
  SUZANNE_FIXED_MESH_LAST_STATS = mesh_stats;

  stats.visible_triangles = static_cast<uint16_t>(
      std::min<uint32_t>(mesh_stats.visible_triangles, 0xFFFFu));
  stats.rasterized_triangles = static_cast<uint16_t>(
      std::min<uint32_t>(mesh_stats.rasterized_triangles, 0xFFFFu));
  stats.dirty_bytes = mesh_stats.dirty_bytes;
  stats.dma_high_water_bytes = mesh_stats.dma_high_water_bytes;
  stats.perspective_blocks = mesh_stats.perspective_blocks;
  PERSPECTIVE_LAST_STATS = stats;
  return stats;
}

}  // namespace esphome::mipi_spi::demo3d
