#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "suzanne_data.h"

namespace esphome::mipi_spi::demo3d {

struct SuzanneStats {
  uint16_t visible_faces{0};
  uint16_t rasterized_triangles{0};
};

struct SuzanneRenderVertex {
  int16_t x;
  int16_t y;
  int16_t cx;
  int16_t cy;
  int16_t cz;
  uint8_t inv_z;
};

struct SuzanneModelVertex {
  int16_t x;
  int16_t y;
  int16_t z;
};

struct SuzanneNormal {
  int16_t x;
  int16_t y;
  int16_t z;
};

static constexpr int16_t SIN_Q15[65] = {
      0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
   6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
  12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
  18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
  23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
  27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
  30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
  32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
  32767
};

static inline int32_t sin_q15(uint8_t phase) {
  const uint8_t quadrant = phase >> 6;
  const uint8_t index = phase & 0x3F;
  switch (quadrant) {
    case 0:
      return SIN_Q15[index];
    case 1:
      return SIN_Q15[64 - index];
    case 2:
      return -SIN_Q15[index];
    default:
      return -SIN_Q15[64 - index];
  }
}

// Integer square root used only during the one-time model-normal setup. Keeping this integer avoids pulling software
// floating-point square-root into the C6 benchmark while still giving each triangle a true Euclidean unit normal.
static inline uint32_t integer_sqrt_u64(uint64_t value) {
  uint64_t result = 0;
  uint64_t bit = uint64_t{1} << 62;
  while (bit > value)
    bit >>= 2;

  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<uint32_t>(result);
}

template<typename DisplayT> SuzanneStats render_suzanne(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {
  using PixelT = std::remove_pointer_t<decltype(display->get_framebuffer())>;
  static_assert(sizeof(PixelT) == 2, "Suzanne demo expects a 16-bit framebuffer");

  SuzanneStats stats{};
  auto *fb = display->get_framebuffer();
  if (fb == nullptr)
    return stats;

  const int screen_w = display->get_width();
  const int screen_h = display->get_height();
  const int stride = static_cast<int>(display->get_framebuffer_stride());
  if (screen_w <= 0 || screen_h <= 0 || stride <= 0)
    return stats;

  static std::vector<uint8_t> z_buffer;
  const size_t z_size = static_cast<size_t>(screen_w) * screen_h;
  if (z_buffer.size() != z_size)
    z_buffer.resize(z_size);
  std::memset(z_buffer.data(), 0, z_size);

  constexpr size_t HALF_VERTS = SUZANNE_HALF_VERTEX_COUNT;
  constexpr size_t TOTAL_VERTS = HALF_VERTS * 2;
  constexpr size_t NORMALS_PER_HALF_FACE = 4;
  static std::array<SuzanneModelVertex, TOTAL_VERTS> model_vertices{};
  static std::array<SuzanneRenderVertex, TOTAL_VERTS> projected{};
  static std::array<SuzanneNormal, SUZANNE_HALF_FACE_COUNT * NORMALS_PER_HALF_FACE> model_normals{};
  static bool geometry_ready = false;

  // Suzanne's native Blender axes are X right, Y depth and Z up, with the face toward -Y.
  // Convert them to our camera basis with a proper rotation (determinant +1), not a reflection:
  // render X = -source X, render Y = source Z, render Z = source Y.
  if (!geometry_ready) {
    for (size_t i = 0; i < HALF_VERTS; i++) {
      const int32_t source_x = static_cast<int32_t>(SUZANNE_VERTICES[i][0]) + 127;
      const int32_t source_y = static_cast<int32_t>(SUZANNE_VERTICES[i][1]);
      const int32_t source_z = static_cast<int32_t>(SUZANNE_VERTICES[i][2]);

      model_vertices[i] = {static_cast<int16_t>(-source_x), static_cast<int16_t>(source_z),
                           static_cast<int16_t>(source_y)};
      model_vertices[HALF_VERTS + i] = {static_cast<int16_t>(source_x), static_cast<int16_t>(source_z),
                                        static_cast<int16_t>(source_y)};
    }

    auto make_normal = [&](size_t ia, size_t ib, size_t ic) -> SuzanneNormal {
      const auto &a = model_vertices[ia];
      const auto &b = model_vertices[ib];
      const auto &c = model_vertices[ic];
      const int64_t e1x = static_cast<int32_t>(b.x) - a.x;
      const int64_t e1y = static_cast<int32_t>(b.y) - a.y;
      const int64_t e1z = static_cast<int32_t>(b.z) - a.z;
      const int64_t e2x = static_cast<int32_t>(c.x) - a.x;
      const int64_t e2y = static_cast<int32_t>(c.y) - a.y;
      const int64_t e2z = static_cast<int32_t>(c.z) - a.z;
      const int64_t nx = e1y * e2z - e1z * e2y;
      const int64_t ny = e1z * e2x - e1x * e2z;
      const int64_t nz = e1x * e2y - e1y * e2x;
      const uint64_t magnitude_sq = static_cast<uint64_t>(nx * nx + ny * ny + nz * nz);
      const uint32_t magnitude = integer_sqrt_u64(magnitude_sq);
      if (magnitude == 0)
        return SuzanneNormal{0, 0, 0};

      auto normalise = [&](int64_t component) -> int16_t {
        const int64_t scaled = (component * 32767) / magnitude;
        return static_cast<int16_t>(std::clamp<int64_t>(scaled, -32767, 32767));
      };
      return SuzanneNormal{normalise(nx), normalise(ny), normalise(nz)};
    };

    for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
      const int a0 = static_cast<int>(SUZANNE_FACES[i][0]) + static_cast<int>(i) - SUZANNE_OFFSET;
      const int b0 = static_cast<int>(SUZANNE_FACES[i][1]) + static_cast<int>(i) - SUZANNE_OFFSET;
      const int c0 = static_cast<int>(SUZANNE_FACES[i][2]) + static_cast<int>(i) - SUZANNE_OFFSET;
      const int d0 = static_cast<int>(SUZANNE_FACES[i][3]) + static_cast<int>(i) - SUZANNE_OFFSET;
      const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];

      const size_t a = static_cast<size_t>(a0);
      const size_t b = static_cast<size_t>(b0);
      const size_t c = static_cast<size_t>(c0);
      const size_t d = static_cast<size_t>(d0);
      const size_t base = i * NORMALS_PER_HALF_FACE;

      model_normals[base + 0] = make_normal(a, b, c);
      model_normals[base + 1] = quad ? make_normal(a, c, d) : SuzanneNormal{};

      const size_t ma = HALF_VERTS + c;
      const size_t mb = HALF_VERTS + b;
      const size_t mc = HALF_VERTS + a;
      const size_t md = HALF_VERTS + d;
      model_normals[base + 2] = make_normal(ma, mb, mc);
      model_normals[base + 3] = quad ? make_normal(ma, mc, md) : SuzanneNormal{};
    }
    geometry_ready = true;
  }

