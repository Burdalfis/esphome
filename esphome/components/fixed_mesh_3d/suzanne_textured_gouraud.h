#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "suzanne_gouraud.h"

namespace esphome::mipi_spi::demo3d {

struct TexturedGouraudStats {
  uint16_t visible_triangles{0};
  uint16_t rasterized_triangles{0};
  uint32_t dirty_bytes{0};
  uint32_t dma_high_water_bytes{0};
};

struct TexturedGouraudUV {
  int32_t u;
  int32_t v;
};

struct TexturedGouraudRenderVertex {
  int16_t x;
  int16_t y;
  int16_t cx;
  int16_t cy;
  int16_t cz;
  uint8_t inv_z;
  uint8_t shade;
  int32_t u;
  int32_t v;
};

struct TexturedGouraudBounds {
  int16_t min_x;
  int16_t max_x;
  int16_t min_y;
  int16_t max_y;
};

static constexpr int TG_TEXTURE_SIZE = 64;
static constexpr int TG_LIGHT_LEVELS = 16;
static constexpr int TG_PALETTE_SIZE = 256;
static constexpr int32_t TG_TEXTURE_COORD_MAX = (TG_TEXTURE_SIZE - 1) << 16;

static constexpr TexturedGouraudBounds make_textured_gouraud_bounds() {
  TexturedGouraudBounds bounds{GOURAUD_MODEL_VERTICES[0].x, GOURAUD_MODEL_VERTICES[0].x,
                               GOURAUD_MODEL_VERTICES[0].y, GOURAUD_MODEL_VERTICES[0].y};
  for (size_t i = 1; i < GOURAUD_TOTAL_VERTS; i++) {
    const auto &v = GOURAUD_MODEL_VERTICES[i];
    if (v.x < bounds.min_x)
      bounds.min_x = v.x;
    if (v.x > bounds.max_x)
      bounds.max_x = v.x;
    if (v.y < bounds.min_y)
      bounds.min_y = v.y;
    if (v.y > bounds.max_y)
      bounds.max_y = v.y;
  }
  return bounds;
}

static constexpr auto TEXTURED_GOURAUD_BOUNDS = make_textured_gouraud_bounds();

static constexpr std::array<TexturedGouraudUV, GOURAUD_TOTAL_VERTS> make_textured_gouraud_uvs() {
  std::array<TexturedGouraudUV, GOURAUD_TOTAL_VERTS> uv{};
  const int32_t x_span = static_cast<int32_t>(TEXTURED_GOURAUD_BOUNDS.max_x) - TEXTURED_GOURAUD_BOUNDS.min_x;
  const int32_t y_span = static_cast<int32_t>(TEXTURED_GOURAUD_BOUNDS.max_y) - TEXTURED_GOURAUD_BOUNDS.min_y;
  for (size_t i = 0; i < GOURAUD_TOTAL_VERTS; i++) {
    const auto &m = GOURAUD_MODEL_VERTICES[i];
    const int64_t u_num = static_cast<int64_t>(static_cast<int32_t>(m.x) - TEXTURED_GOURAUD_BOUNDS.min_x) *
                          TG_TEXTURE_COORD_MAX;
    const int64_t v_num = static_cast<int64_t>(TEXTURED_GOURAUD_BOUNDS.max_y - static_cast<int32_t>(m.y)) *
                          TG_TEXTURE_COORD_MAX;
    uv[i].u = static_cast<int32_t>(u_num / x_span);
    uv[i].v = static_cast<int32_t>(v_num / y_span);
  }
  return uv;
}

static constexpr auto TEXTURED_GOURAUD_UVS = make_textured_gouraud_uvs();

// Full 8-bit indexed texture path. The current artwork only uses two indices, but the renderer and lighting LUT accept
// all 256 values so later textures can use the full palette without changing the inner loop.
static constexpr std::array<uint8_t, TG_TEXTURE_SIZE * TG_TEXTURE_SIZE> make_textured_gouraud_texture() {
  std::array<uint8_t, TG_TEXTURE_SIZE * TG_TEXTURE_SIZE> texture{};
  for (int y = 0; y < TG_TEXTURE_SIZE; y++) {
    for (int x = 0; x < TG_TEXTURE_SIZE; x++) {
      const bool bright = (((x >> 3) ^ (y >> 3)) & 1) != 0;
      texture[static_cast<size_t>(y) * TG_TEXTURE_SIZE + x] = bright ? 220 : 32;
    }
  }
  return texture;
}

static constexpr auto TEXTURED_GOURAUD_TEXTURE = make_textured_gouraud_texture();

struct TexturedGouraudRGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr TexturedGouraudRGB textured_gouraud_base_color(uint8_t index) {
  // A 256-entry blue/cyan ramp. The checkerboard currently selects two well-separated entries, while future artwork
  // can use the whole indexed range without changing the renderer.
  return {
      static_cast<uint8_t>(8 + (static_cast<uint16_t>(index) * 52) / 255),
      static_cast<uint8_t>(18 + (static_cast<uint16_t>(index) * 180) / 255),
      static_cast<uint8_t>(28 + (static_cast<uint16_t>(index) * 227) / 255),
  };
}

static constexpr uint16_t textured_gouraud_pack565_le(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}

static constexpr uint16_t textured_gouraud_pack565_be(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>((r & 0xF8) | g >> 5 | (g & 0x1C) << 11 | (b & 0xF8) << 5);
}

template<bool BIG_ENDIAN_PIXELS>
static constexpr std::array<uint16_t, TG_LIGHT_LEVELS * TG_PALETTE_SIZE> make_textured_gouraud_lit_palette() {
  std::array<uint16_t, TG_LIGHT_LEVELS * TG_PALETTE_SIZE> table{};
  for (int light = 0; light < TG_LIGHT_LEVELS; light++) {
    const int scale = light * 17;  // 0..255
    for (int index = 0; index < TG_PALETTE_SIZE; index++) {
      const auto base = textured_gouraud_base_color(static_cast<uint8_t>(index));
      const uint8_t r = static_cast<uint8_t>((static_cast<uint16_t>(base.r) * scale + 127) / 255);
      const uint8_t g = static_cast<uint8_t>((static_cast<uint16_t>(base.g) * scale + 127) / 255);
      const uint8_t b = static_cast<uint8_t>((static_cast<uint16_t>(base.b) * scale + 127) / 255);
      table[static_cast<size_t>(light) * TG_PALETTE_SIZE + index] =
          BIG_ENDIAN_PIXELS ? textured_gouraud_pack565_be(r, g, b) : textured_gouraud_pack565_le(r, g, b);
    }
  }
  return table;
}

// Two flash-resident tables keep the inner loop generic for either RGB565 byte order without spending 8 KiB of BSS.
static constexpr auto TEXTURED_GOURAUD_LIT_PALETTE_LE = make_textured_gouraud_lit_palette<false>();
static constexpr auto TEXTURED_GOURAUD_LIT_PALETTE_BE = make_textured_gouraud_lit_palette<true>();

static TexturedGouraudStats TEXTURED_GOURAUD_LAST_STATS{};
static inline const TexturedGouraudStats &get_suzanne_textured_gouraud_stats() {
  return TEXTURED_GOURAUD_LAST_STATS;
}

template<typename DisplayT>
TexturedGouraudStats render_suzanne_textured_gouraud(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {
  using PixelT = std::remove_pointer_t<decltype(display->get_framebuffer())>;
  static_assert(sizeof(PixelT) == 2, "Suzanne textured Gouraud demo expects a 16-bit framebuffer");

  TexturedGouraudStats stats{};
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

  static std::array<TexturedGouraudRenderVertex, GOURAUD_TOTAL_VERTS> projected{};
  const PixelT black = display->native_color(Color(0, 0, 0));

  // Detect the display's native RGB565 byte order once and then use a fully pre-lit flash table in the pixel loop.
  static bool palette_order_ready = false;
  static bool use_big_endian_palette = false;
  if (!palette_order_ready) {
    const PixelT native_red = display->native_color(Color(248, 0, 0));
    use_big_endian_palette = native_red == static_cast<PixelT>(0x00F8);
    palette_order_ready = true;
  }
  const auto &lit_palette =
      use_big_endian_palette ? TEXTURED_GOURAUD_LIT_PALETTE_BE : TEXTURED_GOURAUD_LIT_PALETTE_LE;

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
    const int32_t x1 =
        static_cast<int32_t>((static_cast<int64_t>(m.x) * cy + static_cast<int64_t>(m.z) * sy) >> 15);
    const int32_t z1 =
        static_cast<int32_t>((-static_cast<int64_t>(m.x) * sy + static_cast<int64_t>(m.z) * cy) >> 15);
    const int32_t y2 =
        static_cast<int32_t>((static_cast<int64_t>(m.y) * cx - static_cast<int64_t>(z1) * sx) >> 15);
    const int32_t z2 =
        static_cast<int32_t>((static_cast<int64_t>(m.y) * sx + static_cast<int64_t>(z1) * cx) >> 15);
    const int32_t depth = CAMERA_Z + z2;

    const auto &n = GOURAUD_VERTEX_NORMALS[i];
    const int32_t nx =
        static_cast<int32_t>((static_cast<int64_t>(n.x) * cy + static_cast<int64_t>(n.z) * sy) >> 15);
    const int32_t nz1 =
        static_cast<int32_t>((-static_cast<int64_t>(n.x) * sy + static_cast<int64_t>(n.z) * cy) >> 15);
    const int32_t ny =
        static_cast<int32_t>((static_cast<int64_t>(n.y) * cx - static_cast<int64_t>(nz1) * sx) >> 15);
    const int32_t nz =
        static_cast<int32_t>((static_cast<int64_t>(n.y) * sx + static_cast<int64_t>(nz1) * cx) >> 15);
    const int64_t light_dot_q30 = static_cast<int64_t>(nx) * LIGHT_X_Q15 +
                                  static_cast<int64_t>(ny) * LIGHT_Y_Q15 +
                                  static_cast<int64_t>(nz) * LIGHT_Z_Q15;
    const int32_t diffuse_q15 =
        static_cast<int32_t>(std::clamp<int64_t>(light_dot_q30 >> 15, 0, 32767));
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
    p.u = TEXTURED_GOURAUD_UVS[i].u;
    p.v = TEXTURED_GOURAUD_UVS[i].v;
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

  auto draw_span = [&](int y, int32_t xa, int32_t za, int32_t sa, int32_t ua, int32_t va,
                       int32_t xb, int32_t zb, int32_t sb, int32_t ub, int32_t vb) {
    if (y < 0 || y >= screen_h)
      return;
    if (xa > xb) {
      std::swap(xa, xb);
      std::swap(za, zb);
      std::swap(sa, sb);
      std::swap(ua, ub);
      std::swap(va, vb);
    }

    int x0 = xa >> 16;
    int x1 = xb >> 16;
    if (x1 < 0 || x0 >= screen_w)
      return;

    const int original_span = x1 - x0;
    int32_t z_step = 0;
    int32_t s_step = 0;
    int32_t u_step = 0;
    int32_t v_step = 0;
    if (original_span > 0) {
      z_step = (zb - za) / original_span;
      s_step = (sb - sa) / original_span;
      u_step = (ub - ua) / original_span;
      v_step = (vb - va) / original_span;
    }

    int32_t z = za;
    int32_t shade = sa;
    int32_t u = ua;
    int32_t v = va;
    if (x0 < 0) {
      const int clipped = -x0;
      z += clipped * z_step;
      shade += clipped * s_step;
      u += clipped * u_step;
      v += clipped * v_step;
      x0 = 0;
    }
    if (x1 >= screen_w)
      x1 = screen_w - 1;
    if (x0 > x1)
      return;

    PixelT *pixel = fb + static_cast<size_t>(y) * stride + x0;
    uint8_t *depth_pixel = z_buffer.data() + static_cast<size_t>(y) * screen_w + x0;
    for (int x = x0; x <= x1;
         x++, pixel++, depth_pixel++, z += z_step, shade += s_step, u += u_step, v += v_step) {
      const uint8_t iz = static_cast<uint8_t>(std::clamp<int32_t>(z >> 16, 0, 255));
      if (iz > *depth_pixel) {
        *depth_pixel = iz;
        const int tex_u = std::clamp<int32_t>(u >> 16, 0, TG_TEXTURE_SIZE - 1);
        const int tex_v = std::clamp<int32_t>(v >> 16, 0, TG_TEXTURE_SIZE - 1);
        const uint8_t texel = TEXTURED_GOURAUD_TEXTURE[static_cast<size_t>(tex_v) * TG_TEXTURE_SIZE + tex_u];
        const uint8_t shade8 = static_cast<uint8_t>(std::clamp<int32_t>(shade >> 16, 0, 255));
        const uint8_t light = static_cast<uint8_t>(shade8 >> 4);
        *pixel = static_cast<PixelT>(lit_palette[static_cast<size_t>(light) * TG_PALETTE_SIZE + texel]);
      }
    }
  };

  auto fill_triangle = [&](const TexturedGouraudRenderVertex &va, const TexturedGouraudRenderVertex &vb,
                           const TexturedGouraudRenderVertex &vc) {
    struct ScanVertex {
      int x;
      int y;
      int z;
      int s;
      int32_t u;
      int32_t v;
    };

    ScanVertex v0{va.x, va.y, va.inv_z, va.shade, va.u, va.v};
    ScanVertex v1{vb.x, vb.y, vb.inv_z, vb.shade, vb.u, vb.v};
    ScanVertex v2{vc.x, vc.y, vc.inv_z, vc.shade, vc.u, vc.v};
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
    const int32_t long_u_step = static_cast<int32_t>(static_cast<int64_t>(v2.u - v0.u) / long_dy);
    const int32_t long_v_step = static_cast<int32_t>(static_cast<int64_t>(v2.v - v0.v) / long_dy);
    int32_t long_x = v0.x << 16;
    int32_t long_z = v0.z << 16;
    int32_t long_s = v0.s << 16;
    int32_t long_u = v0.u;
    int32_t long_v = v0.v;

    if (v1.y > v0.y) {
      const int short_dy = v1.y - v0.y;
      const int32_t short_x_step = static_cast<int32_t>((static_cast<int64_t>(v1.x - v0.x) << 16) / short_dy);
      const int32_t short_z_step = static_cast<int32_t>((static_cast<int64_t>(v1.z - v0.z) << 16) / short_dy);
      const int32_t short_s_step = static_cast<int32_t>((static_cast<int64_t>(v1.s - v0.s) << 16) / short_dy);
      const int32_t short_u_step = static_cast<int32_t>(static_cast<int64_t>(v1.u - v0.u) / short_dy);
      const int32_t short_v_step = static_cast<int32_t>(static_cast<int64_t>(v1.v - v0.v) / short_dy);
      int32_t short_x = v0.x << 16;
      int32_t short_z = v0.z << 16;
      int32_t short_s = v0.s << 16;
      int32_t short_u = v0.u;
      int32_t short_v = v0.v;
      const int top_end = (v1.y == v2.y) ? v1.y : v1.y - 1;
      for (int y = v0.y; y <= top_end; y++) {
        draw_span(y, long_x, long_z, long_s, long_u, long_v,
                  short_x, short_z, short_s, short_u, short_v);
        long_x += long_x_step;
        long_z += long_z_step;
        long_s += long_s_step;
        long_u += long_u_step;
        long_v += long_v_step;
        short_x += short_x_step;
        short_z += short_z_step;
        short_s += short_s_step;
        short_u += short_u_step;
        short_v += short_v_step;
      }
    }

    if (v2.y > v1.y) {
      const int short_dy = v2.y - v1.y;
      const int32_t short_x_step = static_cast<int32_t>((static_cast<int64_t>(v2.x - v1.x) << 16) / short_dy);
      const int32_t short_z_step = static_cast<int32_t>((static_cast<int64_t>(v2.z - v1.z) << 16) / short_dy);
      const int32_t short_s_step = static_cast<int32_t>((static_cast<int64_t>(v2.s - v1.s) << 16) / short_dy);
      const int32_t short_u_step = static_cast<int32_t>(static_cast<int64_t>(v2.u - v1.u) / short_dy);
      const int32_t short_v_step = static_cast<int32_t>(static_cast<int64_t>(v2.v - v1.v) / short_dy);
      int32_t short_x = v1.x << 16;
      int32_t short_z = v1.z << 16;
      int32_t short_s = v1.s << 16;
      int32_t short_u = v1.u;
      int32_t short_v = v1.v;
      for (int y = v1.y; y <= v2.y; y++) {
        draw_span(y, long_x, long_z, long_s, long_u, long_v,
                  short_x, short_z, short_s, short_u, short_v);
        long_x += long_x_step;
        long_z += long_z_step;
        long_s += long_s_step;
        long_u += long_u_step;
        long_v += long_v_step;
        short_x += short_x_step;
        short_z += short_z_step;
        short_s += short_s_step;
        short_u += short_u_step;
        short_v += short_v_step;
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
  if (have_old_box)
    dma_high_water_bytes = std::max(dma_high_water_bytes, stats.dirty_bytes);
  stats.dma_high_water_bytes = dma_high_water_bytes;

  display->mark_dirty(dirty_x0, dirty_y0, dirty_x1, dirty_y1);

  old_x0 = new_x0;
  old_y0 = new_y0;
  old_x1 = new_x1;
  old_y1 = new_y1;
  have_old_box = true;
  TEXTURED_GOURAUD_LAST_STATS = stats;
  return stats;
}

}  // namespace esphome::mipi_spi::demo3d
