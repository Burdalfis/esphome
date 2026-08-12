#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace esphome::mipi_spi::demo3d {

// Generic fixed-point indexed-mesh software renderer. Geometry and materials
// are intentionally plain flash-friendly data; persistent RAM is limited to
// one reusable projected-vertex scratch buffer and the 8-bit Z buffer.
static constexpr int FIXED_MESH_PERSPECTIVE_BLOCK_PIXELS = 8;
static constexpr int FIXED_MESH_INV_W_BITS = 24;
static constexpr int FIXED_MESH_LIGHT_LEVELS = 16;
static constexpr int FIXED_MESH_PALETTE_SIZE = 256;

struct FixedMeshUV {
  uint16_t u;
  uint16_t v;
};

// UVs are face-corner attributes rather than per geometric vertex, so seams
// need no duplicated transformed/projected vertices.
struct FixedMeshTriangle {
  uint16_t a;
  uint16_t b;
  uint16_t c;
  FixedMeshUV uv_a;
  FixedMeshUV uv_b;
  FixedMeshUV uv_c;
};

template<typename PositionT, typename NormalT> struct FixedMeshView {
  const PositionT *positions{nullptr};
  const NormalT *normals{nullptr};
  size_t vertex_count{0};
  const FixedMeshTriangle *triangles{nullptr};
  size_t triangle_count{0};
};

struct FixedMeshIndexedMaterial {
  const uint8_t *texture{nullptr};
  uint16_t texture_width{0};
  uint16_t texture_height{0};
  uint8_t uv_frac_bits{0};
  const uint16_t *lit_palette_le{nullptr};
  const uint16_t *lit_palette_be{nullptr};
};

struct FixedMeshDrawParams {
  uint8_t phase_x{0};
  uint8_t phase_y{0};
  uint8_t phase_z{0};
  int32_t translate_x{0};
  int32_t translate_y{0};
  int32_t translate_z{500};
  int32_t focal{220};
  int32_t near_depth{256};
  int32_t light_x_q15{-10733};
  int32_t light_y_q15{17173};
  int32_t light_z_q15{-25760};
  uint8_t ambient{48};
  uint8_t diffuse{207};
};

struct FixedMeshStats {
  uint32_t meshes_drawn{0};
  uint32_t input_triangles{0};
  uint32_t visible_triangles{0};
  uint32_t clipped_triangles{0};
  uint32_t rasterized_triangles{0};
  uint32_t perspective_blocks{0};
  uint32_t dirty_bytes{0};
  uint32_t dma_high_water_bytes{0};
};

