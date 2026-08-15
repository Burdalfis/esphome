from pathlib import Path

root = Path('.')

# ---- mipi_rgb.h ---------------------------------------------------------
p = root / 'esphome/components/mipi_rgb/mipi_rgb.h'
s = p.read_text()
s = s.replace('#include "esp_lcd_panel_ops.h"\n', '#include "esp_lcd_panel_ops.h"\n#include "esp_lcd_panel_rgb.h"\n#include "freertos/FreeRTOS.h"\n#include "freertos/semphr.h"\n')
s = s.replace('  void mark_dirty(int x0, int y0, int x1, int y1);\n', '  void mark_dirty(int x0, int y0, int x1, int y1);\n')
s = s.replace('  void common_setup_();\n', '  void common_setup_();\n  static bool IRAM_ATTR frame_done_callback_(esp_lcd_panel_handle_t panel,\n                                              const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx);\n')
s = s.replace('  esp_lcd_panel_handle_t handle_{};\n  uint16_t *panel_framebuffer_{nullptr};\n', '''  esp_lcd_panel_handle_t handle_{};\n  uint16_t *panel_framebuffers_[2]{nullptr, nullptr};\n  uint8_t render_framebuffer_index_{1};\n  SemaphoreHandle_t frame_done_sem_{nullptr};\n  bool direct_present_failed_{false};\n''')
p.write_text(s)

# ---- mipi_rgb.cpp -------------------------------------------------------
p = root / 'esphome/components/mipi_rgb/mipi_rgb.cpp'
s = p.read_text()
s = s.replace('#include <esp_lcd_panel_rgb.h>\n', '#include <esp_lcd_panel_rgb.h>\n#include <esp_idf_version.h>\n')
s = s.replace('static constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 20;\n', 'static constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 20;\nstatic constexpr size_t MIPI_RGB_FRAMEBUFFER_COUNT = 2;\n')
s = s.replace('static constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 10;\n', 'static constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 10;\nstatic constexpr size_t MIPI_RGB_FRAMEBUFFER_COUNT = 1;\n')
s = s.replace('  config.num_fbs = 1;\n', '  config.num_fbs = MIPI_RGB_FRAMEBUFFER_COUNT;\n')
old = '''  if (err == ESP_OK) {\n    void *framebuffer = nullptr;\n    err = esp_lcd_rgb_panel_get_frame_buffer(this->handle_, 1, &framebuffer);\n    if (err == ESP_OK)\n      this->panel_framebuffer_ = static_cast<uint16_t *>(framebuffer);\n  }\n'''
new = '''  if (err == ESP_OK) {\n#if defined(USE_ESP32_VARIANT_ESP32S3)\n    void *framebuffer0 = nullptr;\n    void *framebuffer1 = nullptr;\n    err = esp_lcd_rgb_panel_get_frame_buffer(this->handle_, 2, &framebuffer0, &framebuffer1);\n    if (err == ESP_OK) {\n      this->panel_framebuffers_[0] = static_cast<uint16_t *>(framebuffer0);\n      this->panel_framebuffers_[1] = static_cast<uint16_t *>(framebuffer1);\n      // The RGB driver starts scanout from framebuffer 0. Render the first\n      // software frame into framebuffer 1 so scanout and rasterization never\n      // touch the same PSRAM image.\n      this->render_framebuffer_index_ = 1;\n      this->frame_done_sem_ = xSemaphoreCreateBinary();\n      if (this->frame_done_sem_ == nullptr) {\n        err = ESP_ERR_NO_MEM;\n      } else {\n        esp_lcd_rgb_panel_event_callbacks_t callbacks{};\n#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)\n        callbacks.on_frame_buf_complete = &MipiRgb::frame_done_callback_;\n#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 2)\n        callbacks.on_bounce_frame_finish = &MipiRgb::frame_done_callback_;\n#else\n        callbacks.on_vsync = &MipiRgb::frame_done_callback_;\n#endif\n        err = esp_lcd_rgb_panel_register_event_callbacks(this->handle_, &callbacks, this);\n      }\n    }\n#else\n    void *framebuffer0 = nullptr;\n    err = esp_lcd_rgb_panel_get_frame_buffer(this->handle_, 1, &framebuffer0);\n    if (err == ESP_OK)\n      this->panel_framebuffers_[0] = static_cast<uint16_t *>(framebuffer0);\n#endif\n  }\n'''
if old not in s:
    raise SystemExit('mipi_rgb.cpp framebuffer setup block not found')
