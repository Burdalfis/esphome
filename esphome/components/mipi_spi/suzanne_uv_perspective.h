#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "suzanne_perspective.h"

namespace esphome::mipi_spi::demo3d {

// Seam-aware cylindrical unwrap for Suzanne.
//
// The model's Y axis is vertical and Z is front/back. U wraps once around Y,
// with U=0.5 at the front of the face and the seam placed at the back of the
// head. V maps the model's vertical extent to the texture height.
//
// Only the 271 vertices from the compact half-mesh need stored U coordinates.
// The mirrored half is reconstructed exactly in texture space. Coordinates are
// Q8.8 in texel units, so the U period is exactly 64 texels.
static constexpr int UVP_U_PERIOD_Q8 = TG_TEXTURE_SIZE << PERSPECTIVE_UV_FRAC_BITS;
static constexpr int UVP_U_HALF_PERIOD_Q8 = UVP_U_PERIOD_Q8 / 2;
static constexpr int UVP_V_MAX_Q8 = (TG_TEXTURE_SIZE - 1) << PERSPECTIVE_UV_FRAC_BITS;
static constexpr int UVP_MODEL_Y_MIN = -109;
static constexpr int UVP_MODEL_Y_MAX = 109;
static constexpr int UVP_MODEL_Y_SPAN = UVP_MODEL_Y_MAX - UVP_MODEL_Y_MIN;

static constexpr std::array<uint16_t, GOURAUD_HALF_VERTS> UVP_HALF_U_Q8 = {
  3160, 3613, 3836, 4270, 3865, 3154, 2687, 2968, 3218, 814, 1372, 2048,
  1812, 1250, 894, 1560, 1721, 2019, 2418, 2367, 2337, 3132, 3036, 2852,
  2870, 2411, 1964, 1745, 1961, 2709, 2523, 3257, 3216, 0, 0, 8192,
  8192, 8192, 8192, 0, 0, 0, 0, 0, 8192, 6040, 6575, 6971,
  7172, 7321, 7714, 8192, 4907, 4257, 3634, 2887, 2536, 1788, 1043, 558,
  329, 984, 1015, 2968, 3987, 3767, 3336, 2964, 2642, 2025, 1278, 8192,
  7798, 7829, 8008, 8192, 8192, 8192, 6627, 6877, 7438, 4401, 3866, 3385,
  2876, 2682, 1759, 1183, 829, 605, 5875, 7038, 7301, 7375, 7537, 7737,
  8192, 0, 0, 1572, 2248, 1465, 7752, 7739, 8192, 8192, 7331, 6809,
  6396, 7402, 8192, 7394, 6783, 7019, 7402, 8192, 6393, 6639, 6831, 6228,
  8192, 8051, 7895, 7864, 8192, 7868, 7892, 8049, 8192, 1737, 2284, 1730,
  1500, 2122, 2639, 2901, 3246, 3548, 3665, 2923, 1419, 1336, 1507, 1517,
  2854, 3509, 3447, 3136, 2872, 2626, 2124, 1603, 1802, 2265, 1855, 608,
  746, 1185, 1872, 2563, 2867, 3551, 4096, 4646, 0, 0, 8192, 8192,
  8192, 8192, 8192, 8192, 3396, 3166, 3233, 2116, 4262, 4637, 4128, 3675,
  6659, 7124, 7185, 6983, 7293, 7714, 7598, 7744, 6468, 6237, 5927, 6901,
  6211, 5159, 1256, 1183, 1275, 1883, 2767, 2476, 1927, 2493, 2378, 1843,
  1971, 2545, 2822, 3970, 3104, 5140, 5208, 2980, 4565, 4350, 3985, 3489,
  3078, 2960, 3087, 3176, 3510, 3928, 4195, 4317, 3127, 3283, 4165, 4096,
  3891, 3575, 3320, 3251, 3235, 3567, 3775, 3824, 4048, 4146, 4096, 4181,
  3954, 3445, 3404, 3425, 4146, 4048, 3851, 3778, 3572, 3298, 3361, 3600,
  3911, 4096, 4142, 3328, 3777, 3922, 3772, 3634, 3505, 3637, 3606, 3453,
  3043, 3160, 3538, 3987, 4311, 4506, 3003,
};

static_assert((TG_TEXTURE_SIZE & (TG_TEXTURE_SIZE - 1)) == 0,
              "Suzanne wrapped UV sampler expects a power-of-two texture");
static_assert(UVP_HALF_U_Q8.size() == GOURAUD_HALF_VERTS,
              "Suzanne cylindrical UV table must match the compact half mesh");

static constexpr int32_t uvp_base_u_q8(size_t index) {
  if (index < GOURAUD_HALF_VERTS)
    return UVP_HALF_U_Q8[index];

  const int32_t left_u = UVP_HALF_U_Q8[index - GOURAUD_HALF_VERTS];
  return (UVP_U_PERIOD_Q8 - left_u) & (UVP_U_PERIOD_Q8 - 1);
}

static constexpr int32_t uvp_base_v_q8(size_t index) {
  const int32_t y = GOURAUD_MODEL_VERTICES[index].y;
  return static_cast<int32_t>(
      (static_cast<int64_t>(UVP_MODEL_Y_MAX - y) * UVP_V_MAX_Q8 + UVP_MODEL_Y_SPAN / 2) /
      UVP_MODEL_Y_SPAN);
}

template<typename DisplayT>
PerspectiveStats render_suzanne_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {
  using PixelT = std::remove_pointer_t<decltype(display->get_framebuffer())>;
  static_assert(sizeof(PixelT) == 2, "Suzanne UV perspective demo expects a 16-bit framebuffer");

  PerspectiveStats stats{};
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

  static std::array<PerspectiveRenderVertex, GOURAUD_TOTAL_VERTS> projected{};
  const PixelT black = display->native_color(Color(0, 0, 0));

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

    const int32_t inv_w = static_cast<int32_t>((int64_t{1} << PERSPECTIVE_INV_W_BITS) / depth);
    const int32_t u_q8 = uvp_base_u_q8(i);
    const int32_t v_q8 = uvp_base_v_q8(i);

    auto &p = projected[i];
    p.cx = static_cast<int16_t>(x1);
    p.cy = static_cast<int16_t>(y2);
    p.cz = static_cast<int16_t>(z2);
    p.x = static_cast<int16_t>(center_x + static_cast<int32_t>((static_cast<int64_t>(x1) * FOCAL) / depth));
    p.y = static_cast<int16_t>(center_y - static_cast<int32_t>((static_cast<int64_t>(y2) * FOCAL) / depth));
    p.shade = shade;
    p.inv_w = inv_w;
    p.u_over_w = static_cast<int32_t>(static_cast<int64_t>(u_q8) * inv_w);
    p.v_over_w = static_cast<int32_t>(static_cast<int64_t>(v_q8) * inv_w);
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

  auto draw_span = [&](int y, int32_t xa, int32_t wa, int32_t sa, int32_t uowa, int32_t vowa,
                       int32_t xb, int32_t wb, int32_t sb, int32_t uowb, int32_t vowb) {
    if (y < 0 || y >= screen_h)
      return;
    if (xa > xb) {
      std::swap(xa, xb);
      std::swap(wa, wb);
      std::swap(sa, sb);
      std::swap(uowa, uowb);
      std::swap(vowa, vowb);
    }

    int x0 = xa >> 16;
    int x1 = xb >> 16;
    if (x1 < 0 || x0 >= screen_w)
      return;

    const int original_span = x1 - x0;
    int32_t w_step = 0;
    int32_t s_step = 0;
    int32_t uow_step = 0;
    int32_t vow_step = 0;
    if (original_span > 0) {
      w_step = (wb - wa) / original_span;
      s_step = (sb - sa) / original_span;
      uow_step = (uowb - uowa) / original_span;
      vow_step = (vowb - vowa) / original_span;
    }

    int32_t inv_w = wa;
    int32_t shade = sa;
    int32_t u_over_w = uowa;
    int32_t v_over_w = vowa;
    if (x0 < 0) {
      const int clipped = -x0;
      inv_w += clipped * w_step;
      shade += clipped * s_step;
      u_over_w += clipped * uow_step;
      v_over_w += clipped * vow_step;
      x0 = 0;
    }
    if (x1 >= screen_w)
      x1 = screen_w - 1;
    if (x0 > x1 || inv_w <= 0)
      return;

    PixelT *pixel = fb + static_cast<size_t>(y) * stride + x0;
    uint8_t *depth_pixel = z_buffer.data() + static_cast<size_t>(y) * screen_w + x0;

    int32_t u_start_q8 = u_over_w / inv_w;
    int32_t v_start_q8 = v_over_w / inv_w;
    int x = x0;
    while (x <= x1) {
      const int count = std::min(PERSPECTIVE_BLOCK_PIXELS, x1 - x + 1);
      const int32_t w_end = inv_w + w_step * count;
      const int32_t uow_end = u_over_w + uow_step * count;
      const int32_t vow_end = v_over_w + vow_step * count;
      if (w_end <= 0)
        break;

      const int32_t u_end_q8 = uow_end / w_end;
      const int32_t v_end_q8 = vow_end / w_end;
      int32_t u = u_start_q8 << (16 - PERSPECTIVE_UV_FRAC_BITS);
      int32_t v = v_start_q8 << (16 - PERSPECTIVE_UV_FRAC_BITS);
      const int32_t u_step =
          static_cast<int32_t>((static_cast<int64_t>(u_end_q8 - u_start_q8) << (16 - PERSPECTIVE_UV_FRAC_BITS)) /
                               count);
      const int32_t v_step =
          static_cast<int32_t>((static_cast<int64_t>(v_end_q8 - v_start_q8) << (16 - PERSPECTIVE_UV_FRAC_BITS)) /
                               count);

      for (int i = 0; i < count; i++, x++, pixel++, depth_pixel++) {
        const uint8_t iz =
            static_cast<uint8_t>(std::clamp<int32_t>(inv_w >> (PERSPECTIVE_INV_W_BITS - 16), 0, 255));
        if (iz > *depth_pixel) {
          *depth_pixel = iz;
          const int tex_u = (u >> 16) & (TG_TEXTURE_SIZE - 1);
          const int tex_v = std::clamp<int32_t>(v >> 16, 0, TG_TEXTURE_SIZE - 1);
          const uint8_t texel =
              TEXTURED_GOURAUD_TEXTURE[static_cast<size_t>(tex_v) * TG_TEXTURE_SIZE + tex_u];
          const uint8_t shade8 = static_cast<uint8_t>(std::clamp<int32_t>(shade >> 16, 0, 255));
          const uint8_t light = static_cast<uint8_t>(shade8 >> 4);
          *pixel = static_cast<PixelT>(lit_palette[static_cast<size_t>(light) * TG_PALETTE_SIZE + texel]);
        }

        inv_w += w_step;
        shade += s_step;
        u += u_step;
        v += v_step;
      }

      u_over_w = uow_end;
      v_over_w = vow_end;
      inv_w = w_end;
      u_start_q8 = u_end_q8;
      v_start_q8 = v_end_q8;
      stats.perspective_blocks++;
    }
  };

  auto fill_triangle = [&](const PerspectiveRenderVertex &va, const PerspectiveRenderVertex &vb,
                           const PerspectiveRenderVertex &vc) {
    struct ScanVertex {
      int x;
      int y;
      int w;
      int s;
      int32_t uow;
      int32_t vow;
    };

    ScanVertex v0{va.x, va.y, va.inv_w, va.shade, va.u_over_w, va.v_over_w};
    ScanVertex v1{vb.x, vb.y, vb.inv_w, vb.shade, vb.u_over_w, vb.v_over_w};
    ScanVertex v2{vc.x, vc.y, vc.inv_w, vc.shade, vc.u_over_w, vc.v_over_w};
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
    const int32_t long_w_step = (v2.w - v0.w) / long_dy;
    const int32_t long_s_step = static_cast<int32_t>((static_cast<int64_t>(v2.s - v0.s) << 16) / long_dy);
    const int32_t long_uow_step = (v2.uow - v0.uow) / long_dy;
    const int32_t long_vow_step = (v2.vow - v0.vow) / long_dy;
    int32_t long_x = v0.x << 16;
    int32_t long_w = v0.w;
    int32_t long_s = v0.s << 16;
    int32_t long_uow = v0.uow;
    int32_t long_vow = v0.vow;

    if (v1.y > v0.y) {
      const int short_dy = v1.y - v0.y;
      const int32_t short_x_step =
          static_cast<int32_t>((static_cast<int64_t>(v1.x - v0.x) << 16) / short_dy);
      const int32_t short_w_step = (v1.w - v0.w) / short_dy;
      const int32_t short_s_step =
          static_cast<int32_t>((static_cast<int64_t>(v1.s - v0.s) << 16) / short_dy);
      const int32_t short_uow_step = (v1.uow - v0.uow) / short_dy;
      const int32_t short_vow_step = (v1.vow - v0.vow) / short_dy;
      int32_t short_x = v0.x << 16;
      int32_t short_w = v0.w;
      int32_t short_s = v0.s << 16;
      int32_t short_uow = v0.uow;
      int32_t short_vow = v0.vow;
      const int top_end = (v1.y == v2.y) ? v1.y : v1.y - 1;
      for (int y = v0.y; y <= top_end; y++) {
        draw_span(y, long_x, long_w, long_s, long_uow, long_vow,
                  short_x, short_w, short_s, short_uow, short_vow);
        long_x += long_x_step;
        long_w += long_w_step;
        long_s += long_s_step;
        long_uow += long_uow_step;
        long_vow += long_vow_step;
        short_x += short_x_step;
        short_w += short_w_step;
        short_s += short_s_step;
        short_uow += short_uow_step;
        short_vow += short_vow_step;
      }
    }

    if (v2.y > v1.y) {
      const int short_dy = v2.y - v1.y;
      const int32_t short_x_step =
          static_cast<int32_t>((static_cast<int64_t>(v2.x - v1.x) << 16) / short_dy);
      const int32_t short_w_step = (v2.w - v1.w) / short_dy;
      const int32_t short_s_step =
          static_cast<int32_t>((static_cast<int64_t>(v2.s - v1.s) << 16) / short_dy);
      const int32_t short_uow_step = (v2.uow - v1.uow) / short_dy;
      const int32_t short_vow_step = (v2.vow - v1.vow) / short_dy;
      int32_t short_x = v1.x << 16;
      int32_t short_w = v1.w;
      int32_t short_s = v1.s << 16;
      int32_t short_uow = v1.uow;
      int32_t short_vow = v1.vow;
      for (int y = v1.y; y <= v2.y; y++) {
        draw_span(y, long_x, long_w, long_s, long_uow, long_vow,
                  short_x, short_w, short_s, short_uow, short_vow);
        long_x += long_x_step;
        long_w += long_w_step;
        long_s += long_s_step;
        long_uow += long_uow_step;
        long_vow += long_vow_step;
        short_x += short_x_step;
        short_w += short_w_step;
        short_s += short_s_step;
        short_uow += short_uow_step;
        short_vow += short_vow_step;
      }
    }
  };

  auto render_triangle = [&](size_t ia, size_t ib, size_t ic) {
    PerspectiveRenderVertex a = projected[ia];
    PerspectiveRenderVertex b = projected[ib];
    PerspectiveRenderVertex c = projected[ic];

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

    // Unwrap triangles that cross the cylindrical U seam at the back of the head.
    // The lower-U corners are lifted by one full texture period for interpolation,
    // then the sampler wraps them back with a power-of-two mask.
    const int32_t ua = uvp_base_u_q8(ia);
    const int32_t ub = uvp_base_u_q8(ib);
    const int32_t uc = uvp_base_u_q8(ic);
    const int32_t min_u = std::min(ua, std::min(ub, uc));
    const int32_t max_u = std::max(ua, std::max(ub, uc));
    if (max_u - min_u > UVP_U_HALF_PERIOD_Q8) {
      if (ua < UVP_U_HALF_PERIOD_Q8)
        a.u_over_w += UVP_U_PERIOD_Q8 * a.inv_w;
      if (ub < UVP_U_HALF_PERIOD_Q8)
        b.u_over_w += UVP_U_PERIOD_Q8 * b.inv_w;
      if (uc < UVP_U_HALF_PERIOD_Q8)
        c.u_over_w += UVP_U_PERIOD_Q8 * c.inv_w;
    }

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

  // Deliberately publish through the existing perspective stats accessor so the
  // benchmark lambda does not need another stats rewrite.
  PERSPECTIVE_LAST_STATS = stats;
  return stats;
}

}  // namespace esphome::mipi_spi::demo3d
