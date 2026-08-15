from pathlib import Path

path = Path("esphome/components/mipi_rgb/mipi_rgb.cpp")
text = path.read_text()

needle = "static constexpr size_t MIPI_RGB_MAX_CMD_LOG_BYTES = 64;\n"
insert = """static constexpr size_t MIPI_RGB_MAX_CMD_LOG_BYTES = 64;\n\n// ESP32-S3 RGB panels stream their framebuffer from PSRAM through internal-RAM\n// bounce buffers. Give the S3 twenty scanlines of headroom so short cache/PSRAM\n// stalls are less likely to starve the LCD DMA. Keep the existing P4 footprint.\n#if defined(USE_ESP32_VARIANT_ESP32S3)\nstatic constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 20;\n#else\nstatic constexpr size_t MIPI_RGB_BOUNCE_BUFFER_LINES = 10;\n#endif\n"""
if needle not in text:
    raise SystemExit("constant insertion point not found")
text = text.replace(needle, insert, 1)

old = "  config.bounce_buffer_size_px = this->width_ * 10;\n"
new = "  config.bounce_buffer_size_px = this->width_ * MIPI_RGB_BOUNCE_BUFFER_LINES;\n"
if old not in text:
    raise SystemExit("bounce buffer assignment not found")
text = text.replace(old, new, 1)

old_log = '  ESP_LOGCONFIG(TAG, "MipiRgb setup complete");\n'
new_log = '''  ESP_LOGCONFIG(TAG, "MipiRgb setup complete");\n  ESP_LOGCONFIG(TAG, "RGB bounce buffer: %u lines (%u pixels per buffer)",\n                static_cast<unsigned>(MIPI_RGB_BOUNCE_BUFFER_LINES),\n                static_cast<unsigned>(this->width_ * MIPI_RGB_BOUNCE_BUFFER_LINES));\n'''
if old_log not in text:
    raise SystemExit("setup log insertion point not found")
text = text.replace(old_log, new_log, 1)

path.write_text(text)
