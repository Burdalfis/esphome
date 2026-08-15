from pathlib import Path

# ---- mipi_rgb.h ---------------------------------------------------------
h = Path("esphome/components/mipi_rgb/mipi_rgb.h")
text = h.read_text()

needle = "  int get_width() override;\n  int get_height() override;\n"
insert = """  int get_width() override;\n  int get_height() override;\n\n  // Direct access to the ESP-IDF RGB scanout framebuffer for performance-sensitive\n  // renderers. The direct path currently requires rotation 0 so logical screen\n  // coordinates and the physical framebuffer stride are identical.\n  uint16_t *get_framebuffer();\n  size_t get_framebuffer_stride();\n  uint16_t native_color(const Color &color);\n  void mark_dirty(int x0, int y0, int x1, int y1);\n"""
if needle not in text:
    raise SystemExit("mipi_rgb.h public insertion point not found")
text = text.replace(needle, insert, 1)

needle = "  esp_lcd_panel_handle_t handle_{};\n"
insert = """  esp_lcd_panel_handle_t handle_{};\n  uint16_t *panel_framebuffer_{nullptr};\n"""
if needle not in text:
    raise SystemExit("mipi_rgb.h framebuffer member insertion point not found")
text = text.replace(needle, insert, 1)
h.write_text(text)

# ---- mipi_rgb.cpp -------------------------------------------------------
cpp = Path("esphome/components/mipi_rgb/mipi_rgb.cpp")
text = cpp.read_text()

needle = """  if (err == ESP_OK)\n    err = esp_lcd_panel_init(this->handle_);\n  if (err != ESP_OK) {\n"""
insert = """  if (err == ESP_OK)\n    err = esp_lcd_panel_init(this->handle_);\n  if (err == ESP_OK) {\n    void *framebuffer = nullptr;\n    err = esp_lcd_rgb_panel_get_frame_buffer(this->handle_, 1, &framebuffer);\n    if (err == ESP_OK)\n      this->panel_framebuffer_ = static_cast<uint16_t *>(framebuffer);\n  }\n  if (err != ESP_OK) {\n"""
if needle not in text:
    raise SystemExit("mipi_rgb.cpp framebuffer acquisition insertion point not found")
text = text.replace(needle, insert, 1)

needle = """  ESP_LOGCONFIG(TAG, \"RGB bounce buffer: %u lines (%u pixels per buffer)\",\n                static_cast<unsigned>(MIPI_RGB_BOUNCE_BUFFER_LINES),\n                static_cast<unsigned>(this->width_ * MIPI_RGB_BOUNCE_BUFFER_LINES));\n}\n\nvoid MipiRgb::loop() {\n"""
insert = """  ESP_LOGCONFIG(TAG, \"RGB bounce buffer: %u lines (%u pixels per buffer)\",\n                static_cast<unsigned>(MIPI_RGB_BOUNCE_BUFFER_LINES),\n                static_cast<unsigned>(this->width_ * MIPI_RGB_BOUNCE_BUFFER_LINES));\n  ESP_LOGCONFIG(TAG, \"Direct RGB framebuffer: %s (%u bytes in PSRAM)\",\n                this->panel_framebuffer_ != nullptr ? \"YES\" : \"NO\",\n                static_cast<unsigned>(this->width_ * this->height_ * sizeof(uint16_t)));\n}\n\nuint16_t *MipiRgb::get_framebuffer() {\n  if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES)\n    return nullptr;\n  return this->panel_framebuffer_;\n}\n\nsize_t MipiRgb::get_framebuffer_stride() {\n  if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES)\n    return 0;\n  return this->width_;\n}\n\nuint16_t MipiRgb::native_color(const Color &color) {\n  return convert_big_endian(display::ColorUtil::color_to_565(color));\n}\n\nvoid MipiRgb::mark_dirty(int x0, int y0, int x1, int y1) {\n  // The renderer writes the continuously scanned ESP-IDF framebuffer directly,\n  // so unlike the SPI display path there is no dirty rectangle to flush.\n  (void) x0;\n  (void) y0;\n  (void) x1;\n  (void) y1;\n}\n\nvoid MipiRgb::loop() {\n"""
if needle not in text:
    raise SystemExit("mipi_rgb.cpp direct API insertion point not found")
text = text.replace(needle, insert, 1)
cpp.write_text(text)

# ---- fixed_mesh_renderer.h ---------------------------------------------
r = Path("esphome/components/mipi_spi/fixed_mesh_renderer.h")
text = r.read_text()

