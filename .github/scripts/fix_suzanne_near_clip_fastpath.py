#!/usr/bin/env python3
from pathlib import Path

p = Path('esphome/components/mipi_spi/suzanne_blender_uv_perspective.h')
s = p.read_text()

old_state = '''  static bool have_old_box = false;\n  static int old_x0 = 0;\n  static int old_y0 = 0;\n  static int old_x1 = 0;\n  static int old_y1 = 0;\n  static uint32_t dma_high_water_bytes = 0;\n\n  if (!have_old_box)\n    std::fill_n(fb, static_cast<size_t>(stride) * screen_h, black);\n  else\n    fill_rect(old_x0, old_y0, old_x1, old_y1, black);\n'''
new_state = '''  static bool frame_initialized = false;\n  static bool have_old_box = false;\n  static int old_x0 = 0;\n  static int old_y0 = 0;\n  static int old_x1 = 0;\n  static int old_y1 = 0;\n  static uint32_t dma_high_water_bytes = 0;\n\n  if (!frame_initialized)\n    std::fill_n(fb, static_cast<size_t>(stride) * screen_h, black);\n  else if (have_old_box)\n    fill_rect(old_x0, old_y0, old_x1, old_y1, black);\n'''
if old_state not in s:
    raise SystemExit('state block not found')
s = s.replace(old_state, new_state, 1)

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
    const int32_t da = camera_z + pa.cz;
    const int32_t db = camera_z + pb.cz;
    const int32_t dc = camera_z + pc.cz;
    const int64_t view_dot = static_cast<int64_t>(nx) * pa.cx + static_cast<int64_t>(ny) * pa.cy +
                             static_cast<int64_t>(nz) * da;
    if (view_dot >= 0)
      return;

    const bool ia_front = da >= SUZANNE_PERSPECTIVE_NEAR_DEPTH;
    const bool ib_front = db >= SUZANNE_PERSPECTIVE_NEAR_DEPTH;
    const bool ic_front = dc >= SUZANNE_PERSPECTIVE_NEAR_DEPTH;
    if (!ia_front && !ib_front && !ic_front)
      return;

    stats.visible_triangles++;

    // Hot path: the ordinary demo lives entirely in front of the near plane.
    // Preserve the shared per-vertex projection work and only attach this
    // face's UVs here, exactly as before clipping support was added.
    if (ia_front && ib_front && ic_front) {
      PerspectiveRenderVertex a = pa;
      PerspectiveRenderVertex b = pb;
      PerspectiveRenderVertex c = pc;
      a.u_over_w = static_cast<int32_t>(static_cast<int64_t>(uva.u) * a.inv_w);
      a.v_over_w = static_cast<int32_t>(static_cast<int64_t>(uva.v) * a.inv_w);
      b.u_over_w = static_cast<int32_t>(static_cast<int64_t>(uvb.u) * b.inv_w);
      b.v_over_w = static_cast<int32_t>(static_cast<int64_t>(uvb.v) * b.inv_w);
      c.u_over_w = static_cast<int32_t>(static_cast<int64_t>(uvc.u) * c.inv_w);
      c.v_over_w = static_cast<int32_t>(static_cast<int64_t>(uvc.v) * c.inv_w);
      extend_new_box(a);
      extend_new_box(b);
      extend_new_box(c);
      fill_triangle(a, b, c);
      stats.rasterized_triangles++;
      return;
    }

    // Slow path: only a triangle that actually crosses the plane gets clipped
    // and has its generated intersections projected.
    const std::array<SuzanneNearClipVertex, 3> clip_input = {{
        {pa.cx, pa.cy, da, pa.shade, uva.u, uva.v},
        {pb.cx, pb.cy, db, pb.shade, uvb.u, uvb.v},
        {pc.cx, pc.cy, dc, pc.shade, uvc.u, uvc.v},
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

    for (size_t i = 1; i + 1 < clipped_count; i++) {
      extend_new_box(raster[0]);
      extend_new_box(raster[i]);
      extend_new_box(raster[i + 1]);
      fill_triangle(raster[0], raster[i], raster[i + 1]);
      stats.rasterized_triangles++;
    }
  };'''
s = s[:start] + new_render_triangle + s[end:]

start = s.index('  int dirty_x0 = 0;')
end = s.index('\n\n  if (have_new_box) {', start)
new_dirty = r'''  int dirty_x0 = 0;
  int dirty_y0 = 0;
  int dirty_x1 = -1;
  int dirty_y1 = -1;
  if (!frame_initialized) {
    // First frame always transfers the whole framebuffer once.
    dirty_x1 = screen_w - 1;
    dirty_y1 = screen_h - 1;
  } else if (have_old_box) {
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
  } else if (have_new_box) {
    // Nothing was drawn last frame, so the framebuffer is already black; only
    // the newly occupied region needs to be transferred.
    dirty_x0 = std::max(0, new_x0);
    dirty_y0 = std::max(0, new_y0);
    dirty_x1 = std::min(screen_w - 1, new_x1);
    dirty_y1 = std::min(screen_h - 1, new_y1);
  }

  if (dirty_x0 <= dirty_x1 && dirty_y0 <= dirty_y1) {
    stats.dirty_bytes = static_cast<uint32_t>(dirty_x1 - dirty_x0 + 1) *
                        static_cast<uint32_t>(dirty_y1 - dirty_y0 + 1) * sizeof(PixelT);
    if (frame_initialized)
      dma_high_water_bytes = std::max(dma_high_water_bytes, stats.dirty_bytes);
    display->mark_dirty(dirty_x0, dirty_y0, dirty_x1, dirty_y1);
  }
  stats.dma_high_water_bytes = dma_high_water_bytes;'''
s = s[:start] + new_dirty + s[end:]

needle = '  have_old_box = have_new_box;\n'
if needle not in s:
    raise SystemExit('old-box assignment not found')
s = s.replace(needle, '  frame_initialized = true;\n  have_old_box = have_new_box;\n', 1)

p.write_text(s)
