#!/usr/bin/env python3
from pathlib import Path

renderer = Path('esphome/components/mipi_spi/fixed_mesh_renderer.h')
s = renderer.read_text()

replacements = [
    ('#include <algorithm>\n#include <cstddef>', '#include <algorithm>\n#include <array>\n#include <cstddef>'),
    ('''struct FixedMeshProjectedVertex {\n  int32_t x;\n  int32_t y;\n  int32_t cx;\n  int32_t cy;\n  int32_t depth;\n  uint8_t shade;\n  int32_t inv_w;\n};''',
     '''struct FixedMeshProjectedVertex {\n  // Screen coordinates only need int16 range; camera-space coordinates stay\n  // int32 so translated/clipped objects remain robust. This keeps the reusable\n  // projected scratch vertex at 24 bytes, matching the old Suzanne path.\n  int16_t x;\n  int16_t y;\n  int32_t cx;\n  int32_t cy;\n  int32_t depth;\n  int32_t inv_w;\n  uint8_t shade;\n};\nstatic_assert(sizeof(FixedMeshProjectedVertex) == 24,\n              "Projected fixed-mesh vertex should stay at 24 bytes");'''),
    ('template<typename DisplayT> class FixedMeshRenderer {',
     'template<typename DisplayT, size_t MAX_VERTICES> class FixedMeshRenderer {'),
    ('''        material.texture_width == 0 || material.texture_height == 0 || material.uv_frac_bits > 16 ||\n        params.near_depth <= 0 || params.focal <= 0)''',
     '''        material.texture_width == 0 || material.texture_height == 0 || material.uv_frac_bits > 16 ||\n        mesh.vertex_count > MAX_VERTICES || params.near_depth <= 0 || params.focal <= 0)'''),
    ('''    if (this->projected_.size() < mesh.vertex_count)\n      this->projected_.resize(mesh.vertex_count);\n\n''', ''),
    ('''        p.inv_w = static_cast<int32_t>((int64_t{1} << FIXED_MESH_INV_W_BITS) / depth);\n        p.x = center_x + static_cast<int32_t>((static_cast<int64_t>(camera_x) * params.focal) / depth);\n        p.y = center_y - static_cast<int32_t>((static_cast<int64_t>(camera_y) * params.focal) / depth);''',
     '''        p.inv_w = static_cast<int32_t>((int64_t{1} << FIXED_MESH_INV_W_BITS) / depth);\n        const int32_t screen_x = center_x +\n            static_cast<int32_t>((static_cast<int64_t>(camera_x) * params.focal) / depth);\n        const int32_t screen_y = center_y -\n            static_cast<int32_t>((static_cast<int64_t>(camera_y) * params.focal) / depth);\n        p.x = static_cast<int16_t>(std::clamp<int32_t>(screen_x, -32768, 32767));\n        p.y = static_cast<int16_t>(std::clamp<int32_t>(screen_y, -32768, 32767));'''),
    ('  std::vector<FixedMeshProjectedVertex> projected_{};',
     '  std::array<FixedMeshProjectedVertex, MAX_VERTICES> projected_{};'),
]

for old, new in replacements:
    if old not in s:
        raise SystemExit(f'expected renderer text not found:\n{old[:160]}')
    s = s.replace(old, new, 1)
renderer.write_text(s)

wrapper = Path('esphome/components/mipi_spi/suzanne_blender_uv_perspective.h')
w = wrapper.read_text()
old = '  static FixedMeshRenderer<DisplayT> renderer;'
new = '  static FixedMeshRenderer<DisplayT, GOURAUD_TOTAL_VERTS> renderer;'
if old not in w:
    raise SystemExit('expected Suzanne renderer instantiation not found')
wrapper.write_text(w.replace(old, new, 1))
