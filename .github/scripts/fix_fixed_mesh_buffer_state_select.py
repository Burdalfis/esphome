from pathlib import Path

for rel in [
    'esphome/components/fixed_mesh_3d/fixed_mesh_renderer.h',
    'esphome/components/mipi_spi/fixed_mesh_renderer.h',
]:
    p = Path(rel)
    s = p.read_text()
    old = '''  bool select_buffer_state_(PixelT *framebuffer) {\n    BufferState *empty = nullptr;\n'''
    new = '''  bool select_buffer_state_(PixelT *framebuffer) {\n    this->buffer_state_ = nullptr;\n    BufferState *empty = nullptr;\n'''
    if old not in s:
        raise SystemExit(f'marker missing in {rel}')
    p.write_text(s.replace(old, new, 1))
