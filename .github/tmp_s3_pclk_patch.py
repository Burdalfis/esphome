from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


# mipi_rgb.h: expose runtime PCLK and configurable bounce-buffer depth.
path = "esphome/components/mipi_rgb/mipi_rgb.h"
replace_once(
    path,
    "  void set_pclk_frequency(uint32_t pclk_frequency) { this->pclk_frequency_ = pclk_frequency; }\n",
    "  void set_pclk_frequency(uint32_t pclk_frequency) { this->pclk_frequency_ = pclk_frequency; }\n"
    "  bool set_runtime_pclk_frequency(uint32_t pclk_frequency);\n"
    "  uint32_t get_pclk_frequency() const { return this->pclk_frequency_; }\n"
    "  void set_bounce_buffer_lines(size_t lines) { this->bounce_buffer_lines_ = lines; }\n"
    "  size_t get_bounce_buffer_lines() const { return this->bounce_buffer_lines_; }\n",
)
replace_once(
    path,
    "  uint32_t pclk_frequency_ = 16 * 1000 * 1000;\n",
    "  uint32_t pclk_frequency_ = 16 * 1000 * 1000;\n"
    "#if defined(USE_ESP32_VARIANT_ESP32S3)\n"
    "  size_t bounce_buffer_lines_{20};\n"
    "#else\n"
    "  size_t bounce_buffer_lines_{10};\n"
    "#endif\n",
)

# display.py: YAML knob. 0 means direct PSRAM -> RGB EDMA.
path = "esphome/components/mipi_rgb/display.py"
replace_once(
    path,
    'RgbDriverChip("CUSTOM")\n',
    'CONF_BOUNCE_BUFFER_LINES = "bounce_buffer_lines"\n\nRgbDriverChip("CUSTOM")\n',
)
replace_once(
    path,
    "            model.option(CONF_PCLK_INVERTED, True): cv.boolean,\n",
    "            model.option(CONF_PCLK_INVERTED, True): cv.boolean,\n"
    "            cv.Optional(CONF_BOUNCE_BUFFER_LINES): cv.int_range(min=0, max=120),\n",
)
replace_once(
    path,
    "    cg.add(var.set_pclk_frequency(config[CONF_PCLK_FREQUENCY]))\n",
    "    cg.add(var.set_pclk_frequency(config[CONF_PCLK_FREQUENCY]))\n"
    "    if CONF_BOUNCE_BUFFER_LINES in config:\n"
    "        cg.add(var.set_bounce_buffer_lines(config[CONF_BOUNCE_BUFFER_LINES]))\n",
)

# mipi_rgb.cpp: use configured depth, retain 3 FBs, expose runtime PCLK.
path = "esphome/components/mipi_rgb/mipi_rgb.cpp"
replace_once(
    path,
    "// ESP32-S3 RGB panels stream their framebuffer from PSRAM through internal-RAM\n"
    "// bounce buffers. Give the S3 twenty scanlines of headroom so short cache/PSRAM\n"
    "// stalls are less likely to starve the LCD DMA. Three PSRAM framebuffers let\n"
    "// rendering overlap scanout without forcing the producer to wait at every VSYNC.\n"
    "#if defined(USE_ESP32_VARIANT_ESP32S3)\n"
    "static constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 20;\n"
    "static constexpr size_t MIPI_RGB_FRAMEBUFFER_COUNT = 3;\n"
    "#else\n"
    "static constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 10;\n"
    "static constexpr size_t MIPI_RGB_FRAMEBUFFER_COUNT = 1;\n"
    "#endif\n",
    "// Three S3 PSRAM framebuffers let rendering overlap scanout. Bounce-buffer depth\n"
    "// is configurable per display: 0 selects direct PSRAM -> RGB EDMA, while a\n"
    "// non-zero line count uses the two internal-DRAM bounce buffers.\n"
    "#if defined(USE_ESP32_VARIANT_ESP32S3)\n"
    "static constexpr size_t MIPI_RGB_FRAMEBUFFER_COUNT = 3;\n"
    "#else\n"
    "static constexpr size_t MIPI_RGB_FRAMEBUFFER_COUNT = 1;\n"
    "#endif\n",
)
replace_once(
    path,
    "  config.bounce_buffer_size_px = this->width_ * MIPI_RGB_BOUNCE_BUFFER_LINES;\n",
    "  config.bounce_buffer_size_px = this->width_ * this->bounce_buffer_lines_;\n",
)
replace_once(
    path,
    '  ESP_LOGCONFIG(TAG, "RGB bounce buffer: %u lines (%u pixels per buffer)",\n'
    "                static_cast<unsigned>(MIPI_RGB_BOUNCE_BUFFER_LINES),\n"
    "                static_cast<unsigned>(this->width_ * MIPI_RGB_BOUNCE_BUFFER_LINES));\n",
    '  ESP_LOGCONFIG(TAG, "RGB scanout: %s",\n'
    '                this->bounce_buffer_lines_ == 0 ? "direct PSRAM EDMA" : "DRAM bounce buffers");\n'
    '  ESP_LOGCONFIG(TAG, "RGB bounce buffer: %u lines (%u pixels per buffer)",\n'
    "                static_cast<unsigned>(this->bounce_buffer_lines_),\n"
    "                static_cast<unsigned>(this->width_ * this->bounce_buffer_lines_));\n",
)
replace_once(
    path,
    "uint16_t MipiRgb::native_color(const Color &color) {\n"
    "  return convert_big_endian(display::ColorUtil::color_to_565(color));\n"
    "}\n",
    "uint16_t MipiRgb::native_color(const Color &color) {\n"
    "  return convert_big_endian(display::ColorUtil::color_to_565(color));\n"
    "}\n\n"
    "bool MipiRgb::set_runtime_pclk_frequency(uint32_t pclk_frequency) {\n"
    "  if (this->handle_ == nullptr)\n"
    "    return false;\n"
    "  const esp_err_t err = esp_lcd_rgb_panel_set_pclk(this->handle_, pclk_frequency);\n"
    "  if (err != ESP_OK) {\n"
    '    ESP_LOGE(TAG, "Failed to set runtime PCLK to %u Hz: %s",\n'
    "             static_cast<unsigned>(pclk_frequency), esp_err_to_name(err));\n"
    "    return false;\n"
    "  }\n"
    "  this->pclk_frequency_ = pclk_frequency;\n"
    "  return true;\n"
    "}\n",
)

