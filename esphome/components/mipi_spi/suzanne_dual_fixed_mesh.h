#pragma once

#include <algorithm>
#include <cstdint>

#include "suzanne_blender_uv_perspective.h"

namespace esphome::mipi_spi::demo3d {

static FixedMeshStats DUAL_SUZANNE_FIXED_MESH_LAST_STATS{};

static inline const FixedMeshStats &get_dual_suzanne_fixed_mesh_stats() {
  return DUAL_SUZANNE_FIXED_MESH_LAST_STATS;
}

// Exercise the generic renderer with two independent objects in one frame.
// The nearer Suzanne is deliberately submitted first and the farther Suzanne
// second, so correct overlap cannot be mistaken for painter's-algorithm draw
// ordering: the shared Z buffer must reject farther fragments drawn later.
template<typename DisplayT>
PerspectiveStats render_dual_suzanne_fixed_mesh(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {
  static FixedMeshRenderer<DisplayT, GOURAUD_TOTAL_VERTS> renderer;
  PerspectiveStats stats{};
  if (!renderer.begin_frame(display))
    return stats;

  // Near/right object. This one also exercises the generic Z-rotation path.
  FixedMeshDrawParams near_params{};
  near_params.phase_x = static_cast<uint8_t>(phase_y + 24u);
  near_params.phase_y = static_cast<uint8_t>(0u - phase_x);
  near_params.phase_z = static_cast<uint8_t>(phase_x + 64u);
  near_params.translate_x = 72;
  near_params.translate_y = 8;
  near_params.translate_z = 590;
  near_params.focal = 220;
  near_params.near_depth = 256;
  renderer.draw_mesh(SUZANNE_FIXED_MESH, SUZANNE_BLENDER_FIXED_MATERIAL, near_params);

  // Far/left object, intentionally drawn second. It should still disappear
  // behind the near object wherever their projected silhouettes overlap.
  FixedMeshDrawParams far_params{};
  far_params.phase_x = phase_x;
  far_params.phase_y = phase_y;
  far_params.translate_x = -92;
  far_params.translate_y = -6;
  far_params.translate_z = 710;
  far_params.focal = 220;
  far_params.near_depth = 256;
  renderer.draw_mesh(SUZANNE_FIXED_MESH, SUZANNE_BLENDER_FIXED_MATERIAL, far_params);

  const FixedMeshStats mesh_stats = renderer.end_frame();
  DUAL_SUZANNE_FIXED_MESH_LAST_STATS = mesh_stats;
  // Keep the generic/single-Suzanne getter and the existing perspective stats
  // useful for unchanged ESPHome logging code while this demo is active.
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