s = s.replace(old, new)
s = s.replace('''  ESP_LOGCONFIG(TAG, "Direct RGB framebuffer: %s (%u bytes in PSRAM)",\n                this->panel_framebuffer_ != nullptr ? "YES" : "NO",\n                static_cast<unsigned>(this->width_ * this->height_ * sizeof(uint16_t)));\n''', '''  ESP_LOGCONFIG(TAG, "Direct RGB framebuffers: %u (%u bytes each in PSRAM)",\n                static_cast<unsigned>(MIPI_RGB_FRAMEBUFFER_COUNT),\n                static_cast<unsigned>(this->width_ * this->height_ * sizeof(uint16_t)));\n''')
s = s.replace('''uint16_t *MipiRgb::get_framebuffer() {\n  if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES)\n    return nullptr;\n  return this->panel_framebuffer_;\n}\n''', '''uint16_t *MipiRgb::get_framebuffer() {\n  if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES || this->direct_present_failed_)\n    return nullptr;\n  return this->panel_framebuffers_[this->render_framebuffer_index_];\n}\n''')
old = '''void MipiRgb::mark_dirty(int x0, int y0, int x1, int y1) {\n  // The renderer writes the continuously scanned ESP-IDF framebuffer directly,\n  // so unlike the SPI display path there is no dirty rectangle to flush.\n  (void) x0;\n  (void) y0;\n  (void) x1;\n  (void) y1;\n}\n'''
new = '''bool IRAM_ATTR MipiRgb::frame_done_callback_(esp_lcd_panel_handle_t panel,\n                                                   const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {\n  (void) panel;\n  (void) edata;\n  auto *self = static_cast<MipiRgb *>(user_ctx);\n  if (self == nullptr || self->frame_done_sem_ == nullptr)\n    return false;\n  BaseType_t need_yield = pdFALSE;\n  xSemaphoreGiveFromISR(self->frame_done_sem_, &need_yield);\n  return need_yield == pdTRUE;\n}\n\nvoid MipiRgb::mark_dirty(int x0, int y0, int x1, int y1) {\n  (void) x0;\n  (void) y0;\n  (void) x1;\n  (void) y1;\n#if defined(USE_ESP32_VARIANT_ESP32S3)\n  if (this->handle_ == nullptr || this->frame_done_sem_ == nullptr || this->direct_present_failed_)\n    return;\n  uint16_t *completed = this->panel_framebuffers_[this->render_framebuffer_index_];\n  if (completed == nullptr)\n    return;\n\n  // Discard an old frame-finish token, present the complete back buffer, then\n  // wait until the RGB driver reports a frame boundary before reusing the old\n  // scanout buffer. Espressif's own LVGL RGB port uses the same handshake.\n  while (xSemaphoreTake(this->frame_done_sem_, 0) == pdTRUE) {\n  }\n  const esp_err_t err = esp_lcd_panel_draw_bitmap(this->handle_, 0, 0, this->width_, this->height_, completed);\n  if (err != ESP_OK) {\n    ESP_LOGE(TAG, "Direct RGB framebuffer present failed: %s", esp_err_to_name(err));\n    this->direct_present_failed_ = true;\n    return;\n  }\n  if (xSemaphoreTake(this->frame_done_sem_, pdMS_TO_TICKS(250)) != pdTRUE) {\n    ESP_LOGE(TAG, "Timed out waiting for RGB frame boundary after framebuffer present");\n    this->direct_present_failed_ = true;\n    return;\n  }\n  this->render_framebuffer_index_ ^= 1u;\n#endif\n}\n'''
if old not in s:
    raise SystemExit('mipi_rgb.cpp mark_dirty block not found')
s = s.replace(old, new)
p.write_text(s)