# Suzanne benchmark: fixed geometry, no HUD, 2 s samples at each PCLK.
path = "esphome/components/fixed_mesh_3d/suzanne_blender_uv_perspective.h"
replace_once(
    path,
    "static SuzannePerfStats SUZANNE_PERF_LAST_STATS{};\n"
    "static inline const SuzannePerfStats &get_suzanne_perf_stats() {\n"
    "  return SUZANNE_PERF_LAST_STATS;\n"
    "}\n",
    "static SuzannePerfStats SUZANNE_PERF_LAST_STATS{};\n"
    "static inline const SuzannePerfStats &get_suzanne_perf_stats() {\n"
    "  return SUZANNE_PERF_LAST_STATS;\n"
    "}\n\n"
    "struct SuzanneFrameTiming {\n"
    "  uint32_t render_us{0};\n"
    "  uint32_t present_us{0};\n"
    "  uint32_t work_us{0};\n"
    "  uint32_t visible_triangles{0};\n"
    "  uint32_t rasterized_triangles{0};\n"
    "  uint32_t perspective_blocks{0};\n"
    "};\n\n"
    "static SuzanneFrameTiming SUZANNE_FRAME_LAST_TIMING{};\n"
    "static inline const SuzanneFrameTiming &get_suzanne_frame_timing() {\n"
    "  return SUZANNE_FRAME_LAST_TIMING;\n"
    "}\n",
)
replace_once(
    path,
    "PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y,\n"
    "                                                        int32_t camera_z = 500) {\n",
    "PerspectiveStats render_suzanne_blender_uv_perspective(DisplayT *display, uint8_t phase_x, uint8_t phase_y,\n"
    "                                                        int32_t camera_z = 500, bool draw_hud = true,\n"
    "                                                        bool log_perf = true) {\n",
)
replace_once(
    path,
    "#if defined(USE_ESP32_VARIANT_ESP32S3)\n"
    "  draw_suzanne_perf_hud_(display, SUZANNE_PERF_LAST_STATS);\n"
    "#endif\n",
    "#if defined(USE_ESP32_VARIANT_ESP32S3)\n"
    "  if (draw_hud)\n"
    "    draw_suzanne_perf_hud_(display, SUZANNE_PERF_LAST_STATS);\n"
    "#endif\n",
)
replace_once(
    path,
    "  const uint32_t work_us = frame_end_us - frame_start_us;\n"
    "  perf.frames++;\n",
    "  const uint32_t work_us = frame_end_us - frame_start_us;\n"
    "  SUZANNE_FRAME_LAST_TIMING.render_us = render_us;\n"
    "  SUZANNE_FRAME_LAST_TIMING.present_us = present_us;\n"
    "  SUZANNE_FRAME_LAST_TIMING.work_us = work_us;\n"
    "  SUZANNE_FRAME_LAST_TIMING.visible_triangles = mesh_stats.visible_triangles;\n"
    "  SUZANNE_FRAME_LAST_TIMING.rasterized_triangles = mesh_stats.rasterized_triangles;\n"
    "  SUZANNE_FRAME_LAST_TIMING.perspective_blocks = mesh_stats.perspective_blocks;\n"
    "  perf.frames++;\n",
)
replace_once(
    path,
    "    ESP_LOGI(SUZANNE_PERF_TAG,\n",
    "    if (log_perf)\n"
    "      ESP_LOGI(SUZANNE_PERF_TAG,\n",
)

p = Path(path)
text = p.read_text()
marker = "\n}  // namespace esphome::mipi_spi::demo3d\n"
if text.count(marker) != 1:
    raise SystemExit("Suzanne namespace marker mismatch")
benchmark = r'''

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

  render_suzanne_blender_uv_perspective(display, BENCH_PHASE_X, BENCH_PHASE_Y,
                                         camera_z, false, false);
  if (bench.finished)
    return;

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
'''
p.write_text(text.replace(marker, benchmark + marker, 1))
