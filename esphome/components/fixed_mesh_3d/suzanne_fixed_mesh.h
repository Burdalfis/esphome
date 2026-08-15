#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fixed_mesh_renderer.h"
#include "suzanne_blender_uv_data.h"
#include "suzanne_gouraud.h"

namespace esphome::mipi_spi::demo3d {

static constexpr size_t suzanne_fixed_mesh_triangle_count() {
  size_t count = 0;
  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];
    count += quad ? 4u : 2u;  // one or two triangles on each mirrored half
  }
  return count;
}

static constexpr size_t SUZANNE_FIXED_MESH_TRIANGLE_COUNT = suzanne_fixed_mesh_triangle_count();
static_assert(SUZANNE_FIXED_MESH_TRIANGLE_COUNT == 968,
              "Suzanne compact topology should expand to 968 triangles");

static constexpr FixedMeshUV suzanne_fixed_uv(const SuzanneBlenderUVQ7 &uv) {
  return {uv.u, uv.v};
}

static constexpr std::array<FixedMeshTriangle, SUZANNE_FIXED_MESH_TRIANGLE_COUNT>
make_suzanne_fixed_mesh_triangles() {
  std::array<FixedMeshTriangle, SUZANNE_FIXED_MESH_TRIANGLE_COUNT> out{};
  size_t triangle_cursor = 0;
  size_t uv_cursor = 0;

  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const size_t a = gouraud_face_index(i, SUZANNE_FACES[i][0]);
    const size_t b = gouraud_face_index(i, SUZANNE_FACES[i][1]);
    const size_t c = gouraud_face_index(i, SUZANNE_FACES[i][2]);
    const size_t d = gouraud_face_index(i, SUZANNE_FACES[i][3]);
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];
    const size_t corner_count = quad ? 4u : 3u;
    const auto *left_uv = SUZANNE_BLENDER_UVS_Q7.data() + uv_cursor;
    const auto *right_uv = left_uv + corner_count;

    out[triangle_cursor++] = {
        static_cast<uint16_t>(a), static_cast<uint16_t>(b), static_cast<uint16_t>(c),
        suzanne_fixed_uv(left_uv[0]), suzanne_fixed_uv(left_uv[1]), suzanne_fixed_uv(left_uv[2])};
    if (quad) {
      out[triangle_cursor++] = {
          static_cast<uint16_t>(a), static_cast<uint16_t>(c), static_cast<uint16_t>(d),
          suzanne_fixed_uv(left_uv[0]), suzanne_fixed_uv(left_uv[2]), suzanne_fixed_uv(left_uv[3])};
    }

    const size_t ma = GOURAUD_HALF_VERTS + c;
    const size_t mb = GOURAUD_HALF_VERTS + b;
    const size_t mc = GOURAUD_HALF_VERTS + a;
    const size_t md = GOURAUD_HALF_VERTS + d;
    out[triangle_cursor++] = {
        static_cast<uint16_t>(ma), static_cast<uint16_t>(mb), static_cast<uint16_t>(mc),
        suzanne_fixed_uv(right_uv[0]), suzanne_fixed_uv(right_uv[1]), suzanne_fixed_uv(right_uv[2])};
    if (quad) {
      out[triangle_cursor++] = {
          static_cast<uint16_t>(ma), static_cast<uint16_t>(mc), static_cast<uint16_t>(md),
          suzanne_fixed_uv(right_uv[0]), suzanne_fixed_uv(right_uv[2]), suzanne_fixed_uv(right_uv[3])};
    }

    uv_cursor += corner_count * 2u;
  }
  return out;
}

static constexpr auto SUZANNE_FIXED_MESH_TRIANGLES = make_suzanne_fixed_mesh_triangles();

static_assert(SUZANNE_BLENDER_UV_COUNT == 1968,
              "Suzanne Blender face-corner atlas changed unexpectedly");

static constexpr FixedMeshView<GouraudModelVertex, GouraudNormal> SUZANNE_FIXED_MESH = {
    GOURAUD_MODEL_VERTICES.data(), GOURAUD_VERTEX_NORMALS.data(), GOURAUD_TOTAL_VERTS,
    SUZANNE_FIXED_MESH_TRIANGLES.data(), SUZANNE_FIXED_MESH_TRIANGLE_COUNT};

}  // namespace esphome::mipi_spi::demo3d
