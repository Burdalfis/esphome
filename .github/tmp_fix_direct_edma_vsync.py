from pathlib import Path

h = Path('esphome/components/mipi_rgb/mipi_rgb.h')
s = h.read_text()
s = s.replace(
'''  static bool IRAM_ATTR frame_done_callback_(esp_lcd_panel_handle_t panel,\n                                              const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx);\n''',
'''  static bool IRAM_ATTR frame_done_callback_(esp_lcd_panel_handle_t panel,\n                                              const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx);\n  static bool IRAM_ATTR vsync_callback_(esp_lcd_panel_handle_t panel,\n                                        const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx);\n''')
s = s.replace(
'''  volatile uint32_t frame_done_count_{0};\n  uint32_t pending_frame_done_count_{0};\n''',
'''  volatile uint32_t frame_done_count_{0};\n  volatile uint32_t vsync_count_{0};\n  uint32_t pending_frame_done_count_{0};\n  uint32_t pending_vsync_count_{0};\n''')
h.write_text(s)

p = Path('esphome/components/mipi_rgb/mipi_rgb.cpp')
s = p.read_text()
s = s.replace(
'''      this->frame_done_count_ = 0;\n      this->pending_frame_done_count_ = 0;\n''',
'''      this->frame_done_count_ = 0;\n      this->vsync_count_ = 0;\n      this->pending_frame_done_count_ = 0;\n      this->pending_vsync_count_ = 0;\n''')
s = s.replace(
'''        esp_lcd_rgb_panel_event_callbacks_t callbacks{};\n#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)\n        callbacks.on_frame_buf_complete = &MipiRgb::frame_done_callback_;\n''',
'''        esp_lcd_rgb_panel_event_callbacks_t callbacks{};\n        callbacks.on_vsync = &MipiRgb::vsync_callback_;\n#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)\n        callbacks.on_frame_buf_complete = &MipiRgb::frame_done_callback_;\n''')
needle = '''bool IRAM_ATTR MipiRgb::frame_done_callback_(esp_lcd_panel_handle_t panel,\n                                              const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {\n  (void) panel;\n  (void) edata;\n  auto *self = static_cast<MipiRgb *>(user_ctx);\n  if (self == nullptr || self->frame_done_sem_ == nullptr)\n    return false;\n  self->frame_done_count_++;\n  BaseType_t need_yield = pdFALSE;\n  xSemaphoreGiveFromISR(self->frame_done_sem_, &need_yield);\n  return need_yield == pdTRUE;\n}\n'''
replacement = needle + '''\nbool IRAM_ATTR MipiRgb::vsync_callback_(esp_lcd_panel_handle_t panel,\n                                        const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {\n  (void) panel;\n  (void) edata;\n  auto *self = static_cast<MipiRgb *>(user_ctx);\n  if (self == nullptr || self->frame_done_sem_ == nullptr)\n    return false;\n  self->vsync_count_++;\n  BaseType_t need_yield = pdFALSE;\n  xSemaphoreGiveFromISR(self->frame_done_sem_, &need_yield);\n  return need_yield == pdTRUE;\n}\n'''
if needle not in s:
    raise SystemExit('frame_done_callback needle not found')
s = s.replace(needle, replacement)
old = '''  if (this->pending_framebuffer_index_ != MIPI_RGB_NO_FRAMEBUFFER) {\n    // Bounce-buffer mode needs the conservative second boundary because a submit\n    // can race a bounce refill that already captured the previous framebuffer.\n    // Direct EDMA's frame-complete event already means the old framebuffer is\n    // safe to reuse, so waiting for a second event can deadlock the handoff.\n    const uint32_t required_completions = this->bounce_buffer_lines_ == 0 ? 1u : 2u;\n    while (static_cast<uint32_t>(this->frame_done_count_ - this->pending_frame_done_count_) < required_completions) {\n      if (xSemaphoreTake(this->frame_done_sem_, pdMS_TO_TICKS(250)) != pdTRUE) {\n        ESP_LOGE(TAG, "Timed out waiting for RGB framebuffer handoff");\n        this->direct_present_failed_ = true;\n        return;\n      }\n    }\n'''
new = '''  if (this->pending_framebuffer_index_ != MIPI_RGB_NO_FRAMEBUFFER) {\n    // Bounce-buffer mode uses the frame-complete event and keeps the conservative\n    // two-boundary guard against an ISR that already captured the old source.\n    //\n    // On ESP32-S3 direct EDMA, IDF notes that frame-complete can be unreliable\n    // after GDMA prefetch. Accept it immediately when it does arrive (the API\n    // defines that event as safe reuse), otherwise fall back to two VSYNCs. A\n    // prefetched old link can therefore be displayed once more without letting\n    // the renderer overwrite it while GDMA may still be reading it.\n    while (true) {\n      const uint32_t frame_done_delta =\n          static_cast<uint32_t>(this->frame_done_count_ - this->pending_frame_done_count_);\n      const uint32_t vsync_delta = static_cast<uint32_t>(this->vsync_count_ - this->pending_vsync_count_);\n      const bool handoff_complete = this->bounce_buffer_lines_ == 0\n                                        ? (frame_done_delta >= 1u || vsync_delta >= 2u)\n                                        : (frame_done_delta >= 2u);\n      if (handoff_complete)\n        break;\n      if (xSemaphoreTake(this->frame_done_sem_, pdMS_TO_TICKS(250)) != pdTRUE) {\n        ESP_LOGE(TAG, "Timed out waiting for RGB framebuffer handoff");\n        this->direct_present_failed_ = true;\n        return;\n      }\n    }\n'''
if old not in s:
    raise SystemExit('handoff block needle not found')
s = s.replace(old, new)
s = s.replace(
'''  this->pending_framebuffer_index_ = completed_index;\n  this->pending_frame_done_count_ = this->frame_done_count_;\n''',
'''  this->pending_framebuffer_index_ = completed_index;\n  this->pending_frame_done_count_ = this->frame_done_count_;\n  this->pending_vsync_count_ = this->vsync_count_;\n''')
s = s.replace(
'''void MipiRgb::loop() {\n  if (this->handle_ != nullptr)\n    esp_lcd_rgb_panel_restart(this->handle_);\n}\n''',
'''void MipiRgb::loop() {\n  // The restart workaround is useful for bounce-buffer recovery, but on ESP32-S3\n  // the direct-EDMA restart link is built from framebuffer 0. Restarting every\n  // VSYNC therefore fights multi-framebuffer scanout and can pin the DMA back to\n  // FB0. Leave direct EDMA running continuously and let IDF switch framebuffer\n  // links normally.\n  if (this->handle_ != nullptr && this->bounce_buffer_lines_ != 0)\n    esp_lcd_rgb_panel_restart(this->handle_);\n}\n''')
p.write_text(s)
