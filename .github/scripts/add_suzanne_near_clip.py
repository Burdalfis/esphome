#!/usr/bin/env python3
from pathlib import Path

p = Path('esphome/components/mipi_spi/suzanne_blender_uv_perspective.h')
s = p.read_text()

marker = 'static constexpr auto SUZANNE_BLENDER_UV_TEXTURE = make_suzanne_blender_uv_texture();\n\n'
helper = r'''static constexpr int32_t SUZANNE_PERSPECTIVE_NEAR_DEPTH = 256;

// Camera-space vertex used only while clipping a face against the near plane.
// UVs stay in Blender's Q7 atlas domain; perspective quantities are derived
// only after clipping, when depth is guaranteed positive and in range.
struct SuzanneNearClipVertex {
  int32_t x;
  int32_t y;
  int32_t depth;
  int32_t shade;
  int32_t u;
  int32_t v;
};

static inline SuzanneNearClipVertex suzanne_near_intersection(const SuzanneNearClipVertex &a,
                                                               const SuzanneNearClipVertex &b,
                                                               int32_t near_depth) {
  const int32_t den = b.depth - a.depth;
  const int32_t num = near_depth - a.depth;
  auto lerp = [num, den](int32_t av, int32_t bv) -> int32_t {
    if (den == 0)
      return av;
    return av + static_cast<int32_t>((static_cast<int64_t>(bv - av) * num) / den);
  };
  return {lerp(a.x, b.x), lerp(a.y, b.y), near_depth,
          lerp(a.shade, b.shade), lerp(a.u, b.u), lerp(a.v, b.v)};
}

// Sutherland-Hodgman clipping of one triangle against depth >= near_depth.
// A triangle can produce 0, 3, or 4 vertices; four vertices are rendered as
// two triangles by the caller. No heap storage is involved.
static inline size_t clip_suzanne_triangle_near(const std::array<SuzanneNearClipVertex, 3> &input,
                                                std::array<SuzanneNearClipVertex, 4> &output,
                                                int32_t near_depth = SUZANNE_PERSPECTIVE_NEAR_DEPTH) {
  size_t out_count = 0;
  SuzanneNearClipVertex previous = input[2];
  bool previous_inside = previous.depth >= near_depth;
  for (const auto &current : input) {
    const bool current_inside = current.depth >= near_depth;
    if (current_inside != previous_inside)
      output[out_count++] = suzanne_near_intersection(previous, current, near_depth);
    if (current_inside)
      output[out_count++] = current;
    previous = current;
    previous_inside = current_inside;
  }
  return out_count;
}

'''
if marker not in s:
    raise SystemExit('texture marker not found')
s = s.replace(marker, marker + helper, 1)

old_sig = 'PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y) {'
new_sig = ('PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y,\n'
           '                                                        int32_t camera_z = 500) {')
if old_sig not in s:
    raise SystemExit('renderer signature not found')
s = s.replace(old_sig, new_sig, 1)

if '  constexpr int32_t CAMERA_Z = 500;\n' not in s:
    raise SystemExit('camera constant not found')
s = s.replace('  constexpr int32_t CAMERA_Z = 500;\n', '', 1)
if 'const int32_t depth = CAMERA_Z + z2;' not in s:
    raise SystemExit('camera depth expression not found')
s = s.replace('const int32_t depth = CAMERA_Z + z2;', 'const int32_t depth = camera_z + z2;', 1)

old_project = r'''    const int32_t inv_w = static_cast<int32_t>((int64_t{1} << PERSPECTIVE_INV_W_BITS) / depth);

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
'''
new_project = r'''    auto &p = projected[i];
    p.cx = static_cast<int16_t>(x1);
    p.cy = static_cast<int16_t>(y2);
    p.cz = static_cast<int16_t>(z2);
    p.shade = shade;
    p.u_over_w = 0;
    p.v_over_w = 0;
    if (depth >= SUZANNE_PERSPECTIVE_NEAR_DEPTH) {
      p.inv_w = static_cast<int32_t>((int64_t{1} << PERSPECTIVE_INV_W_BITS) / depth);
      p.x = static_cast<int16_t>(center_x + static_cast<int32_t>((static_cast<int64_t>(x1) * FOCAL) / depth));
      p.y = static_cast<int16_t>(center_y - static_cast<int32_t>((static_cast<int64_t>(y2) * FOCAL) / depth));
    } else {
      // Never project a vertex on/behind the near plane. It will either be
      // discarded or replaced by a safe camera-space intersection below.
      p.inv_w = 0;
      p.x = 0;
      p.y = 0;
    }
  }

  bool have_new_box = false;
  int new_x0 = 0;
  int new_y0 = 0;
  int new_x1 = 0;
  int new_y1 = 0;
  auto extend_new_box = [&](const PerspectiveRenderVertex &v) {
    if (!have_new_box) {
      new_x0 = new_x1 = v.x;
      new_y0 = new_y1 = v.y;
      have_new_box = true;
    } else {
      new_x0 = std::min<int>(new_x0, v.x);
      new_y0 = std::min<int>(new_y0, v.y);
      new_x1 = std::max<int>(new_x1, v.x);
      new_y1 = std::max<int>(new_y1, v.y);
    }
  };
'''
if old_project not in s:
    raise SystemExit('projection/bounds block not found')
s = s.replace(old_project, new_project, 1)

