#pragma once

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "fixed_mesh_renderer.h"
#include "suzanne_fixed_mesh.h"
#include "suzanne_perspective.h"

namespace esphome::mipi_spi::demo3d {

static constexpr const char *SUZANNE_PERF_TAG = "fixed_mesh_3d";

static_assert(FIXED_MESH_PERSPECTIVE_BLOCK_PIXELS == PERSPECTIVE_BLOCK_PIXELS,
              "Generic and legacy perspective block settings must stay aligned");
static_assert(SUZANNE_BLENDER_UV_FRAC_BITS == 7,
              "Suzanne fixed material expects the Blender atlas in Q7 texel units");

// 128x128 indexed UV diagnostic texture. This remains entirely in flash.
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

static constexpr FixedMeshIndexedMaterial SUZANNE_BLENDER_FIXED_MATERIAL = {
    SUZANNE_BLENDER_UV_TEXTURE.data(),
    SUZANNE_BLENDER_TEXTURE_SIZE,
    SUZANNE_BLENDER_TEXTURE_SIZE,
    SUZANNE_BLENDER_UV_FRAC_BITS,
    TEXTURED_GOURAUD_LIT_PALETTE_LE.data(),
    TEXTURED_GOURAUD_LIT_PALETTE_BE.data(),
};

static FixedMeshStats SUZANNE_FIXED_MESH_LAST_STATS{};
static inline const FixedMeshStats &get_suzanne_fixed_mesh_stats() {
  return SUZANNE_FIXED_MESH_LAST_STATS;
}

struct SuzannePerfStats {
  uint32_t fps_x10{0};
  uint32_t period_us{0};
  uint32_t render_us{0};
  uint32_t present_us{0};
  uint32_t work_us{0};
  uint32_t render_max_us{0};
  uint32_t present_max_us{0};
  uint32_t work_max_us{0};
  uint32_t visible_triangles{0};
  uint32_t rasterized_triangles{0};
  uint32_t perspective_blocks{0};
};

static SuzannePerfStats SUZANNE_PERF_LAST_STATS{};
static inline const SuzannePerfStats &get_suzanne_perf_stats() {
  return SUZANNE_PERF_LAST_STATS;
}

struct SuzanneFrameTiming {
  uint32_t render_us{0};
  uint32_t present_us{0};
  uint32_t work_us{0};
  uint32_t visible_triangles{0};
  uint32_t rasterized_triangles{0};
  uint32_t perspective_blocks{0};
};

static SuzanneFrameTiming SUZANNE_FRAME_LAST_TIMING{};
static inline const SuzanneFrameTiming &get_suzanne_frame_timing() {
  return SUZANNE_FRAME_LAST_TIMING;
}

struct SuzannePerfAccumulator {
  uint32_t window_start_us{0};
  uint32_t frames{0};
  uint64_t render_sum_us{0};
  uint64_t present_sum_us{0};
  uint64_t work_sum_us{0};
  uint32_t render_max_us{0};
  uint32_t present_max_us{0};
  uint32_t work_max_us{0};
};

#if defined(USE_ESP32_VARIANT_ESP32S3)
// Small 5x7 font used only by the direct-RGB performance HUD. Each byte is one
// vertical column, bit 0 at the top. Keeping this here avoids touching ESPHome's
// normal drawing buffer, which would defeat the direct framebuffer path.
static constexpr uint8_t SUZANNE_FONT_DIGITS[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
};

static constexpr uint8_t SUZANNE_FONT_UPPER[26][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
};

static inline const uint8_t *suzanne_glyph_(char c) {
  if (c >= '0' && c <= '9')
    return SUZANNE_FONT_DIGITS[c - '0'];
  if (c >= 'A' && c <= 'Z')
    return SUZANNE_FONT_UPPER[c - 'A'];
  return nullptr;
}

template<typename PixelT>
static inline void suzanne_hud_fill_rect_(PixelT *fb, int stride, int screen_w, int screen_h,
                                          int x, int y, int w, int h, PixelT color) {
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(screen_w, x + w);
  const int y1 = std::min(screen_h, y + h);
  if (x0 >= x1 || y0 >= y1)
    return;
  for (int py = y0; py < y1; py++)
    std::fill_n(fb + static_cast<size_t>(py) * stride + x0, x1 - x0, color);
}

template<typename PixelT>
static inline void suzanne_hud_char_(PixelT *fb, int stride, int screen_w, int screen_h,
                                     int x, int y, char c, PixelT color, int scale = 2) {
  if (c == '.') {
    suzanne_hud_fill_rect_(fb, stride, screen_w, screen_h, x + 2 * scale, y + 6 * scale,
                           scale, scale, color);
    return;
  }
  const uint8_t *glyph = suzanne_glyph_(c);
  if (glyph == nullptr)
    return;
  for (int col = 0; col < 5; col++) {
    const uint8_t bits = glyph[col];
    for (int row = 0; row < 7; row++) {
      if ((bits & (1u << row)) != 0)
        suzanne_hud_fill_rect_(fb, stride, screen_w, screen_h,
                               x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}

template<typename PixelT>
static inline void suzanne_hud_text_(PixelT *fb, int stride, int screen_w, int screen_h,
                                     int x, int y, const char *text, PixelT color, int scale = 2) {
  const int advance = 6 * scale;
  while (*text != '\0') {
    suzanne_hud_char_(fb, stride, screen_w, screen_h, x, y, *text++, color, scale);
    x += advance;
  }
}

template<typename DisplayT>
static inline void draw_suzanne_perf_hud_(DisplayT *display, const SuzannePerfStats &perf) {
  auto *fb = display->get_framebuffer();
  if (fb == nullptr)
    return;
  const int stride = static_cast<int>(display->get_framebuffer_stride());
  const int screen_w = display->get_width();
  const int screen_h = display->get_height();
  if (stride <= 0 || screen_w <= 0 || screen_h <= 0)
    return;

  const auto bg = display->native_color(Color(0, 0, 0));
  const auto fg = display->native_color(Color(255, 255, 255));
  constexpr int x = 8;
  constexpr int y = 8;
  constexpr int hud_w = 154;
  constexpr int hud_h = 84;
  suzanne_hud_fill_rect_(fb, stride, screen_w, screen_h, x, y, hud_w, hud_h, bg);

  char line[24];
  std::snprintf(line, sizeof(line), "FPS %" PRIu32 ".%" PRIu32, perf.fps_x10 / 10, perf.fps_x10 % 10);
  suzanne_hud_text_(fb, stride, screen_w, screen_h, x + 4, y + 4, line, fg);
  std::snprintf(line, sizeof(line), "REN %" PRIu32 ".%" PRIu32 "MS", perf.render_us / 1000,
                (perf.render_us / 100) % 10);
  suzanne_hud_text_(fb, stride, screen_w, screen_h, x + 4, y + 20, line, fg);
  std::snprintf(line, sizeof(line), "PRS %" PRIu32 ".%" PRIu32 "MS", perf.present_us / 1000,
                (perf.present_us / 100) % 10);
  suzanne_hud_text_(fb, stride, screen_w, screen_h, x + 4, y + 36, line, fg);
  std::snprintf(line, sizeof(line), "TRI %" PRIu32, perf.visible_triangles);
  suzanne_hud_text_(fb, stride, screen_w, screen_h, x + 4, y + 52, line, fg);
  std::snprintf(line, sizeof(line), "BLK %" PRIu32, perf.perspective_blocks);
  suzanne_hud_text_(fb, stride, screen_w, screen_h, x + 4, y + 68, line, fg);
}
#endif  // USE_ESP32_VARIANT_ESP32S3

// Compatibility wrapper for the existing demo/YAML API. All actual geometry,
// clipping, projection, perspective correction, Z buffering and dirty-region
// handling now live in FixedMeshRenderer.
template<typename DisplayT>
PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y,
                                                        int32_t camera_z = 500, bool draw_hud = true,
                                                        bool log_perf = true) {
  static FixedMeshRenderer<DisplayT, GOURAUD_TOTAL_VERTS> renderer;
  static SuzannePerfAccumulator perf{};

  PerspectiveStats stats{};
  const uint32_t frame_start_us = micros();
  if (perf.window_start_us == 0)
    perf.window_start_us = frame_start_us;

  if (!renderer.begin_frame(display))
    return stats;

  FixedMeshDrawParams params{};
  params.phase_x = phase_x;
  params.phase_y = phase_y;
  params.translate_z = camera_z;
  params.focal = 220;
  params.near_depth = 256;
  renderer.draw_mesh(SUZANNE_FIXED_MESH, SUZANNE_BLENDER_FIXED_MATERIAL, params);

  const uint32_t render_end_us = micros();
#if defined(USE_ESP32_VARIANT_ESP32S3)
  if (draw_hud)
    draw_suzanne_perf_hud_(display, SUZANNE_PERF_LAST_STATS);
#endif
  const uint32_t present_start_us = micros();
  const FixedMeshStats mesh_stats = renderer.end_frame();
  const uint32_t frame_end_us = micros();

  SUZANNE_FIXED_MESH_LAST_STATS = mesh_stats;
  stats.visible_triangles = static_cast<uint16_t>(
      std::min<uint32_t>(mesh_stats.visible_triangles, 0xFFFFu));
  stats.rasterized_triangles = static_cast<uint16_t>(
      std::min<uint32_t>(mesh_stats.rasterized_triangles, 0xFFFFu));
  stats.dirty_bytes = mesh_stats.dirty_bytes;
  stats.dma_high_water_bytes = mesh_stats.dma_high_water_bytes;
  stats.perspective_blocks = mesh_stats.perspective_blocks;
  PERSPECTIVE_LAST_STATS = stats;

  const uint32_t render_us = render_end_us - frame_start_us;
  const uint32_t present_us = frame_end_us - present_start_us;
  const uint32_t work_us = frame_end_us - frame_start_us;
  SUZANNE_FRAME_LAST_TIMING.render_us = render_us;
  SUZANNE_FRAME_LAST_TIMING.present_us = present_us;
  SUZANNE_FRAME_LAST_TIMING.work_us = work_us;
  SUZANNE_FRAME_LAST_TIMING.visible_triangles = mesh_stats.visible_triangles;
  SUZANNE_FRAME_LAST_TIMING.rasterized_triangles = mesh_stats.rasterized_triangles;
  SUZANNE_FRAME_LAST_TIMING.perspective_blocks = mesh_stats.perspective_blocks;
  perf.frames++;
  perf.render_sum_us += render_us;
  perf.present_sum_us += present_us;
  perf.work_sum_us += work_us;
  perf.render_max_us = std::max(perf.render_max_us, render_us);
  perf.present_max_us = std::max(perf.present_max_us, present_us);
  perf.work_max_us = std::max(perf.work_max_us, work_us);

  const uint32_t elapsed_us = frame_end_us - perf.window_start_us;
  if (elapsed_us >= 1000000u && perf.frames != 0) {
    SUZANNE_PERF_LAST_STATS.fps_x10 = static_cast<uint32_t>(
        (static_cast<uint64_t>(perf.frames) * 10000000ull + elapsed_us / 2) / elapsed_us);
    SUZANNE_PERF_LAST_STATS.period_us = elapsed_us / perf.frames;
    SUZANNE_PERF_LAST_STATS.render_us = static_cast<uint32_t>(perf.render_sum_us / perf.frames);
    SUZANNE_PERF_LAST_STATS.present_us = static_cast<uint32_t>(perf.present_sum_us / perf.frames);
    SUZANNE_PERF_LAST_STATS.work_us = static_cast<uint32_t>(perf.work_sum_us / perf.frames);
    SUZANNE_PERF_LAST_STATS.render_max_us = perf.render_max_us;
    SUZANNE_PERF_LAST_STATS.present_max_us = perf.present_max_us;
    SUZANNE_PERF_LAST_STATS.work_max_us = perf.work_max_us;
    SUZANNE_PERF_LAST_STATS.visible_triangles = mesh_stats.visible_triangles;
    SUZANNE_PERF_LAST_STATS.rasterized_triangles = mesh_stats.rasterized_triangles;
    SUZANNE_PERF_LAST_STATS.perspective_blocks = mesh_stats.perspective_blocks;

    if (log_perf)
      ESP_LOGI(SUZANNE_PERF_TAG,
             "FPS %" PRIu32 ".%" PRIu32 " | period %" PRIu32 ".%" PRIu32
             " ms | render %" PRIu32 ".%" PRIu32 " ms max %" PRIu32 ".%" PRIu32
             " | present %" PRIu32 ".%" PRIu32 " ms max %" PRIu32 ".%" PRIu32
             " | work %" PRIu32 ".%" PRIu32 " ms max %" PRIu32 ".%" PRIu32
             " | tris %" PRIu32 " vis / %" PRIu32 " rast | persp blocks %" PRIu32,
             SUZANNE_PERF_LAST_STATS.fps_x10 / 10, SUZANNE_PERF_LAST_STATS.fps_x10 % 10,
             SUZANNE_PERF_LAST_STATS.period_us / 1000, (SUZANNE_PERF_LAST_STATS.period_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.render_us / 1000, (SUZANNE_PERF_LAST_STATS.render_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.render_max_us / 1000, (SUZANNE_PERF_LAST_STATS.render_max_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.present_us / 1000, (SUZANNE_PERF_LAST_STATS.present_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.present_max_us / 1000, (SUZANNE_PERF_LAST_STATS.present_max_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.work_us / 1000, (SUZANNE_PERF_LAST_STATS.work_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.work_max_us / 1000, (SUZANNE_PERF_LAST_STATS.work_max_us / 100) % 10,
             SUZANNE_PERF_LAST_STATS.visible_triangles, SUZANNE_PERF_LAST_STATS.rasterized_triangles,
             SUZANNE_PERF_LAST_STATS.perspective_blocks);

    perf.window_start_us = frame_end_us;
    perf.frames = 0;
    perf.render_sum_us = 0;
    perf.present_sum_us = 0;
    perf.work_sum_us = 0;
    perf.render_max_us = 0;
    perf.present_max_us = 0;
    perf.work_max_us = 0;
  }

  return stats;
}


#if defined(USE_ESP32_VARIANT_ESP32S3)
struct SuzannePclkBenchmarkState {
  bool initialized{false};
  bool finished{false};
  size_t step{0};
  uint8_t settle_frames{0};
  uint32_t original_pclk_hz{0};
  uint32_t sample_start_us{0};
  uint32_t frames{0};
  uint64_t render_sum_us{0};
  uint64_t present_sum_us{0};
  uint64_t work_sum_us{0};
  uint32_t render_max_us{0};
  uint32_t present_max_us{0};
  uint32_t work_max_us{0};
};

static inline void reset_suzanne_pclk_sample_(SuzannePclkBenchmarkState &bench) {
  bench.sample_start_us = 0;
  bench.frames = 0;
  bench.render_sum_us = 0;
  bench.present_sum_us = 0;
  bench.work_sum_us = 0;
  bench.render_max_us = 0;
  bench.present_max_us = 0;
  bench.work_max_us = 0;
}
#endif

template<typename DisplayT>
void benchmark_suzanne_pclk(DisplayT *display, int32_t camera_z = 500) {
#if defined(USE_ESP32_VARIANT_ESP32S3)
  static constexpr uint32_t PCLK_HZ[] = {
      12000000u, 14000000u, 16000000u, 18000000u,
      20000000u, 22000000u, 24000000u, 26000000u,
  };
  static constexpr uint8_t BENCH_PHASE_X = 80;
  static constexpr uint8_t BENCH_PHASE_Y = 160;
  static constexpr uint8_t SETTLE_FRAMES = 6;
  static constexpr uint32_t SAMPLE_TIME_US = 2000000u;
  static SuzannePclkBenchmarkState bench{};

  if (!bench.initialized) {
    bench.initialized = true;
    bench.original_pclk_hz = display->get_pclk_frequency();
    bench.step = 0;
    bench.settle_frames = SETTLE_FRAMES;
    reset_suzanne_pclk_sample_(bench);
    ESP_LOGI(SUZANNE_PERF_TAG,
             "PCLK_BENCH start | bounce %u lines | fixed phase %u/%u | original %u MHz",
             static_cast<unsigned>(display->get_bounce_buffer_lines()),
             static_cast<unsigned>(BENCH_PHASE_X), static_cast<unsigned>(BENCH_PHASE_Y),
             static_cast<unsigned>(bench.original_pclk_hz / 1000000u));
    if (!display->set_runtime_pclk_frequency(PCLK_HZ[bench.step])) {
      ESP_LOGE(SUZANNE_PERF_TAG, "PCLK_BENCH unable to set first PCLK");
      bench.finished = true;
    } else {
      ESP_LOGI(SUZANNE_PERF_TAG, "PCLK_BENCH settling at %u MHz",
               static_cast<unsigned>(PCLK_HZ[bench.step] / 1000000u));
    }
  }

  const PerspectiveStats rendered = render_suzanne_blender_uv_perspective(
      display, BENCH_PHASE_X, BENCH_PHASE_Y, camera_z, false, false);
  if (bench.finished)
    return;

  // Do not turn a failed begin/present into hundreds of copies of the previous
  // valid timing sample. This fixed scene always rasterizes many triangles.
  if (rendered.rasterized_triangles == 0 || rendered.perspective_blocks == 0) {
    ESP_LOGE(SUZANNE_PERF_TAG, "PCLK_BENCH render did not complete; sample not counted");
    return;
  }

  const SuzanneFrameTiming frame = get_suzanne_frame_timing();
  if (bench.settle_frames != 0) {
    bench.settle_frames--;
    if (bench.settle_frames == 0)
      bench.sample_start_us = micros();
    return;
  }

  bench.frames++;
  bench.render_sum_us += frame.render_us;
  bench.present_sum_us += frame.present_us;
  bench.work_sum_us += frame.work_us;
  bench.render_max_us = std::max(bench.render_max_us, frame.render_us);
  bench.present_max_us = std::max(bench.present_max_us, frame.present_us);
  bench.work_max_us = std::max(bench.work_max_us, frame.work_us);

  const uint32_t now_us = micros();
  if (bench.sample_start_us == 0)
    bench.sample_start_us = now_us;
  const uint32_t elapsed_us = now_us - bench.sample_start_us;
  if (elapsed_us < SAMPLE_TIME_US || bench.frames == 0)
    return;

  const uint32_t avg_render = static_cast<uint32_t>(bench.render_sum_us / bench.frames);
  const uint32_t avg_present = static_cast<uint32_t>(bench.present_sum_us / bench.frames);
  const uint32_t avg_work = static_cast<uint32_t>(bench.work_sum_us / bench.frames);
  const uint32_t pclk_mhz = PCLK_HZ[bench.step] / 1000000u;
  ESP_LOGI(SUZANNE_PERF_TAG,
           "PCLK_BENCH %u MHz | bounce %u | frames %u | render %u.%u ms max %u.%u | "
           "present %u.%u ms max %u.%u | work %u.%u ms max %u.%u | tris %u | blocks %u",
           static_cast<unsigned>(pclk_mhz), static_cast<unsigned>(display->get_bounce_buffer_lines()),
           static_cast<unsigned>(bench.frames),
           static_cast<unsigned>(avg_render / 1000), static_cast<unsigned>((avg_render / 100) % 10),
           static_cast<unsigned>(bench.render_max_us / 1000), static_cast<unsigned>((bench.render_max_us / 100) % 10),
           static_cast<unsigned>(avg_present / 1000), static_cast<unsigned>((avg_present / 100) % 10),
           static_cast<unsigned>(bench.present_max_us / 1000), static_cast<unsigned>((bench.present_max_us / 100) % 10),
           static_cast<unsigned>(avg_work / 1000), static_cast<unsigned>((avg_work / 100) % 10),
           static_cast<unsigned>(bench.work_max_us / 1000), static_cast<unsigned>((bench.work_max_us / 100) % 10),
           static_cast<unsigned>(frame.visible_triangles), static_cast<unsigned>(frame.perspective_blocks));

  bench.step++;
  reset_suzanne_pclk_sample_(bench);
  if (bench.step >= (sizeof(PCLK_HZ) / sizeof(PCLK_HZ[0]))) {
    display->set_runtime_pclk_frequency(bench.original_pclk_hz);
    bench.finished = true;
    ESP_LOGI(SUZANNE_PERF_TAG, "PCLK_BENCH complete | restored %u MHz",
             static_cast<unsigned>(bench.original_pclk_hz / 1000000u));
    return;
  }

  bench.settle_frames = SETTLE_FRAMES;
  if (!display->set_runtime_pclk_frequency(PCLK_HZ[bench.step])) {
    ESP_LOGE(SUZANNE_PERF_TAG, "PCLK_BENCH failed setting %u MHz",
             static_cast<unsigned>(PCLK_HZ[bench.step] / 1000000u));
    display->set_runtime_pclk_frequency(bench.original_pclk_hz);
    bench.finished = true;
    return;
  }
  ESP_LOGI(SUZANNE_PERF_TAG, "PCLK_BENCH settling at %u MHz",
           static_cast<unsigned>(PCLK_HZ[bench.step] / 1000000u));
#else
  (void) camera_z;
  (void) display;
#endif
}

}  // namespace esphome::mipi_spi::demo3d