static constexpr int16_t FIXED_MESH_SIN_Q15[65] = {
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

static inline int32_t fixed_mesh_sin_q15(uint8_t phase) {
  const uint8_t quadrant = phase >> 6;
  const uint8_t index = phase & 0x3F;
  switch (quadrant) {
    case 0:
      return FIXED_MESH_SIN_Q15[index];
    case 1:
      return FIXED_MESH_SIN_Q15[64 - index];
    case 2:
      return -FIXED_MESH_SIN_Q15[index];
    default:
      return -FIXED_MESH_SIN_Q15[64 - index];
  }
}

struct FixedMeshProjectedVertex {
  // Screen coordinates only need int16 range; camera-space coordinates stay
  // int32 so translated/clipped objects remain robust. This keeps the reusable
  // projected scratch vertex at 24 bytes, matching the old Suzanne path.
  int16_t x;
  int16_t y;
  int32_t cx;
  int32_t cy;
  int32_t depth;
  int32_t inv_w;
  uint8_t shade;
};
static_assert(sizeof(FixedMeshProjectedVertex) == 24,
              "Projected fixed-mesh vertex should stay at 24 bytes");

struct FixedMeshRasterVertex {
  int32_t x;
  int32_t y;
  int32_t inv_w;
  uint8_t shade;
  int32_t u_over_w;
  int32_t v_over_w;
};

struct FixedMeshClipVertex {
  int32_t x;
  int32_t y;
  int32_t depth;
  int32_t shade;
  int32_t u;
  int32_t v;
};

static inline FixedMeshClipVertex fixed_mesh_near_intersection(const FixedMeshClipVertex &a,
                                                                const FixedMeshClipVertex &b,
                                                                int32_t near_depth) {
  const int32_t denominator = b.depth - a.depth;
  const int32_t numerator = near_depth - a.depth;
  auto lerp = [numerator, denominator](int32_t av, int32_t bv) -> int32_t {
    if (denominator == 0)
      return av;
    return av + static_cast<int32_t>((static_cast<int64_t>(bv - av) * numerator) / denominator);
  };
  return {lerp(a.x, b.x), lerp(a.y, b.y), near_depth,
          lerp(a.shade, b.shade), lerp(a.u, b.u), lerp(a.v, b.v)};
}

// Clip one triangle against depth >= near_depth. Output is 0, 3 or 4
// vertices; a four-vertex result is emitted as a two-triangle fan.
static inline size_t fixed_mesh_clip_triangle_near(const FixedMeshClipVertex (&input)[3],
                                                   FixedMeshClipVertex (&output)[4],
                                                   int32_t near_depth) {
  size_t out_count = 0;
  FixedMeshClipVertex previous = input[2];
  bool previous_inside = previous.depth >= near_depth;
  for (const auto &current : input) {
    const bool current_inside = current.depth >= near_depth;
    if (current_inside != previous_inside)
      output[out_count++] = fixed_mesh_near_intersection(previous, current, near_depth);
    if (current_inside)
      output[out_count++] = current;
    previous = current;
    previous_inside = current_inside;
  }
  return out_count;
}

template<typename DisplayT, size_t MAX_VERTICES> class FixedMeshRenderer {
 public:
  using PixelT = std::remove_pointer_t<decltype(std::declval<DisplayT *>()->get_framebuffer())>;
  static_assert(sizeof(PixelT) == 2, "FixedMeshRenderer requires a 16-bit framebuffer");

  bool begin_frame(DisplayT *display) {
    this->display_ = display;
    this->active_ = false;
    this->stats_ = {};
    this->have_new_box_ = false;
    if (display == nullptr)
      return false;

    this->fb_ = display->get_framebuffer();
    if (this->fb_ == nullptr)
      return false;

    const int new_w = display->get_width();
    const int new_h = display->get_height();
    const int new_stride = static_cast<int>(display->get_framebuffer_stride());
    if (new_w <= 0 || new_h <= 0 || new_stride <= 0)
      return false;

    if (new_w != this->screen_w_ || new_h != this->screen_h_ || new_stride != this->stride_) {
      this->screen_w_ = new_w;
      this->screen_h_ = new_h;
      this->stride_ = new_stride;
      this->frame_initialized_ = false;
      this->have_old_box_ = false;
      this->palette_order_ready_ = false;
    }

    const size_t z_size = static_cast<size_t>(this->screen_w_) * this->screen_h_;
    if (this->z_buffer_.size() != z_size)
      this->z_buffer_.resize(z_size);
    std::memset(this->z_buffer_.data(), 0, z_size);

    if (!this->palette_order_ready_) {
      const PixelT native_red = display->native_color(Color(248, 0, 0));
      this->use_big_endian_palette_ = native_red == static_cast<PixelT>(0x00F8);
      this->palette_order_ready_ = true;
    }
    this->black_ = display->native_color(Color(0, 0, 0));

    if (!this->frame_initialized_)
      std::fill_n(this->fb_, static_cast<size_t>(this->stride_) * this->screen_h_, this->black_);
    else if (this->have_old_box_)
      this->fill_rect_(this->old_x0_, this->old_y0_, this->old_x1_, this->old_y1_, this->black_);

    this->active_ = true;
    return true;
  }

  template<typename PositionT, typename NormalT>
  void draw_mesh(const FixedMeshView<PositionT, NormalT> &mesh,
                 const FixedMeshIndexedMaterial &material,
                 const FixedMeshDrawParams &params = {}) {
    if (!this->active_ || mesh.positions == nullptr || mesh.normals == nullptr ||
        mesh.triangles == nullptr || mesh.vertex_count == 0 || mesh.triangle_count == 0 ||
        material.texture == nullptr || material.lit_palette_le == nullptr || material.lit_palette_be == nullptr ||
        material.texture_width == 0 || material.texture_height == 0 || material.uv_frac_bits > 16 ||
        mesh.vertex_count > MAX_VERTICES || params.near_depth <= 0 || params.focal <= 0)
      return;

    this->stats_.meshes_drawn++;
    this->stats_.input_triangles += static_cast<uint32_t>(mesh.triangle_count);
    const int32_t sx = fixed_mesh_sin_q15(params.phase_x);
    const int32_t cx = fixed_mesh_sin_q15(static_cast<uint8_t>(params.phase_x + 64));
    const int32_t sy = fixed_mesh_sin_q15(params.phase_y);
    const int32_t cy = fixed_mesh_sin_q15(static_cast<uint8_t>(params.phase_y + 64));
    const bool rotate_z = params.phase_z != 0;
    const int32_t sz = rotate_z ? fixed_mesh_sin_q15(params.phase_z) : 0;
    const int32_t cz = rotate_z ? fixed_mesh_sin_q15(static_cast<uint8_t>(params.phase_z + 64)) : 32767;
    const int center_x = this->screen_w_ / 2;
    const int center_y = this->screen_h_ / 2;

    for (size_t i = 0; i < mesh.vertex_count; i++) {
      const auto &m = mesh.positions[i];
      const int32_t mx = static_cast<int32_t>(m.x);
      const int32_t my = static_cast<int32_t>(m.y);
      const int32_t mz = static_cast<int32_t>(m.z);
      const int32_t x1 = static_cast<int32_t>((static_cast<int64_t>(mx) * cy +
                                               static_cast<int64_t>(mz) * sy) >> 15);
      const int32_t z1 = static_cast<int32_t>((-static_cast<int64_t>(mx) * sy +
                                               static_cast<int64_t>(mz) * cy) >> 15);
      int32_t rx = x1;
      int32_t ry = static_cast<int32_t>((static_cast<int64_t>(my) * cx -
                                         static_cast<int64_t>(z1) * sx) >> 15);
      int32_t rz = static_cast<int32_t>((static_cast<int64_t>(my) * sx +
                                         static_cast<int64_t>(z1) * cx) >> 15);
      if (rotate_z) {
        const int32_t zx = static_cast<int32_t>((static_cast<int64_t>(rx) * cz -
                                                 static_cast<int64_t>(ry) * sz) >> 15);
        const int32_t zy = static_cast<int32_t>((static_cast<int64_t>(rx) * sz +
                                                 static_cast<int64_t>(ry) * cz) >> 15);
        rx = zx;
        ry = zy;
      }
      const int32_t camera_x = rx + params.translate_x;
      const int32_t camera_y = ry + params.translate_y;
      const int32_t depth = rz + params.translate_z;

      const auto &n = mesh.normals[i];
      const int32_t nx0 = static_cast<int32_t>(n.x);
      const int32_t ny0 = static_cast<int32_t>(n.y);
      const int32_t nz0 = static_cast<int32_t>(n.z);
      const int32_t nx1 = static_cast<int32_t>((static_cast<int64_t>(nx0) * cy +
                                                static_cast<int64_t>(nz0) * sy) >> 15);
      const int32_t nz1 = static_cast<int32_t>((-static_cast<int64_t>(nx0) * sy +
                                                static_cast<int64_t>(nz0) * cy) >> 15);
      int32_t nx = nx1;
      int32_t ny = static_cast<int32_t>((static_cast<int64_t>(ny0) * cx -
                                         static_cast<int64_t>(nz1) * sx) >> 15);
      int32_t nz = static_cast<int32_t>((static_cast<int64_t>(ny0) * sx +
                                         static_cast<int64_t>(nz1) * cx) >> 15);
      if (rotate_z) {
        const int32_t znx = static_cast<int32_t>((static_cast<int64_t>(nx) * cz -
                                                  static_cast<int64_t>(ny) * sz) >> 15);
        const int32_t zny = static_cast<int32_t>((static_cast<int64_t>(nx) * sz +
                                                  static_cast<int64_t>(ny) * cz) >> 15);
        nx = znx;
        ny = zny;
      }
      const int64_t light_dot_q30 = static_cast<int64_t>(nx) * params.light_x_q15 +
                                    static_cast<int64_t>(ny) * params.light_y_q15 +
                                    static_cast<int64_t>(nz) * params.light_z_q15;
      const int32_t diffuse_q15 =
          static_cast<int32_t>(std::clamp<int64_t>(light_dot_q30 >> 15, 0, 32767));
      const uint8_t shade = static_cast<uint8_t>(std::clamp(
          static_cast<int>(params.ambient) +
              static_cast<int>((static_cast<int64_t>(diffuse_q15) * params.diffuse + 16384) >> 15),
          0, 255));

      auto &p = this->projected_[i];
      p.cx = camera_x;
      p.cy = camera_y;
      p.depth = depth;
      p.shade = shade;
      if (depth >= params.near_depth) {
        p.inv_w = static_cast<int32_t>((int64_t{1} << FIXED_MESH_INV_W_BITS) / depth);
        const int32_t screen_x = center_x +
            static_cast<int32_t>((static_cast<int64_t>(camera_x) * params.focal) / depth);
        const int32_t screen_y = center_y -
            static_cast<int32_t>((static_cast<int64_t>(camera_y) * params.focal) / depth);
        p.x = static_cast<int16_t>(std::clamp<int32_t>(screen_x, -32768, 32767));
        p.y = static_cast<int16_t>(std::clamp<int32_t>(screen_y, -32768, 32767));
      } else {
        p.inv_w = 0;
        p.x = 0;
        p.y = 0;
      }
    }

    const uint16_t *lit_palette =
        this->use_big_endian_palette_ ? material.lit_palette_be : material.lit_palette_le;

    for (size_t triangle_index = 0; triangle_index < mesh.triangle_count; triangle_index++) {
      const auto &triangle = mesh.triangles[triangle_index];
      if (triangle.a >= mesh.vertex_count || triangle.b >= mesh.vertex_count || triangle.c >= mesh.vertex_count)
        continue;
      const auto &pa = this->projected_[triangle.a];
      const auto &pb = this->projected_[triangle.b];
      const auto &pc = this->projected_[triangle.c];

      const int64_t e1x = static_cast<int64_t>(pb.cx) - pa.cx;
      const int64_t e1y = static_cast<int64_t>(pb.cy) - pa.cy;
      const int64_t e1z = static_cast<int64_t>(pb.depth) - pa.depth;
      const int64_t e2x = static_cast<int64_t>(pc.cx) - pa.cx;
      const int64_t e2y = static_cast<int64_t>(pc.cy) - pa.cy;
      const int64_t e2z = static_cast<int64_t>(pc.depth) - pa.depth;
      const int64_t face_nx = e1y * e2z - e1z * e2y;
      const int64_t face_ny = e1z * e2x - e1x * e2z;
      const int64_t face_nz = e1x * e2y - e1y * e2x;
      const int64_t view_dot = face_nx * pa.cx + face_ny * pa.cy + face_nz * pa.depth;
      if (view_dot >= 0)
        continue;

      const bool a_front = pa.depth >= params.near_depth;
      const bool b_front = pb.depth >= params.near_depth;
      const bool c_front = pc.depth >= params.near_depth;
      if (!a_front && !b_front && !c_front)
        continue;

      this->stats_.visible_triangles++;
      if (a_front && b_front && c_front) {
        FixedMeshRasterVertex a = this->make_raster_vertex_(pa, triangle.uv_a);
        FixedMeshRasterVertex b = this->make_raster_vertex_(pb, triangle.uv_b);
        FixedMeshRasterVertex c = this->make_raster_vertex_(pc, triangle.uv_c);
        this->extend_new_box_(a.x, a.y);
        this->extend_new_box_(b.x, b.y);
        this->extend_new_box_(c.x, c.y);
        this->fill_triangle_(a, b, c, material, lit_palette);
        this->stats_.rasterized_triangles++;
        continue;
      }

      this->stats_.clipped_triangles++;
      const FixedMeshClipVertex clip_input[3] = {
          {pa.cx, pa.cy, pa.depth, pa.shade, triangle.uv_a.u, triangle.uv_a.v},
          {pb.cx, pb.cy, pb.depth, pb.shade, triangle.uv_b.u, triangle.uv_b.v},
          {pc.cx, pc.cy, pc.depth, pc.shade, triangle.uv_c.u, triangle.uv_c.v},
      };
      FixedMeshClipVertex clipped[4]{};
      const size_t clipped_count = fixed_mesh_clip_triangle_near(clip_input, clipped, params.near_depth);
      if (clipped_count < 3)
        continue;

      FixedMeshRasterVertex raster[4]{};
      for (size_t i = 0; i < clipped_count; i++)
        raster[i] = this->project_clipped_(clipped[i], params.focal, center_x, center_y);
      for (size_t i = 1; i + 1 < clipped_count; i++) {
        this->extend_new_box_(raster[0].x, raster[0].y);
        this->extend_new_box_(raster[i].x, raster[i].y);
        this->extend_new_box_(raster[i + 1].x, raster[i + 1].y);
        this->fill_triangle_(raster[0], raster[i], raster[i + 1], material, lit_palette);
        this->stats_.rasterized_triangles++;
      }
    }
  }

  FixedMeshStats end_frame() {
    if (!this->active_)
      return this->stats_;

    if (this->have_new_box_) {
      this->new_x0_ -= 2;
      this->new_y0_ -= 2;
      this->new_x1_ += 2;
      this->new_y1_ += 2;
    }

    // Keep dirty/bounds math explicitly int32_t. On ESP32-C6, int32_t is
    // `long int` while the display dimensions are ordinary `int`, so leaving
    // these as mixed types breaks std::min/max template deduction.
    int32_t dirty_x0 = 0;
    int32_t dirty_y0 = 0;
    int32_t dirty_x1 = -1;
    int32_t dirty_y1 = -1;
    const int32_t screen_x1 = static_cast<int32_t>(this->screen_w_ - 1);
    const int32_t screen_y1 = static_cast<int32_t>(this->screen_h_ - 1);
    if (!this->frame_initialized_) {
      dirty_x1 = screen_x1;
      dirty_y1 = screen_y1;
    } else if (this->have_old_box_) {
      int32_t raw_x0 = this->old_x0_;
      int32_t raw_y0 = this->old_y0_;
      int32_t raw_x1 = this->old_x1_;
      int32_t raw_y1 = this->old_y1_;
      if (this->have_new_box_) {
        raw_x0 = std::min<int32_t>(raw_x0, this->new_x0_);
        raw_y0 = std::min<int32_t>(raw_y0, this->new_y0_);
        raw_x1 = std::max<int32_t>(raw_x1, this->new_x1_);
        raw_y1 = std::max<int32_t>(raw_y1, this->new_y1_);
      }
      dirty_x0 = std::max<int32_t>(0, raw_x0);
      dirty_y0 = std::max<int32_t>(0, raw_y0);
      dirty_x1 = std::min<int32_t>(screen_x1, raw_x1);
      dirty_y1 = std::min<int32_t>(screen_y1, raw_y1);
    } else if (this->have_new_box_) {
      dirty_x0 = std::max<int32_t>(0, this->new_x0_);
      dirty_y0 = std::max<int32_t>(0, this->new_y0_);
      dirty_x1 = std::min<int32_t>(screen_x1, this->new_x1_);
      dirty_y1 = std::min<int32_t>(screen_y1, this->new_y1_);
    }

    if (dirty_x0 <= dirty_x1 && dirty_y0 <= dirty_y1) {
      this->stats_.dirty_bytes = static_cast<uint32_t>(dirty_x1 - dirty_x0 + 1) *
                                 static_cast<uint32_t>(dirty_y1 - dirty_y0 + 1) * sizeof(PixelT);
      if (this->frame_initialized_)
        this->dma_high_water_bytes_ = std::max(this->dma_high_water_bytes_, this->stats_.dirty_bytes);
      this->display_->mark_dirty(dirty_x0, dirty_y0, dirty_x1, dirty_y1);
    }
    this->stats_.dma_high_water_bytes = this->dma_high_water_bytes_;

    if (this->have_new_box_) {
      this->old_x0_ = this->new_x0_;
      this->old_y0_ = this->new_y0_;
      this->old_x1_ = this->new_x1_;
      this->old_y1_ = this->new_y1_;
    }
    this->have_old_box_ = this->have_new_box_;
    this->frame_initialized_ = true;
    this->active_ = false;
    return this->stats_;
  }

 private:
  FixedMeshRasterVertex make_raster_vertex_(const FixedMeshProjectedVertex &p, const FixedMeshUV &uv) const {
    return {p.x, p.y, p.inv_w, p.shade,
            static_cast<int32_t>(static_cast<int64_t>(uv.u) * p.inv_w),
            static_cast<int32_t>(static_cast<int64_t>(uv.v) * p.inv_w)};
  }

  FixedMeshRasterVertex project_clipped_(const FixedMeshClipVertex &v, int32_t focal,
                                         int center_x, int center_y) const {
    const int32_t inv_w = static_cast<int32_t>((int64_t{1} << FIXED_MESH_INV_W_BITS) / v.depth);
    return {
        center_x + static_cast<int32_t>((static_cast<int64_t>(v.x) * focal) / v.depth),
        center_y - static_cast<int32_t>((static_cast<int64_t>(v.y) * focal) / v.depth),
        inv_w,
        static_cast<uint8_t>(std::clamp<int32_t>(v.shade, 0, 255)),
        static_cast<int32_t>(static_cast<int64_t>(v.u) * inv_w),
        static_cast<int32_t>(static_cast<int64_t>(v.v) * inv_w),
    };
  }

  void fill_rect_(int x0, int y0, int x1, int y1, PixelT color) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, this->screen_w_ - 1);
    y1 = std::min(y1, this->screen_h_ - 1);
    if (x0 > x1 || y0 > y1)
      return;
    const size_t count = static_cast<size_t>(x1 - x0 + 1);
    for (int y = y0; y <= y1; y++)
      std::fill_n(this->fb_ + static_cast<size_t>(y) * this->stride_ + x0, count, color);
  }

  void extend_new_box_(int32_t x, int32_t y) {
    if (!this->have_new_box_) {
      this->new_x0_ = this->new_x1_ = x;
      this->new_y0_ = this->new_y1_ = y;
      this->have_new_box_ = true;
    } else {
      this->new_x0_ = std::min(this->new_x0_, x);
      this->new_y0_ = std::min(this->new_y0_, y);
      this->new_x1_ = std::max(this->new_x1_, x);
      this->new_y1_ = std::max(this->new_y1_, y);
    }
  }

  void draw_span_(int y, int32_t xa, int32_t wa, int32_t sa, int32_t uowa, int32_t vowa,
                  int32_t xb, int32_t wb, int32_t sb, int32_t uowb, int32_t vowb,
                  const FixedMeshIndexedMaterial &material, const uint16_t *lit_palette) {
    if (y < 0 || y >= this->screen_h_)
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
    if (x1 < 0 || x0 >= this->screen_w_)
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
    if (x1 >= this->screen_w_)
      x1 = this->screen_w_ - 1;
    if (x0 > x1 || inv_w <= 0)
      return;

    PixelT *pixel = this->fb_ + static_cast<size_t>(y) * this->stride_ + x0;
    uint8_t *depth_pixel = this->z_buffer_.data() + static_cast<size_t>(y) * this->screen_w_ + x0;
    const int coord_shift = 16 - material.uv_frac_bits;
    const int32_t coord_scale = int32_t{1} << coord_shift;

    int32_t u_start = u_over_w / inv_w;
    int32_t v_start = v_over_w / inv_w;
    int x = x0;
    while (x <= x1) {
      const int count = std::min(FIXED_MESH_PERSPECTIVE_BLOCK_PIXELS, x1 - x + 1);
      const int32_t w_end = inv_w + w_step * count;
      const int32_t uow_end = u_over_w + uow_step * count;
      const int32_t vow_end = v_over_w + vow_step * count;
      if (w_end <= 0)
        break;

      const int32_t u_end = uow_end / w_end;
      const int32_t v_end = vow_end / w_end;
      int32_t u = static_cast<int32_t>(static_cast<int64_t>(u_start) * coord_scale);
      int32_t v = static_cast<int32_t>(static_cast<int64_t>(v_start) * coord_scale);
      const int32_t u_step = static_cast<int32_t>(
          (static_cast<int64_t>(u_end - u_start) * coord_scale) / count);
      const int32_t v_step = static_cast<int32_t>(
          (static_cast<int64_t>(v_end - v_start) * coord_scale) / count);

      for (int i = 0; i < count; i++, x++, pixel++, depth_pixel++) {
        const uint8_t iz = static_cast<uint8_t>(std::clamp<int32_t>(
            inv_w >> (FIXED_MESH_INV_W_BITS - 16), 0, 255));
        if (iz > *depth_pixel) {
          *depth_pixel = iz;
          const int tex_u = std::clamp<int32_t>(u >> 16, 0, material.texture_width - 1);
          const int tex_v = std::clamp<int32_t>(v >> 16, 0, material.texture_height - 1);
          const uint8_t texel = material.texture[
              static_cast<size_t>(tex_v) * material.texture_width + tex_u];
          const uint8_t shade8 = static_cast<uint8_t>(std::clamp<int32_t>(shade >> 16, 0, 255));
          const uint8_t light = static_cast<uint8_t>(shade8 >> 4);
          *pixel = static_cast<PixelT>(lit_palette[static_cast<size_t>(light) * FIXED_MESH_PALETTE_SIZE + texel]);
        }
        inv_w += w_step;
        shade += s_step;
        u += u_step;
        v += v_step;
      }

      u_over_w = uow_end;
      v_over_w = vow_end;
      inv_w = w_end;
      u_start = u_end;
      v_start = v_end;
      this->stats_.perspective_blocks++;
    }
  }

  void fill_triangle_(const FixedMeshRasterVertex &va, const FixedMeshRasterVertex &vb,
                      const FixedMeshRasterVertex &vc, const FixedMeshIndexedMaterial &material,
                      const uint16_t *lit_palette) {
    struct ScanVertex {
      int32_t x;
      int32_t y;
      int32_t w;
      int32_t s;
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

    const int32_t long_dy = v2.y - v0.y;
    const int32_t long_x_step = static_cast<int32_t>((static_cast<int64_t>(v2.x - v0.x) * 65536) / long_dy);
    const int32_t long_w_step = (v2.w - v0.w) / long_dy;
    const int32_t long_s_step = static_cast<int32_t>((static_cast<int64_t>(v2.s - v0.s) * 65536) / long_dy);
    const int32_t long_uow_step = (v2.uow - v0.uow) / long_dy;
    const int32_t long_vow_step = (v2.vow - v0.vow) / long_dy;
    int32_t long_x = static_cast<int32_t>(static_cast<int64_t>(v0.x) * 65536);
    int32_t long_w = v0.w;
    int32_t long_s = v0.s << 16;
    int32_t long_uow = v0.uow;
    int32_t long_vow = v0.vow;

    if (v1.y > v0.y) {
      const int32_t short_dy = v1.y - v0.y;
      const int32_t short_x_step = static_cast<int32_t>((static_cast<int64_t>(v1.x - v0.x) * 65536) / short_dy);
      const int32_t short_w_step = (v1.w - v0.w) / short_dy;
      const int32_t short_s_step = static_cast<int32_t>((static_cast<int64_t>(v1.s - v0.s) * 65536) / short_dy);
      const int32_t short_uow_step = (v1.uow - v0.uow) / short_dy;
      const int32_t short_vow_step = (v1.vow - v0.vow) / short_dy;
      int32_t short_x = static_cast<int32_t>(static_cast<int64_t>(v0.x) * 65536);
      int32_t short_w = v0.w;
      int32_t short_s = v0.s << 16;
      int32_t short_uow = v0.uow;
      int32_t short_vow = v0.vow;
      const int32_t top_end = (v1.y == v2.y) ? v1.y : v1.y - 1;
      for (int32_t y = v0.y; y <= top_end; y++) {
        this->draw_span_(y, long_x, long_w, long_s, long_uow, long_vow,
                         short_x, short_w, short_s, short_uow, short_vow, material, lit_palette);
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
      const int32_t short_dy = v2.y - v1.y;
      const int32_t short_x_step = static_cast<int32_t>((static_cast<int64_t>(v2.x - v1.x) * 65536) / short_dy);
      const int32_t short_w_step = (v2.w - v1.w) / short_dy;
      const int32_t short_s_step = static_cast<int32_t>((static_cast<int64_t>(v2.s - v1.s) * 65536) / short_dy);
      const int32_t short_uow_step = (v2.uow - v1.uow) / short_dy;
      const int32_t short_vow_step = (v2.vow - v1.vow) / short_dy;
      int32_t short_x = static_cast<int32_t>(static_cast<int64_t>(v1.x) * 65536);
      int32_t short_w = v1.w;
      int32_t short_s = v1.s << 16;
      int32_t short_uow = v1.uow;
      int32_t short_vow = v1.vow;
      for (int32_t y = v1.y; y <= v2.y; y++) {
        this->draw_span_(y, long_x, long_w, long_s, long_uow, long_vow,
                         short_x, short_w, short_s, short_uow, short_vow, material, lit_palette);
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
  }

  DisplayT *display_{nullptr};
  PixelT *fb_{nullptr};
  PixelT black_{};
  int screen_w_{0};
  int screen_h_{0};
  int stride_{0};
  bool active_{false};
  bool frame_initialized_{false};
  bool palette_order_ready_{false};
  bool use_big_endian_palette_{false};
  bool have_old_box_{false};
  bool have_new_box_{false};
  int32_t old_x0_{0};
  int32_t old_y0_{0};
  int32_t old_x1_{0};
  int32_t old_y1_{0};
  int32_t new_x0_{0};
  int32_t new_y0_{0};
  int32_t new_x1_{0};
  int32_t new_y1_{0};
  uint32_t dma_high_water_bytes_{0};
  FixedMeshStats stats_{};
  std::vector<uint8_t> z_buffer_{};
  std::array<FixedMeshProjectedVertex, MAX_VERTICES> projected_{};
};

}  // namespace esphome::mipi_spi::demo3d