start = s.index('  auto render_triangle = [&]')
end = s.index('\n\n  size_t uv_cursor = 0;', start)
new_render_triangle = r'''  auto render_triangle = [&](size_t ia, size_t ib, size_t ic,
                           const SuzanneBlenderUVQ7 &uva,
                           const SuzanneBlenderUVQ7 &uvb,
                           const SuzanneBlenderUVQ7 &uvc) {
    const auto &pa = projected[ia];
    const auto &pb = projected[ib];
    const auto &pc = projected[ic];

    // Backface cull in camera space before clipping. The face plane and winding
    // are unchanged by clipping, so this remains valid for a straddling face.
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
                             static_cast<int64_t>(nz) * (camera_z + pa.cz);
    if (view_dot >= 0)
      return;

    const std::array<SuzanneNearClipVertex, 3> clip_input = {{
        {pa.cx, pa.cy, camera_z + pa.cz, pa.shade, uva.u, uva.v},
        {pb.cx, pb.cy, camera_z + pb.cz, pb.shade, uvb.u, uvb.v},
        {pc.cx, pc.cy, camera_z + pc.cz, pc.shade, uvc.u, uvc.v},
    }};
    std::array<SuzanneNearClipVertex, 4> clipped{};
    const size_t clipped_count = clip_suzanne_triangle_near(clip_input, clipped);
    if (clipped_count < 3)
      return;

    auto project_clipped = [&](const SuzanneNearClipVertex &v) -> PerspectiveRenderVertex {
      PerspectiveRenderVertex p{};
      const int32_t inv_w = static_cast<int32_t>((int64_t{1} << PERSPECTIVE_INV_W_BITS) / v.depth);
      p.cx = static_cast<int16_t>(v.x);
      p.cy = static_cast<int16_t>(v.y);
      p.cz = static_cast<int16_t>(v.depth - camera_z);
      p.x = static_cast<int16_t>(center_x +
          static_cast<int32_t>((static_cast<int64_t>(v.x) * FOCAL) / v.depth));
      p.y = static_cast<int16_t>(center_y -
          static_cast<int32_t>((static_cast<int64_t>(v.y) * FOCAL) / v.depth));
      p.shade = static_cast<uint8_t>(std::clamp<int32_t>(v.shade, 0, 255));
      p.inv_w = inv_w;
      p.u_over_w = static_cast<int32_t>(static_cast<int64_t>(v.u) * inv_w);
      p.v_over_w = static_cast<int32_t>(static_cast<int64_t>(v.v) * inv_w);
      return p;
    };

    std::array<PerspectiveRenderVertex, 4> raster{};
    for (size_t i = 0; i < clipped_count; i++)
      raster[i] = project_clipped(clipped[i]);

    stats.visible_triangles++;
    for (size_t i = 1; i + 1 < clipped_count; i++) {
      extend_new_box(raster[0]);
      extend_new_box(raster[i]);
      extend_new_box(raster[i + 1]);
      fill_triangle(raster[0], raster[i], raster[i + 1]);
      stats.rasterized_triangles++;
    }
  };'''
s = s[:start] + new_render_triangle + s[end:]

old_dirty = r'''  int dirty_x0;
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
'''
new_dirty = r'''  if (have_new_box) {
    new_x0 -= 2;
    new_y0 -= 2;
    new_x1 += 2;
    new_y1 += 2;
  }

  int dirty_x0 = 0;
  int dirty_y0 = 0;
  int dirty_x1 = -1;
  int dirty_y1 = -1;
  if (!have_old_box) {
    // First frame always clears/transfers the whole display, including the case
    // where the entire object is behind the near plane.
    dirty_x1 = screen_w - 1;
    dirty_y1 = screen_h - 1;
  } else {
    int raw_x0 = old_x0;
    int raw_y0 = old_y0;
    int raw_x1 = old_x1;
    int raw_y1 = old_y1;
    if (have_new_box) {
      raw_x0 = std::min(raw_x0, new_x0);
      raw_y0 = std::min(raw_y0, new_y0);
      raw_x1 = std::max(raw_x1, new_x1);
      raw_y1 = std::max(raw_y1, new_y1);
    }
    dirty_x0 = std::max(0, raw_x0);
    dirty_y0 = std::max(0, raw_y0);
    dirty_x1 = std::min(screen_w - 1, raw_x1);
    dirty_y1 = std::min(screen_h - 1, raw_y1);
  }

  if (dirty_x0 <= dirty_x1 && dirty_y0 <= dirty_y1) {
    stats.dirty_bytes = static_cast<uint32_t>(dirty_x1 - dirty_x0 + 1) *
                        static_cast<uint32_t>(dirty_y1 - dirty_y0 + 1) * sizeof(PixelT);
    if (have_old_box)
      dma_high_water_bytes = std::max(dma_high_water_bytes, stats.dirty_bytes);
    display->mark_dirty(dirty_x0, dirty_y0, dirty_x1, dirty_y1);
  }
  stats.dma_high_water_bytes = dma_high_water_bytes;

  if (have_new_box) {
    old_x0 = new_x0;
    old_y0 = new_y0;
    old_x1 = new_x1;
    old_y1 = new_y1;
  }
  have_old_box = have_new_box;
'''
if old_dirty not in s:
    raise SystemExit('dirty block not found')
s = s.replace(old_dirty, new_dirty, 1)

p.write_text(s)