needle = "#include <cstring>\n#include <type_traits>\n"
insert = """#include <cstring>\n#include <cstdlib>\n#include <type_traits>\n\n#if defined(USE_ESP32_VARIANT_ESP32S3)\n#include <esp_heap_caps.h>\n#endif\n"""
if needle not in text:
    raise SystemExit("fixed_mesh_renderer include insertion point not found")
text = text.replace(needle, insert, 1)

needle = """template<typename DisplayT, size_t MAX_VERTICES> class FixedMeshRenderer {\n public:\n  using PixelT = std::remove_pointer_t<decltype(std::declval<DisplayT *>()->get_framebuffer())>;\n  static_assert(sizeof(PixelT) == 2, \"FixedMeshRenderer requires a 16-bit framebuffer\");\n\n  bool begin_frame(DisplayT *display) {\n"""
insert = """template<typename DisplayT, size_t MAX_VERTICES> class FixedMeshRenderer {\n public:\n  using PixelT = std::remove_pointer_t<decltype(std::declval<DisplayT *>()->get_framebuffer())>;\n  static_assert(sizeof(PixelT) == 2, \"FixedMeshRenderer requires a 16-bit framebuffer\");\n\n  FixedMeshRenderer() = default;\n  ~FixedMeshRenderer() { this->release_z_buffer_(); }\n  FixedMeshRenderer(const FixedMeshRenderer &) = delete;\n  FixedMeshRenderer &operator=(const FixedMeshRenderer &) = delete;\n\n  bool begin_frame(DisplayT *display) {\n"""
if needle not in text:
    raise SystemExit("fixed_mesh_renderer class insertion point not found")
text = text.replace(needle, insert, 1)

needle = """    const size_t z_size = static_cast<size_t>(this->screen_w_) * this->screen_h_;\n    if (this->z_buffer_.size() != z_size)\n      this->z_buffer_.resize(z_size);\n    std::memset(this->z_buffer_.data(), 0, z_size);\n"""
insert = """    const size_t z_size = static_cast<size_t>(this->screen_w_) * this->screen_h_;\n    if (!this->ensure_z_buffer_(z_size))\n      return false;\n    std::memset(this->z_buffer_, 0, z_size);\n"""
if needle not in text:
    raise SystemExit("fixed_mesh_renderer z allocation block not found")
text = text.replace(needle, insert, 1)

text = text.replace(
    "uint8_t *depth_pixel = this->z_buffer_.data() + static_cast<size_t>(y) * this->screen_w_ + x0;",
    "uint8_t *depth_pixel = this->z_buffer_ + static_cast<size_t>(y) * this->screen_w_ + x0;",
    1,
)
if "z_buffer_.data()" in text:
    raise SystemExit("unexpected remaining vector z_buffer_.data() use")

needle = """  DisplayT *display_{nullptr};\n  PixelT *fb_{nullptr};\n"""
insert = """  bool ensure_z_buffer_(size_t size) {\n    if (this->z_buffer_ != nullptr && this->z_buffer_size_ == size)\n      return true;\n    this->release_z_buffer_();\n#if defined(USE_ESP32_VARIANT_ESP32S3)\n    this->z_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));\n#else\n    this->z_buffer_ = static_cast<uint8_t *>(std::malloc(size));\n#endif\n    if (this->z_buffer_ == nullptr)\n      return false;\n    this->z_buffer_size_ = size;\n    return true;\n  }\n\n  void release_z_buffer_() {\n    if (this->z_buffer_ == nullptr)\n      return;\n#if defined(USE_ESP32_VARIANT_ESP32S3)\n    heap_caps_free(this->z_buffer_);\n#else\n    std::free(this->z_buffer_);\n#endif\n    this->z_buffer_ = nullptr;\n    this->z_buffer_size_ = 0;\n  }\n\n  DisplayT *display_{nullptr};\n  PixelT *fb_{nullptr};\n"""
if needle not in text:
    raise SystemExit("fixed_mesh_renderer helper insertion point not found")
text = text.replace(needle, insert, 1)

needle = """  FixedMeshStats stats_{};\n  std::vector<uint8_t> z_buffer_{};\n  std::array<FixedMeshProjectedVertex, MAX_VERTICES> projected_{};\n"""
insert = """  FixedMeshStats stats_{};\n  uint8_t *z_buffer_{nullptr};\n  size_t z_buffer_size_{0};\n  std::array<FixedMeshProjectedVertex, MAX_VERTICES> projected_{};\n"""
if needle not in text:
    raise SystemExit("fixed_mesh_renderer member replacement point not found")
text = text.replace(needle, insert, 1)
r.write_text(text)
