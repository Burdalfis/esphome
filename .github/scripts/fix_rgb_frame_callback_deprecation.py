from pathlib import Path

p = Path('esphome/components/mipi_rgb/mipi_rgb.cpp')
s = p.read_text()
old = '''#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)\n        callbacks.on_frame_buf_complete = &MipiRgb::frame_done_callback_;\n#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 2)\n        callbacks.on_bounce_frame_finish = &MipiRgb::frame_done_callback_;\n#else\n        callbacks.on_vsync = &MipiRgb::frame_done_callback_;\n#endif\n'''
new = '''#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)\n        callbacks.on_frame_buf_complete = &MipiRgb::frame_done_callback_;\n#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 2)\n        callbacks.on_bounce_frame_finish = &MipiRgb::frame_done_callback_;\n#else\n        callbacks.on_vsync = &MipiRgb::frame_done_callback_;\n#endif\n'''
if old not in s:
    raise SystemExit('callback selection block not found')
p.write_text(s.replace(old, new, 1))
