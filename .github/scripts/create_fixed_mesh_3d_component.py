from pathlib import Path

src = Path("esphome/components/mipi_spi")
dst = Path("esphome/components/fixed_mesh_3d")
dst.mkdir(parents=True, exist_ok=True)

files = [
    "fixed_mesh_renderer.h",
    "suzanne_data.h",
    "suzanne_gouraud.h",
    "suzanne_textured_gouraud.h",
    "suzanne_perspective.h",
    "suzanne_blender_uv_data.h",
    "suzanne_fixed_mesh.h",
    "suzanne_blender_uv_perspective.h",
]
for name in files:
    (dst / name).write_bytes((src / name).read_bytes())

(dst / "__init__.py").write_text(
    'import esphome.config_validation as cv\n\n'
    'CODEOWNERS = []\n'
    'CONFIG_SCHEMA = cv.Schema({})\n'
)