  const PixelT black = display->native_color(Color(0, 0, 0));

  // 256 levels eliminate the visible 5-bit lighting steps from the original benchmark. Build the native RGB565
  // palette once; per-triangle shading is then only a table lookup.
  static std::array<PixelT, 256> shade_palette{};
  static bool palette_ready = false;
  if (!palette_ready) {
    for (int level = 0; level < 256; level++) {
      shade_palette[level] = display->native_color(
          Color(static_cast<uint8_t>((70 * level) / 255), static_cast<uint8_t>((185 * level) / 255),
                static_cast<uint8_t>(level)));
    }
    palette_ready = true;
  }

  auto fill_rect = [&](int x0, int y0, int x1, int y1, PixelT color) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, screen_w - 1);
    y1 = std::min(y1, screen_h - 1);
    if (x0 > x1 || y0 > y1)
      return;
    const size_t count = static_cast<size_t>(x1 - x0 + 1);
    for (int y = y0; y <= y1; y++)
      std::fill_n(fb + static_cast<size_t>(y) * stride + x0, count, color);
  };

  const int32_t sx = sin_q15(phase_x);
  const int32_t cx = sin_q15(static_cast<uint8_t>(phase_x + 64));
  const int32_t sy = sin_q15(phase_y);
  const int32_t cy = sin_q15(static_cast<uint8_t>(phase_y + 64));

  constexpr int32_t CAMERA_Z = 500;
  constexpr int32_t FOCAL = 220;
  const int center_x = screen_w / 2;
  const int center_y = screen_h / 2;

  auto transform_vertex = [&](size_t out_index, const SuzanneModelVertex &m) {
    const int32_t x = m.x;
    const int32_t y = m.y;
    const int32_t z = m.z;
    const int32_t x1 = static_cast<int32_t>((static_cast<int64_t>(x) * cy + static_cast<int64_t>(z) * sy) >> 15);
    const int32_t z1 = static_cast<int32_t>((-static_cast<int64_t>(x) * sy + static_cast<int64_t>(z) * cy) >> 15);
    const int32_t y2 = static_cast<int32_t>((static_cast<int64_t>(y) * cx - static_cast<int64_t>(z1) * sx) >> 15);
    const int32_t z2 = static_cast<int32_t>((static_cast<int64_t>(y) * sx + static_cast<int64_t>(z1) * cx) >> 15);
    const int32_t depth = CAMERA_Z + z2;

    auto &p = projected[out_index];
    p.cx = static_cast<int16_t>(x1);
    p.cy = static_cast<int16_t>(y2);
    p.cz = static_cast<int16_t>(z2);
    p.x = static_cast<int16_t>(center_x + static_cast<int32_t>((static_cast<int64_t>(x1) * FOCAL) / depth));
    p.y = static_cast<int16_t>(center_y - static_cast<int32_t>((static_cast<int64_t>(y2) * FOCAL) / depth));
    int32_t inv_z = 65535 / depth;
    inv_z = std::clamp<int32_t>(inv_z, 1, 255);
    p.inv_z = static_cast<uint8_t>(inv_z);
  };

  for (size_t i = 0; i < TOTAL_VERTS; i++)
    transform_vertex(i, model_vertices[i]);

  int new_x0 = projected[0].x;
  int new_x1 = projected[0].x;
  int new_y0 = projected[0].y;
  int new_y1 = projected[0].y;
  for (size_t i = 1; i < TOTAL_VERTS; i++) {
    new_x0 = std::min<int>(new_x0, projected[i].x);
    new_x1 = std::max<int>(new_x1, projected[i].x);
    new_y0 = std::min<int>(new_y0, projected[i].y);
    new_y1 = std::max<int>(new_y1, projected[i].y);
  }
  new_x0 -= 2;
  new_y0 -= 2;
  new_x1 += 2;
  new_y1 += 2;

  static bool have_old_box = false;
  static int old_x0 = 0;
  static int old_y0 = 0;
  static int old_x1 = 0;
  static int old_y1 = 0;

  if (!have_old_box) {
    std::fill_n(fb, static_cast<size_t>(stride) * screen_h, black);
  } else {
    fill_rect(old_x0, old_y0, old_x1, old_y1, black);
  }

  auto draw_span = [&](int y, int32_t xa, int32_t za, int32_t xb, int32_t zb, PixelT color) {
    if (y < 0 || y >= screen_h)
      return;
    if (xa > xb) {
      std::swap(xa, xb);
      std::swap(za, zb);
    }

    int x0 = xa >> 16;
    int x1 = xb >> 16;
    if (x1 < 0 || x0 >= screen_w)
      return;

    const int original_span = x1 - x0;
    int32_t z_step = 0;
    if (original_span > 0)
      z_step = (zb - za) / original_span;

    int32_t z = za;
    if (x0 < 0) {
      z += static_cast<int32_t>(-x0) * z_step;
      x0 = 0;
    }
    if (x1 >= screen_w)
      x1 = screen_w - 1;
    if (x0 > x1)
      return;

    PixelT *pixel = fb + static_cast<size_t>(y) * stride + x0;
    uint8_t *depth_pixel = z_buffer.data() + static_cast<size_t>(y) * screen_w + x0;
    for (int x = x0; x <= x1; x++, pixel++, depth_pixel++, z += z_step) {
      const uint8_t iz = static_cast<uint8_t>(std::clamp<int32_t>(z >> 16, 0, 255));
      if (iz > *depth_pixel) {
        *depth_pixel = iz;
        *pixel = color;
      }
    }
  };

  auto fill_triangle = [&](const SuzanneRenderVertex &va, const SuzanneRenderVertex &vb,
                           const SuzanneRenderVertex &vc, PixelT color) {
    struct ScanVertex {
      int x;
      int y;
      int z;
    };

    ScanVertex v0{va.x, va.y, va.inv_z};
    ScanVertex v1{vb.x, vb.y, vb.inv_z};
    ScanVertex v2{vc.x, vc.y, vc.inv_z};
    if (v1.y < v0.y)
      std::swap(v0, v1);
    if (v2.y < v0.y)
      std::swap(v0, v2);
    if (v2.y < v1.y)
      std::swap(v1, v2);
    if (v0.y == v2.y)
      return;

    const int long_dy = v2.y - v0.y;
    const int32_t long_x_step = static_cast<int32_t>((static_cast<int64_t>(v2.x - v0.x) << 16) / long_dy);
    const int32_t long_z_step = static_cast<int32_t>((static_cast<int64_t>(v2.z - v0.z) << 16) / long_dy);
    int32_t long_x = v0.x << 16;
    int32_t long_z = v0.z << 16;

    if (v1.y > v0.y) {
      const int short_dy = v1.y - v0.y;
      const int32_t short_x_step =
          static_cast<int32_t>((static_cast<int64_t>(v1.x - v0.x) << 16) / short_dy);
      const int32_t short_z_step =
          static_cast<int32_t>((static_cast<int64_t>(v1.z - v0.z) << 16) / short_dy);
      int32_t short_x = v0.x << 16;
      int32_t short_z = v0.z << 16;
      const int top_end = (v1.y == v2.y) ? v1.y : v1.y - 1;
      for (int y = v0.y; y <= top_end; y++) {
        draw_span(y, long_x, long_z, short_x, short_z, color);
        long_x += long_x_step;
        long_z += long_z_step;
        short_x += short_x_step;
        short_z += short_z_step;
      }
    }

    if (v2.y > v1.y) {
      const int short_dy = v2.y - v1.y;
      const int32_t short_x_step =
          static_cast<int32_t>((static_cast<int64_t>(v2.x - v1.x) << 16) / short_dy);
      const int32_t short_z_step =
          static_cast<int32_t>((static_cast<int64_t>(v2.z - v1.z) << 16) / short_dy);
      int32_t short_x = v1.x << 16;
      int32_t short_z = v1.z << 16;
      for (int y = v1.y; y <= v2.y; y++) {
        draw_span(y, long_x, long_z, short_x, short_z, color);
        long_x += long_x_step;
        long_z += long_z_step;
        short_x += short_x_step;
        short_z += short_z_step;
      }
    }
  };

  auto rotate_normal = [&](const SuzanneNormal &normal, int32_t &nx, int32_t &ny, int32_t &nz) {
    const int32_t x1 = static_cast<int32_t>((static_cast<int64_t>(normal.x) * cy +
                                             static_cast<int64_t>(normal.z) * sy) >>
                                            15);
    const int32_t z1 = static_cast<int32_t>((-static_cast<int64_t>(normal.x) * sy +
                                             static_cast<int64_t>(normal.z) * cy) >>
                                            15);
    nx = x1;
    ny = static_cast<int32_t>((static_cast<int64_t>(normal.y) * cx - static_cast<int64_t>(z1) * sx) >> 15);
    nz = static_cast<int32_t>((static_cast<int64_t>(normal.y) * sx + static_cast<int64_t>(z1) * cx) >> 15);
  };

  // Unit camera-space light in Q15, equivalent to the original (-50, 80, -120) direction.
  constexpr int32_t LIGHT_X_Q15 = -10733;
  constexpr int32_t LIGHT_Y_Q15 = 17173;
  constexpr int32_t LIGHT_Z_Q15 = -25760;

  auto render_triangle = [&](size_t ia, size_t ib, size_t ic, const SuzanneNormal &model_normal) {
    const auto &a = projected[ia];
    const auto &b = projected[ib];
    const auto &c = projected[ic];

    int32_t nx;
    int32_t ny;
    int32_t nz;
    rotate_normal(model_normal, nx, ny, nz);

    // Cull each constituent triangle independently. This matters for Suzanne's slightly non-planar quads near
    // grazing angles; using ABC's normal to cull both ABC and ACD could remove a still-front-facing second triangle.
    const int64_t view_dot = static_cast<int64_t>(nx) * a.cx + static_cast<int64_t>(ny) * a.cy +
                             static_cast<int64_t>(nz) * (CAMERA_Z + a.cz);
    if (view_dot >= 0)
      return;

    stats.visible_faces++;

    const int64_t light_dot_q30 = static_cast<int64_t>(nx) * LIGHT_X_Q15 +
                                  static_cast<int64_t>(ny) * LIGHT_Y_Q15 +
                                  static_cast<int64_t>(nz) * LIGHT_Z_Q15;
    const int32_t diffuse_q15 =
        static_cast<int32_t>(std::clamp<int64_t>(light_dot_q30 >> 15, 0, 32767));
    const int brightness = std::clamp(48 + static_cast<int>((static_cast<int64_t>(diffuse_q15) * 207 + 16384) >> 15),
                                      0, 255);

    fill_triangle(a, b, c, shade_palette[brightness]);
    stats.rasterized_triangles++;
  };

  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const int a0 = static_cast<int>(SUZANNE_FACES[i][0]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const int b0 = static_cast<int>(SUZANNE_FACES[i][1]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const int c0 = static_cast<int>(SUZANNE_FACES[i][2]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const int d0 = static_cast<int>(SUZANNE_FACES[i][3]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];

    const size_t a = static_cast<size_t>(a0);
    const size_t b = static_cast<size_t>(b0);
    const size_t c = static_cast<size_t>(c0);
    const size_t d = static_cast<size_t>(d0);
    const size_t base = i * NORMALS_PER_HALF_FACE;

    render_triangle(a, b, c, model_normals[base + 0]);
    if (quad)
      render_triangle(a, c, d, model_normals[base + 1]);

    const size_t ma = HALF_VERTS + c;
    const size_t mb = HALF_VERTS + b;
    const size_t mc = HALF_VERTS + a;
    const size_t md = HALF_VERTS + d;
    render_triangle(ma, mb, mc, model_normals[base + 2]);
    if (quad)
      render_triangle(ma, mc, md, model_normals[base + 3]);
  }

  if (!have_old_box) {
    display->mark_dirty(0, 0, screen_w - 1, screen_h - 1);
  } else {
    display->mark_dirty(std::min(old_x0, new_x0), std::min(old_y0, new_y0), std::max(old_x1, new_x1),
                        std::max(old_y1, new_y1));
  }

  old_x0 = new_x0;
  old_y0 = new_y0;
  old_x1 = new_x1;
  old_y1 = new_y1;
  have_old_box = true;
  return stats;
}

}  // namespace esphome::mipi_spi::demo3d
