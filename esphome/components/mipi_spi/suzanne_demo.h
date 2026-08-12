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
  static std::array<SuzanneRenderVertex, TOTAL_VERTS> projected{};

  const PixelT black = display->native_color(Color(0, 0, 0));

  std::array<PixelT, 32> shade_palette{};
  for (int i = 0; i < 32; i++) {
    const int level = (i * 255 + 15) / 31;
    shade_palette[i] = display->native_color(
        Color(static_cast<uint8_t>((70 * level) / 255), static_cast<uint8_t>((185 * level) / 255),
              static_cast<uint8_t>((255 * level) / 255)));
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

  // Suzanne's native Blender axes are X right, Y depth and Z up, with the face toward -Y.
  // Convert them to our camera basis with a proper rotation (determinant +1), not a reflection:
  // render X = -source X, render Y = source Z, render Z = source Y.
  // The X flip is visually irrelevant for symmetric Suzanne, while keeping triangle winding and normals correct.
  constexpr int32_t CAMERA_Z = 500;
  constexpr int32_t FOCAL = 220;
  const int center_x = screen_w / 2;
  const int center_y = screen_h / 2;

  auto transform_vertex = [&](size_t out_index, int32_t x, int32_t y, int32_t z) {
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

  for (size_t i = 0; i < HALF_VERTS; i++) {
    const int32_t source_x = static_cast<int32_t>(SUZANNE_VERTICES[i][0]) + 127;
    const int32_t source_y = static_cast<int32_t>(SUZANNE_VERTICES[i][1]);
    const int32_t source_z = static_cast<int32_t>(SUZANNE_VERTICES[i][2]);

    // Apply the same orientation-preserving basis transform to both the stored half and its mirrored source vertex.
    transform_vertex(i, -source_x, source_z, source_y);
    transform_vertex(HALF_VERTS + i, source_x, source_z, source_y);
  }

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

  auto render_face = [&](size_t ia, size_t ib, size_t ic, size_t id, bool quad) {
    const auto &a = projected[ia];
    const auto &b = projected[ib];
    const auto &c = projected[ic];
    const auto &d = projected[id];

    const int32_t e1x = b.cx - a.cx;
    const int32_t e1y = b.cy - a.cy;
    const int32_t e1z = b.cz - a.cz;
    const int32_t e2x = c.cx - a.cx;
    const int32_t e2y = c.cy - a.cy;
    const int32_t e2z = c.cz - a.cz;
    const int32_t nx = e1y * e2z - e1z * e2y;
    const int32_t ny = e1z * e2x - e1x * e2z;
    const int32_t nz = e1x * e2y - e1y * e2x;

    // With the handedness-preserving source->camera transform above, Blender's winding still produces outward normals.
    const int64_t view_dot = static_cast<int64_t>(nx) * a.cx + static_cast<int64_t>(ny) * a.cy +
                             static_cast<int64_t>(nz) * (CAMERA_Z + a.cz);
    if (view_dot >= 0)
      return;

    stats.visible_faces++;

    // Fixed camera-space light from upper-left/front. L1 normalisation is cheap and visually adequate for flat shading.
    constexpr int32_t LX = -50;
    constexpr int32_t LY = 80;
    constexpr int32_t LZ = -120;
    const int64_t light_dot = static_cast<int64_t>(nx) * LX + static_cast<int64_t>(ny) * LY +
                              static_cast<int64_t>(nz) * LZ;
    const int64_t norm_l1 = static_cast<int64_t>(std::abs(nx)) + std::abs(ny) + std::abs(nz);
    int brightness = 48;
    if (light_dot > 0 && norm_l1 != 0) {
      const int diffuse = static_cast<int>(std::min<int64_t>(207, (light_dot * 207) / (norm_l1 * 120)));
      brightness += diffuse;
    }
    const PixelT color = shade_palette[std::clamp(brightness >> 3, 0, 31)];

    fill_triangle(a, b, c, color);
    stats.rasterized_triangles++;
    if (quad) {
      fill_triangle(a, c, d, color);
      stats.rasterized_triangles++;
    }
  };

  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const int a0 = static_cast<int>(SUZANNE_FACES[i][0]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const int b0 = static_cast<int>(SUZANNE_FACES[i][1]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const int c0 = static_cast<int>(SUZANNE_FACES[i][2]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const int d0 = static_cast<int>(SUZANNE_FACES[i][3]) + static_cast<int>(i) - SUZANNE_OFFSET;
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];

    render_face(static_cast<size_t>(a0), static_cast<size_t>(b0), static_cast<size_t>(c0), static_cast<size_t>(d0),
                quad);
    render_face(HALF_VERTS + static_cast<size_t>(c0), HALF_VERTS + static_cast<size_t>(b0),
                HALF_VERTS + static_cast<size_t>(a0), HALF_VERTS + static_cast<size_t>(d0), quad);
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
