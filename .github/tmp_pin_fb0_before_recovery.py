from pathlib import Path

p = Path('esphome/components/mipi_rgb/mipi_rgb.cpp')
s = p.read_text()
old = '''  while (xSemaphoreTake(this->frame_done_sem_, 0) == pdTRUE) {\n  }\n  const uint32_t start_vsync = this->vsync_count_;\n  const esp_err_t err = esp_lcd_rgb_panel_restart(this->handle_);\n'''
new = '''  while (xSemaphoreTake(this->frame_done_sem_, 0) == pdTRUE) {\n  }\n\n  // draw_bitmap() with an IDF-owned framebuffer updates cur_fb_index and, in\n  // direct stream mode, reconnects every framebuffer DMA tail to that selected\n  // link. Do this before the S3 restart so the special FB0 restart link cannot\n  // fall through into whichever framebuffer happened to be current before the\n  // recovery request. It also performs the required cache writeback for FB0.\n  const esp_err_t select_err = esp_lcd_panel_draw_bitmap(\n      this->handle_, 0, 0, this->width_, this->height_, this->panel_framebuffers_[0]);\n  if (select_err != ESP_OK) {\n    ESP_LOGE(TAG, "Failed to select FB0 for RGB scanout recovery: %s", esp_err_to_name(select_err));\n    return false;\n  }\n  while (xSemaphoreTake(this->frame_done_sem_, 0) == pdTRUE) {\n  }\n\n  const uint32_t start_vsync = this->vsync_count_;\n  const esp_err_t err = esp_lcd_rgb_panel_restart(this->handle_);\n'''
if old not in s:
    raise SystemExit('recovery insertion point not found')
s = s.replace(old, new, 1)
p.write_text(s)