# ---- fixed mesh renderer: per-framebuffer history ----------------------
def patch_renderer(path):
    p = root / path
    s = p.read_text()
    old = '''    if (new_w != this->screen_w_ || new_h != this->screen_h_ || new_stride != this->stride_) {\n      this->screen_w_ = new_w;\n      this->screen_h_ = new_h;\n      this->stride_ = new_stride;\n      this->frame_initialized_ = false;\n      this->have_old_box_ = false;\n      this->palette_order_ready_ = false;\n    }\n\n    const size_t z_size = static_cast<size_t>(this->screen_w_) * this->screen_h_;\n'''
    new = '''    if (new_w != this->screen_w_ || new_h != this->screen_h_ || new_stride != this->stride_) {\n      this->screen_w_ = new_w;\n      this->screen_h_ = new_h;\n      this->stride_ = new_stride;\n      this->reset_buffer_states_();\n      this->palette_order_ready_ = false;\n    }\n    if (!this->select_buffer_state_(this->fb_))\n      return false;\n\n    const size_t z_size = static_cast<size_t>(this->screen_w_) * this->screen_h_;\n'''
    if old not in s:
        raise SystemExit(f'{path}: begin state block not found')
    s = s.replace(old, new)
    old = '''    this->have_old_box_ = this->have_new_box_;\n    this->frame_initialized_ = true;\n    this->active_ = false;\n    return this->stats_;\n  }\n'''
    new = '''    this->have_old_box_ = this->have_new_box_;\n    this->frame_initialized_ = true;\n    this->save_buffer_state_();\n    this->active_ = false;\n    return this->stats_;\n  }\n'''
    if old not in s:
        raise SystemExit(f'{path}: end save block not found')
    s = s.replace(old, new)
    marker = '''  bool ensure_z_buffer_(size_t size) {\n'''
    helpers = '''  struct BufferState {\n    PixelT *framebuffer{nullptr};\n    bool frame_initialized{false};\n    bool have_old_box{false};\n    int32_t old_x0{0};\n    int32_t old_y0{0};\n    int32_t old_x1{0};\n    int32_t old_y1{0};\n  };\n\n  void reset_buffer_states_() {\n    for (auto &state : this->buffer_states_)\n      state = {};\n    this->buffer_state_ = nullptr;\n    this->frame_initialized_ = false;\n    this->have_old_box_ = false;\n  }\n\n  bool select_buffer_state_(PixelT *framebuffer) {\n    BufferState *empty = nullptr;\n    for (auto &state : this->buffer_states_) {\n      if (state.framebuffer == framebuffer) {\n        this->buffer_state_ = &state;\n        break;\n      }\n      if (state.framebuffer == nullptr && empty == nullptr)\n        empty = &state;\n    }\n    if (this->buffer_state_ == nullptr) {\n      if (empty == nullptr)\n        return false;\n      empty->framebuffer = framebuffer;\n      this->buffer_state_ = empty;\n    }\n    this->frame_initialized_ = this->buffer_state_->frame_initialized;\n    this->have_old_box_ = this->buffer_state_->have_old_box;\n    this->old_x0_ = this->buffer_state_->old_x0;\n    this->old_y0_ = this->buffer_state_->old_y0;\n    this->old_x1_ = this->buffer_state_->old_x1;\n    this->old_y1_ = this->buffer_state_->old_y1;\n    return true;\n  }\n\n  void save_buffer_state_() {\n    if (this->buffer_state_ == nullptr)\n      return;\n    this->buffer_state_->frame_initialized = this->frame_initialized_;\n    this->buffer_state_->have_old_box = this->have_old_box_;\n    this->buffer_state_->old_x0 = this->old_x0_;\n    this->buffer_state_->old_y0 = this->old_y0_;\n    this->buffer_state_->old_x1 = this->old_x1_;\n    this->buffer_state_->old_y1 = this->old_y1_;\n  }\n\n'''
    if marker not in s:
        raise SystemExit(f'{path}: zbuffer marker not found')
    s = s.replace(marker, helpers + marker)
    old = '''  uint8_t *z_buffer_{nullptr};\n  size_t z_buffer_size_{0};\n  std::array<FixedMeshProjectedVertex, MAX_VERTICES> projected_{};\n'''
    new = '''  uint8_t *z_buffer_{nullptr};\n  size_t z_buffer_size_{0};\n  std::array<BufferState, 3> buffer_states_{};\n  BufferState *buffer_state_{nullptr};\n  std::array<FixedMeshProjectedVertex, MAX_VERTICES> projected_{};\n'''
    if old not in s:
        raise SystemExit(f'{path}: field marker not found')
    s = s.replace(old, new)
    p.write_text(s)

patch_renderer('esphome/components/fixed_mesh_3d/fixed_mesh_renderer.h')
patch_renderer('esphome/components/mipi_spi/fixed_mesh_renderer.h')
