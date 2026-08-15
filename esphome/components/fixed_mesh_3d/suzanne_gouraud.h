#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "suzanne_data.h"

namespace esphome::mipi_spi::demo3d {

struct GouraudStats {
  uint16_t visible_triangles{0};
  uint16_t rasterized_triangles{0};
  uint32_t dirty_bytes{0};
  uint32_t dma_high_water_bytes{0};
};

struct GouraudModelVertex {
  int16_t x;
  int16_t y;
  int16_t z;
};

struct GouraudNormal {
  int16_t x;
  int16_t y;
  int16_t z;
};

struct GouraudNormalAccum {
  int64_t x;
  int64_t y;
  int64_t z;
};

struct GouraudRenderVertex {
  int16_t x;
  int16_t y;
  int16_t cx;
  int16_t cy;
  int16_t cz;
  uint8_t inv_z;
  uint8_t shade;
};

static constexpr size_t GOURAUD_HALF_VERTS = SUZANNE_HALF_VERTEX_COUNT;
static constexpr size_t GOURAUD_TOTAL_VERTS = GOURAUD_HALF_VERTS * 2;

static constexpr int16_t GOURAUD_SIN_Q15[65] = {
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

static inline int32_t gouraud_sin_q15(uint8_t phase) {
  const uint8_t quadrant = phase >> 6;
  const uint8_t index = phase & 0x3F;
  switch (quadrant) {
    case 0:
      return GOURAUD_SIN_Q15[index];
    case 1:
      return GOURAUD_SIN_Q15[64 - index];
    case 2:
      return -GOURAUD_SIN_Q15[index];
    default:
      return -GOURAUD_SIN_Q15[64 - index];
  }
}

static constexpr uint32_t gouraud_integer_sqrt_u64(uint64_t value) {
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

static constexpr GouraudNormal gouraud_normalise(int64_t x, int64_t y, int64_t z) {
  const uint64_t mag_sq = static_cast<uint64_t>(x * x + y * y + z * z);
  const uint32_t mag = gouraud_integer_sqrt_u64(mag_sq);
  if (mag == 0)
    return {0, 0, 0};
  auto scale = [mag](int64_t v) constexpr -> int16_t {
    const int64_t q15 = (v * 32767) / mag;
    return static_cast<int16_t>(q15 < -32767 ? -32767 : (q15 > 32767 ? 32767 : q15));
  };
  return {scale(x), scale(y), scale(z)};
}

static constexpr std::array<GouraudModelVertex, GOURAUD_TOTAL_VERTS> gouraud_make_vertices() {
  std::array<GouraudModelVertex, GOURAUD_TOTAL_VERTS> out{};
  for (size_t i = 0; i < GOURAUD_HALF_VERTS; i++) {
    const int32_t source_x = static_cast<int32_t>(SUZANNE_VERTICES[i][0]) + 127;
    const int32_t source_y = static_cast<int32_t>(SUZANNE_VERTICES[i][1]);
    const int32_t source_z = static_cast<int32_t>(SUZANNE_VERTICES[i][2]);
    out[i] = {static_cast<int16_t>(-source_x), static_cast<int16_t>(source_z), static_cast<int16_t>(source_y)};
    out[GOURAUD_HALF_VERTS + i] = {static_cast<int16_t>(source_x), static_cast<int16_t>(source_z),
                                    static_cast<int16_t>(source_y)};
  }
  return out;
}

static constexpr auto GOURAUD_MODEL_VERTICES = gouraud_make_vertices();

static constexpr size_t gouraud_face_index(size_t face, int8_t encoded) {
  return static_cast<size_t>(static_cast<int>(encoded) + static_cast<int>(face) - SUZANNE_OFFSET);
}

static constexpr void gouraud_accumulate_triangle(
    std::array<GouraudNormalAccum, GOURAUD_TOTAL_VERTS> &acc, size_t ia, size_t ib, size_t ic) {
  const auto &a = GOURAUD_MODEL_VERTICES[ia];
  const auto &b = GOURAUD_MODEL_VERTICES[ib];
  const auto &c = GOURAUD_MODEL_VERTICES[ic];
  const int64_t e1x = static_cast<int32_t>(b.x) - a.x;
  const int64_t e1y = static_cast<int32_t>(b.y) - a.y;
  const int64_t e1z = static_cast<int32_t>(b.z) - a.z;
  const int64_t e2x = static_cast<int32_t>(c.x) - a.x;
  const int64_t e2y = static_cast<int32_t>(c.y) - a.y;
  const int64_t e2z = static_cast<int32_t>(c.z) - a.z;
  const int64_t nx = e1y * e2z - e1z * e2y;
  const int64_t ny = e1z * e2x - e1x * e2z;
  const int64_t nz = e1x * e2y - e1y * e2x;
  acc[ia].x += nx;
  acc[ia].y += ny;
  acc[ia].z += nz;
  acc[ib].x += nx;
  acc[ib].y += ny;
  acc[ib].z += nz;
  acc[ic].x += nx;
  acc[ic].y += ny;
  acc[ic].z += nz;
}

static constexpr std::array<GouraudNormal, GOURAUD_TOTAL_VERTS> gouraud_make_vertex_normals() {
  std::array<GouraudNormalAccum, GOURAUD_TOTAL_VERTS> acc{};
  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const size_t a = gouraud_face_index(i, SUZANNE_FACES[i][0]);
    const size_t b = gouraud_face_index(i, SUZANNE_FACES[i][1]);
    const size_t c = gouraud_face_index(i, SUZANNE_FACES[i][2]);
    const size_t d = gouraud_face_index(i, SUZANNE_FACES[i][3]);
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];

    gouraud_accumulate_triangle(acc, a, b, c);
    if (quad)
      gouraud_accumulate_triangle(acc, a, c, d);

    const size_t ma = GOURAUD_HALF_VERTS + c;
    const size_t mb = GOURAUD_HALF_VERTS + b;
    const size_t mc = GOURAUD_HALF_VERTS + a;
    const size_t md = GOURAUD_HALF_VERTS + d;
    gouraud_accumulate_triangle(acc, ma, mb, mc);
    if (quad)
      gouraud_accumulate_triangle(acc, ma, mc, md);
  }

  // Blender aliases vertices on the mirror plane. Our compact runtime representation duplicates them, so merge the
  // accumulated normals for each mirrored centerline pair before normalising to avoid a lighting seam down the middle.
  for (size_t i = 0; i < GOURAUD_HALF_VERTS; i++) {
    const size_t mirror = GOURAUD_HALF_VERTS + i;
    if (GOURAUD_MODEL_VERTICES[i].x == 0 && GOURAUD_MODEL_VERTICES[mirror].x == 0) {
      const GouraudNormalAccum merged{acc[i].x + acc[mirror].x, acc[i].y + acc[mirror].y,
                                      acc[i].z + acc[mirror].z};
      acc[i] = merged;
      acc[mirror] = merged;
    }
  }

  std::array<GouraudNormal, GOURAUD_TOTAL_VERTS> normals{};
  for (size_t i = 0; i < GOURAUD_TOTAL_VERTS; i++)
    normals[i] = gouraud_normalise(acc[i].x, acc[i].y, acc[i].z);
  return normals;
}

// Both tables are compile-time constants. On ESP32 they live in read-only program data rather than writable heap/BSS.
static constexpr auto GOURAUD_VERTEX_NORMALS = gouraud_make_vertex_normals();

static GouraudStats GOURAUD_LAST_STATS{};
static inline const GouraudStats &get_suzanne_gouraud_stats() { return GOURAUD_LAST_STATS; }

template<typename DisplayT> GouraudStats render_suzanne_gouraud(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {
  using PixelT = std::remove_pointer_t<decltype(display->get_framebuffer())>;
  static_assert(sizeof(PixelT) == 2, "Suzanne Gouraud demo expects a 16-bit framebuffer");

  GouraudStats stats{};
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

  static std::array<GouraudRenderVertex, GOURAUD_TOTAL_VERTS> projected{};
  const PixelT black = display->native_color(Color(0, 0, 0));

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

  const int32_t sx = gouraud_sin_q15(phase_x);
  const int32_t cx = gouraud_sin_q15(static_cast<uint8_t>(phase_x + 64));
  const int32_t sy = gouraud_sin_q15(phase_y);
  const int32_t cy = gouraud_sin_q15(static_cast<uint8_t>(phase_y + 64));

  constexpr int32_t CAMERA_Z = 500;
  constexpr int32_t FOCAL = 220;
  constexpr int32_t LIGHT_X_Q15 = -10733;
  constexpr int32_t LIGHT_Y_Q15 = 17173;
  constexpr int32_t LIGHT_Z_Q15 = -25760;
  const int center_x = screen_w / 2;
  const int center_y = screen_h / 2;

  for (size_t i = 0; i < GOURAUD_TOTAL_VERTS; i++) {
    const auto &m = GOURAUD_MODEL_VERTICES[i];
    const int32_t x1 = static_cast<int32_t>((static_cast<int64_t>(m.x) * cy + static_cast<int64_t>(m.z) * sy) >> 15);
    const int32_t z1 = static_cast<int32_t>((-static_cast<int64_t>(m.x) * sy + static_cast<int64_t>(m.z) * cy) >> 15);
    const int32_t y2 = static_cast<int32_t>((static_cast<int64_t>(m.y) * cx - static_cast<int64_t>(z1) * sx) >> 15);
    const int32_t z2 = static_cast<int32_t>((static_cast<int64_t>(m.y) * sx + static_cast<int64_t>(z1) * cx) >> 15);
    const int32_t depth = CAMERA_Z + z2;

    const auto &n = GOURAUD_VERTEX_NORMALS[i];
    const int32_t nx = static_cast<int32_t>((static_cast<int64_t>(n.x) * cy + static_cast<int64_t>(n.z) * sy) >> 15);
    const int32_t nz1 = static_cast<int32_t>((-static_cast<int64_t>(n.x) * sy + static_cast<int64_t>(n.z) * cy) >> 15);
    const int32_t ny = static_cast<int32_t>((static_cast<int64_t>(n.y) * cx - static_cast<int64_t>(nz1) * sx) >> 15);
    const int32_t nz = static_cast<int32_t>((static_cast<int64_t>(n.y) * sx + static_cast<int64_t>(nz1) * cx) >> 15);
    const int64_t light_dot_q30 = static_cast<int64_t>(nx) * LIGHT_X_Q15 +
                                  static_cast<int64_t>(ny) * LIGHT_Y_Q15 +
                                  static_cast<int64_t>(nz) * LIGHT_Z_Q15;
    const int32_t diffuse_q15 = static_cast<int32_t>(std::clamp<int64_t>(light_dot_q30 >> 15, 0, 32767));
    const uint8_t shade = static_cast<uint8_t>(std::clamp(
        48 + static_cast<int>((static_cast<int64_t>(diffuse_q15) * 207 + 16384) >> 15), 0, 255));

    auto &p = projected[i];
    p.cx = static_cast<int16_t>(x1);
    p.cy = static_cast<int16_t>(y2);
    p.cz = static_cast<int16_t>(z2);
    p.x = static_cast<int16_t>(center_x + static_cast<int32_t>((static_cast<int64_t>(x1) * FOCAL) / depth));
    p.y = static_cast<int16_t>(center_y - static_cast<int32_t>((static_cast<int64_t>(y2) * FOCAL) / depth));
    p.inv_z = static_cast<uint8_t>(std::clamp<int32_t>(65535 / depth, 1, 255));
    p.shade = shade;
  }

  int new_x0 = projected[0].x;
  int new_x1 = projected[0].x;
  int new_y0 = projected[0].y;
  int new_y1 = projected[0].y;
  for (size_t i = 1; i < GOURAUD_TOTAL_VERTS; i++) {
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
  static uint32_t dma_high_water_bytes = 0;

  if (!have_old_box)
    std::fill_n(fb, static_cast<size_t>(stride) * screen_h, black);
  else
    fill_rect(old_x0, old_y0, old_x1, old_y1, black);

  auto draw_span = [&](int y, int32_t xa, int32_t za, int32_t sa, int32_t xb, int32_t zb, int32_t sb) {
    if (y < 0 || y >= screen_h)
      return;
    if (xa > xb) {
      std::swap(xa, xb);
      std::swap(za, zb);
      std::swap(sa, sb);
    }

    int x0 = xa >> 16;
    int x1 = xb >> 16;
    if (x1 < 0 || x0 >= screen_w)
      return;

    const int original_span = x1 - x0;
    int32_t z_step = 0;
    int32_t s_step = 0;
    if (original_span > 0) {
      z_step = (zb - za) / original_span;
      s_step = (sb - sa) / original_span;
    }

    int32_t z = za;
    int32_t shade = sa;
    if (x0 < 0) {
      z += static_cast<int32_t>(-x0) * z_step;
      shade += static_cast<int32_t>(-x0) * s_step;
      x0 = 0;
    }
    if (x1 >= screen_w)
      x1 = screen_w - 1;
    if (x0 > x1)
      return;

    PixelT *pixel = fb + static_cast<size_t>(y) * stride + x0;
    uint8_t *depth_pixel = z_buffer.data() + static_cast<size_t>(y) * screen_w + x0;
    for (int x = x0; x <= x1; x++, pixel++, depth_pixel++, z += z_step, shade += s_step) {
      const uint8_t iz = static_cast<uint8_t>(std::clamp<int32_t>(z >> 16, 0, 255));
      if (iz > *depth_pixel) {
        *depth_pixel = iz;
        *pixel = shade_palette[static_cast<uint8_t>(std::clamp<int32_t>(shade >> 16, 0, 255))];
      }
    }
  };

  auto fill_triangle = [&](const GouraudRenderVertex &va, const GouraudRenderVertex &vb,
                           const GouraudRenderVertex &vc) {
    struct ScanVertex {
      int x;
      int y;
      int z;
      int s;
    };

    ScanVertex v0{va.x, va.y, va.inv_z, va.shade};
    ScanVertex v1{vb.x, vb.y, vb.inv_z, vb.shade};
    ScanVertex v2{vc.x, vc.y, vc.inv_z, vc.shade};
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
    const int32_t long_s_step = static_cast<int32_t>((static_cast<int64_t>(v2.s - v0.s) << 16) / long_dy);
    int32_t long_x = v0.x << 16;
    int32_t long_z = v0.z << 16;
    int32_t long_s = v0.s << 16;

    if (v1.y > v0.y) {
      const int short_dy = v1.y - v0.y;
      const int32_t short_x_step = static_cast<int32_t>((static_cast<int64_t>(v1.x - v0.x) << 16) / short_dy);
      const int32_t short_z_step = static_cast<int32_t>((static_cast<int64_t>(v1.z - v0.z) << 16) / short_dy);
      const int32_t short_s_step = static_cast<int32_t>((static_cast<int64_t>(v1.s - v0.s) << 16) / short_dy);
      int32_t short_x = v0.x << 16;
      int32_t short_z = v0.z << 16;
      int32_t short_s = v0.s << 16;
      const int top_end = (v1.y == v2.y) ? v1.y : v1.y - 1;
      for (int y = v0.y; y <= top_end; y++) {
        draw_span(y, long_x, long_z, long_s, short_x, short_z, short_s);
        long_x += long_x_step;
        long_z += long_z_step;
        long_s += long_s_step;
        short_x += short_x_step;
        short_z += short_z_step;
        short_s += short_s_step;
      }
    }

    if (v2.y > v1.y) {
      const int short_dy = v2.y - v1.y;
      const int32_t short_x_step = static_cast<int32_t>((static_cast<int64_t>(v2.x - v1.x) << 16) / short_dy);
      const int32_t short_z_step = static_cast<int32_t>((static_cast<int64_t>(v2.z - v1.z) << 16) / short_dy);
      const int32_t short_s_step = static_cast<int32_t>((static_cast<int64_t>(v2.s - v1.s) << 16) / short_dy);
      int32_t short_x = v1.x << 16;
      int32_t short_z = v1.z << 16;
      int32_t short_s = v1.s << 16;
      for (int y = v1.y; y <= v2.y; y++) {
        draw_span(y, long_x, long_z, long_s, short_x, short_z, short_s);
        long_x += long_x_step;
        long_z += long_z_step;
        long_s += long_s_step;
        short_x += short_x_step;
        short_z += short_z_step;
        short_s += short_s_step;
      }
    }
  };

  auto render_triangle = [&](size_t ia, size_t ib, size_t ic) {
    const auto &a = projected[ia];
    const auto &b = projected[ib];
    const auto &c = projected[ic];
    const int32_t e1x = b.cx - a.cx;
    const int32_t e1y = b.cy - a.cy;
    const int32_t e1z = b.cz - a.cz;
    const int32_t e2x = c.cx - a.cx;
    const int32_t e2y = c.cy - a.cy;
    const int32_t e2z = c.cz - a.cz;
    const int32_t nx = e1y * e2z - e1z * e2y;
    const int32_t ny = e1z * e2x - e1x * e2z;
    const int32_t nz = e1x * e2y - e1y * e2x;
    const int64_t view_dot = static_cast<int64_t>(nx) * a.cx + static_cast<int64_t>(ny) * a.cy +
                             static_cast<int64_t>(nz) * (CAMERA_Z + a.cz);
    if (view_dot >= 0)
      return;
    stats.visible_triangles++;
    fill_triangle(a, b, c);
    stats.rasterized_triangles++;
  };

  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const size_t a = gouraud_face_index(i, SUZANNE_FACES[i][0]);
    const size_t b = gouraud_face_index(i, SUZANNE_FACES[i][1]);
    const size_t c = gouraud_face_index(i, SUZANNE_FACES[i][2]);
    const size_t d = gouraud_face_index(i, SUZANNE_FACES[i][3]);
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];

    render_triangle(a, b, c);
    if (quad)
      render_triangle(a, c, d);

    const size_t ma = GOURAUD_HALF_VERTS + c;
    const size_t mb = GOURAUD_HALF_VERTS + b;
    const size_t mc = GOURAUD_HALF_VERTS + a;
    const size_t md = GOURAUD_HALF_VERTS + d;
    render_triangle(ma, mb, mc);
    if (quad)
      render_triangle(ma, mc, md);
  }

  int dirty_x0;
  int dirty_y0;
  int dirty_x1;
  int dirty_y1;
  if (!have_old_box) {
    dirty_x0 = 0;
    dirty_y0 = 0;
    dirty_x1 = screen_w - 1;
    dirty_y1 = screen_h - 1;
  } else {
    dirty_x0 = std::max(0, std::min(old_x0, new_x0));
    dirty_y0 = std::max(0, std::min(old_y0, new_y0));
    dirty_x1 = std::min(screen_w - 1, std::max(old_x1, new_x1));
    dirty_y1 = std::min(screen_h - 1, std::max(old_y1, new_y1));
  }

  stats.dirty_bytes = static_cast<uint32_t>(dirty_x1 - dirty_x0 + 1) *
                      static_cast<uint32_t>(dirty_y1 - dirty_y0 + 1) * sizeof(PixelT);
  // First frame is intentionally full-screen and may use the synchronous fallback on the C6. Track the high-water
  // request after that initialization frame; with the current grow-only DMA backend this matches its steady retained
  // staging-buffer requirement as long as no allocation fallback warning is emitted.
  if (have_old_box)
    dma_high_water_bytes = std::max(dma_high_water_bytes, stats.dirty_bytes);
  stats.dma_high_water_bytes = dma_high_water_bytes;

  display->mark_dirty(dirty_x0, dirty_y0, dirty_x1, dirty_y1);

  old_x0 = new_x0;
  old_y0 = new_y0;
  old_x1 = new_x1;
  old_y1 = new_y1;
  have_old_box = true;
  GOURAUD_LAST_STATS = stats;
  return stats;
}

}  // namespace esphome::mipi_spi::demo3d
