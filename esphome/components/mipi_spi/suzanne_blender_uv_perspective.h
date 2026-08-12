#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "suzanne_perspective.h"
#include "suzanne_blender_uv_data.h"

namespace esphome::mipi_spi::demo3d {

// Blender's artist-authored face-corner UV atlas.
//
// UVs are deliberately NOT stored in PerspectiveRenderVertex: one geometric
// vertex can have several UVs at island seams. Shared projected positions and
// normals therefore remain exactly the same size as the previous renderer; the
// three face-corner UVs are applied only when a visible triangle is submitted.
static constexpr size_t suzanne_blender_expected_uv_count() {
  size_t count = 0;
  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];
    count += (quad ? 4u : 3u) * 2u;
  }
  return count;
}

static_assert(suzanne_blender_expected_uv_count() == SUZANNE_BLENDER_UV_COUNT,
              "Blender Suzanne UV stream must match the compact mirrored topology");

// 128x128 indexed UV diagnostic texture. This lives entirely in flash. The
// asymmetric U/V gradient, checker field and 8/32-texel grid lines make island
// orientation, scale and seams easy to see on the real Blender unwrap.
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

template<typename DisplayT>
PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {
  using PixelT = std::remove_pointer_t<decltype(display->get_framebuffer())>;
  static_assert(sizeof(PixelT) == 2, "Suzanne Blender UV perspective demo expects a 16-bit framebuffer");

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

    auto &p = projected[i];
    p.cx = static_cast<int16_t>(x1);
    p.cy = static_cast<int16_t>(y2);
    p.cz = static_cast<int16_t>(z2);
    p.x = static_cast<int16_t>(center_x + static_cast<int32_t>((static_cast<int64_t>(x1) * FOCAL) / depth));
    p.y = static_cast<int16_t>(center_y - static_cast<int32_t>((static_cast<int64_t>(y2) * FOCAL) / depth));
    p.shade = shade;
    p.inv_w = inv_w;
    p.u_over_w = 0;
    p.v_over_w = 0;
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
      int32_t u = u_start_q8 << (16 - SUZANNE_BLENDER_UV_FRAC_BITS);
      int32_t v = v_start_q8 << (16 - SUZANNE_BLENDER_UV_FRAC_BITS);
      const int32_t u_step =
          static_cast<int32_t>((static_cast<int64_t>(u_end_q8 - u_start_q8) << (16 - SUZANNE_BLENDER_UV_FRAC_BITS)) /
                               count);
      const int32_t v_step =
          static_cast<int32_t>((static_cast<int64_t>(v_end_q8 - v_start_q8) << (16 - SUZANNE_BLENDER_UV_FRAC_BITS)) /
                               count);

      for (int i = 0; i < count; i++, x++, pixel++, depth_pixel++) {
        const uint8_t iz =
            static_cast<uint8_t>(std::clamp<int32_t>(inv_w >> (PERSPECTIVE_INV_W_BITS - 16), 0, 255));
        if (iz > *depth_pixel) {
          *depth_pixel = iz;
          const int tex_u = std::clamp<int32_t>(u >> 16, 0, SUZANNE_BLENDER_TEXTURE_SIZE - 1);
          const int tex_v = std::clamp<int32_t>(v >> 16, 0, SUZANNE_BLENDER_TEXTURE_SIZE - 1);
          const uint8_t texel = SUZANNE_BLENDER_UV_TEXTURE[
              static_cast<size_t>(tex_v) * SUZANNE_BLENDER_TEXTURE_SIZE + tex_u];
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

  auto render_triangle = [&](size_t ia, size_t ib, size_t ic,
                           const SuzanneBlenderUVQ7 &uva,
                           const SuzanneBlenderUVQ7 &uvb,
                           const SuzanneBlenderUVQ7 &uvc) {
    const auto &pa = projected[ia];
    const auto &pb = projected[ib];
    const auto &pc = projected[ic];

    const int32_t e1x = pb.cx - pa.cx;
    const int32_t e1y = pb.cy - pa.cy;
    const int32_t e1z = pb.cz - pa.cz;
    const int32_t e2x = pc.cx - pa.cx;
    const int32_t e2y = pc.cy - pa.cy;
    const int32_t e2z = pc.cz - pa.cz;
    const int32_t nx = e1y * e2z - e1z * e2y;
    const int32_t ny = e1z * e2x - e1x * e2z;
    const int32_t nz = e1x * e2y - e1y * e2x;
    const int64_t view_dot = static_cast<int64_t>(nx) * pa.cx + static_cast<int64_t>(ny) * pa.cy +
                             static_cast<int64_t>(nz) * (CAMERA_Z + pa.cz);
    if (view_dot >= 0)
      return;

    PerspectiveRenderVertex a = pa;
    PerspectiveRenderVertex b = pb;
    PerspectiveRenderVertex c = pc;
    a.u_over_w = static_cast<int32_t>(uva.u) * a.inv_w;
    a.v_over_w = static_cast<int32_t>(uva.v) * a.inv_w;
    b.u_over_w = static_cast<int32_t>(uvb.u) * b.inv_w;
    b.v_over_w = static_cast<int32_t>(uvb.v) * b.inv_w;
    c.u_over_w = static_cast<int32_t>(uvc.u) * c.inv_w;
    c.v_over_w = static_cast<int32_t>(uvc.v) * c.inv_w;

    stats.visible_triangles++;
    fill_triangle(a, b, c);
    stats.rasterized_triangles++;
  };

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

    render_triangle(a, b, c, left_uv[0], left_uv[1], left_uv[2]);
    if (quad)
      render_triangle(a, c, d, left_uv[0], left_uv[2], left_uv[3]);

    const size_t ma = GOURAUD_HALF_VERTS + c;
    const size_t mb = GOURAUD_HALF_VERTS + b;
    const size_t mc = GOURAUD_HALF_VERTS + a;
    const size_t md = GOURAUD_HALF_VERTS + d;
    render_triangle(ma, mb, mc, right_uv[0], right_uv[1], right_uv[2]);
    if (quad)
      render_triangle(ma, mc, md, right_uv[0], right_uv[2], right_uv[3]);

    uv_cursor += corner_count * 2u;
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
