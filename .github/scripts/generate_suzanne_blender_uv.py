#!/usr/bin/env python3

from pathlib import Path
import re
import urllib.request

BLENDER_COMMIT = "ec438d7429e51b33ff8958da0c21def018d43835"
SOURCE_URL = (
    "https://raw.githubusercontent.com/blender/blender/"
    f"{BLENDER_COMMIT}/source/blender/bmesh/operators/bmo_primitive.cc"
)
UV_PATH = Path("esphome/components/mipi_spi/suzanne_blender_uv_data.h")
CYLINDER_PATH = Path("esphome/components/mipi_spi/suzanne_uv_perspective.h")
RENDERER_PATH = Path("esphome/components/mipi_spi/suzanne_blender_uv_perspective.h")


def generate_uv_data() -> None:
    text = urllib.request.urlopen(SOURCE_URL, timeout=30).read().decode("utf-8")
    match = re.search(
        r"static const float monkeyuvs\[\] = \{\s*(.*?)\s*\};",
        text,
        re.S,
    )
    if match is None:
        raise RuntimeError("Could not find Blender monkeyuvs array")

    values = [
        float(value)
        for value in re.findall(r"([-+]?(?:\d+\.\d+|\d+))f", match.group(1))
    ]
    if len(values) != 1968 * 2:
        raise RuntimeError(f"Expected 3936 monkey UV floats, got {len(values)}")

    texture_size = 128
    frac_bits = 7
    coord_max = (texture_size - 1) << frac_bits
    pairs: list[tuple[int, int]] = []
    for i in range(0, len(values), 2):
        u = values[i]
        v = values[i + 1]
        uq = round(u * coord_max)
        # Blender UVs use bottom-left origin; our texture rows use top-left.
        vq = round((1.0 - v) * coord_max)
        if not (0 <= uq <= coord_max and 0 <= vq <= coord_max):
            raise RuntimeError(f"UV out of range at corner {i // 2}: {(u, v)}")
        pairs.append((uq, vq))

    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace esphome::mipi_spi::demo3d {")
    out.append("")
    out.append("// Face-corner UV atlas from Blender's Suzanne primitive (monkeyuvs).")
    out.append("// Blender source: bmo_primitive.cc, GPL-2.0-or-later.")
    out.append(f"// Pinned upstream commit: {BLENDER_COMMIT}")
    out.append("// Coordinates are pre-quantized to Q7 texel units for a 128x128 texture.")
    out.append("struct SuzanneBlenderUVQ7 {")
    out.append("  uint16_t u;")
    out.append("  uint16_t v;")
    out.append("};")
    out.append("")
    out.append("static constexpr int SUZANNE_BLENDER_TEXTURE_SIZE = 128;")
    out.append("static constexpr int SUZANNE_BLENDER_UV_FRAC_BITS = 7;")
    out.append("static constexpr size_t SUZANNE_BLENDER_UV_COUNT = 1968;")
    out.append("")
    out.append(
        "static constexpr std::array<SuzanneBlenderUVQ7, SUZANNE_BLENDER_UV_COUNT> "
        "SUZANNE_BLENDER_UVS_Q7 = {{"
    )
    for start in range(0, len(pairs), 8):
        row = pairs[start : start + 8]
        out.append("    " + ", ".join(f"{{{u}, {v}}}" for u, v in row) + ",")
    out.append("}};")
    out.append("")
    out.append("}  // namespace esphome::mipi_spi::demo3d")
    out.append("")

    UV_PATH.write_text("\n".join(out), encoding="utf-8")
    print(f"Wrote {UV_PATH} with {len(pairs)} face-corner UVs")


def generate_renderer() -> None:
    s = CYLINDER_PATH.read_text(encoding="utf-8")

    s = s.replace(
        '#include "suzanne_perspective.h"',
        '#include "suzanne_perspective.h"\n#include "suzanne_blender_uv_data.h"',
        1,
    )

    helper_start = s.index("// Seam-aware cylindrical unwrap for Suzanne.")
    helper_end = s.index("template<typename DisplayT>", helper_start)
    helper = '''// Blender's artist-authored face-corner UV atlas.
//
// UVs are deliberately NOT stored in PerspectiveRenderVertex: one geometric
// vertex can have several UVs at island seams. Shared projected positions and
// normals therefore remain exactly the same size as the previous renderer; the
// three face-corner UVs are applied only when a visible triangle is submitted.
static constexpr size_t suzanne_blender_expected_uv_count() {
  size_t count = 0;
  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];
    count += (quad ? 4u : 3u) * 2u;
  }
  return count;
}

static_assert(suzanne_blender_expected_uv_count() == SUZANNE_BLENDER_UV_COUNT,
              "Blender Suzanne UV stream must match the compact mirrored topology");

// 128x128 indexed UV diagnostic texture. This lives entirely in flash. The
// asymmetric U/V gradient, checker field and 8/32-texel grid lines make island
// orientation, scale and seams easy to see on the real Blender unwrap.
static constexpr std::array<uint8_t, SUZANNE_BLENDER_TEXTURE_SIZE * SUZANNE_BLENDER_TEXTURE_SIZE>
make_suzanne_blender_uv_texture() {
  std::array<uint8_t, SUZANNE_BLENDER_TEXTURE_SIZE * SUZANNE_BLENDER_TEXTURE_SIZE> texture{};
  for (int y = 0; y < SUZANNE_BLENDER_TEXTURE_SIZE; y++) {
    for (int x = 0; x < SUZANNE_BLENDER_TEXTURE_SIZE; x++) {
      int index = 28 + (x * 70) / (SUZANNE_BLENDER_TEXTURE_SIZE - 1) +
                  (y * 30) / (SUZANNE_BLENDER_TEXTURE_SIZE - 1);
      if ((((x >> 4) ^ (y >> 4)) & 1) != 0)
        index += 40;
      if ((x & 7) == 0 || (y & 7) == 0)
        index = 205;
      if ((x & 31) < 2 || (y & 31) < 2)
        index = 250;
      texture[static_cast<size_t>(y) * SUZANNE_BLENDER_TEXTURE_SIZE + x] =
          static_cast<uint8_t>(index);
    }
  }
  return texture;
}

static constexpr auto SUZANNE_BLENDER_UV_TEXTURE = make_suzanne_blender_uv_texture();

'''
    s = s[:helper_start] + helper + s[helper_end:]

    s = s.replace(
        "render_suzanne_uv_perspective",
        "render_suzanne_blender_uv_perspective",
        1,
    )
    s = s.replace(
        "Suzanne UV perspective demo expects a 16-bit framebuffer",
        "Suzanne Blender UV perspective demo expects a 16-bit framebuffer",
        1,
    )

    old_inv = '''    const int32_t inv_w = static_cast<int32_t>((int64_t{1} << PERSPECTIVE_INV_W_BITS) / depth);
    const int32_t u_q8 = uvp_base_u_q8(i);
    const int32_t v_q8 = uvp_base_v_q8(i);
'''
    if old_inv not in s:
        raise RuntimeError("Could not find cylindrical per-vertex UV setup")
    s = s.replace(
        old_inv,
        '''    const int32_t inv_w = static_cast<int32_t>((int64_t{1} << PERSPECTIVE_INV_W_BITS) / depth);
''',
        1,
    )

    old_store = '''    p.inv_w = inv_w;
    p.u_over_w = static_cast<int32_t>(static_cast<int64_t>(u_q8) * inv_w);
    p.v_over_w = static_cast<int32_t>(static_cast<int64_t>(v_q8) * inv_w);
'''
    if old_store not in s:
        raise RuntimeError("Could not find cylindrical projected UV storage")
    s = s.replace(
        old_store,
        '''    p.inv_w = inv_w;
    p.u_over_w = 0;
    p.v_over_w = 0;
''',
        1,
    )

    # 128x128 needs Q7, not Q8, to keep UV*reciprocal-depth safely in int32_t.
    s = s.replace("PERSPECTIVE_UV_FRAC_BITS", "SUZANNE_BLENDER_UV_FRAC_BITS")

    old_sampler = '''          const int tex_u = (u >> 16) & (TG_TEXTURE_SIZE - 1);
          const int tex_v = std::clamp<int32_t>(v >> 16, 0, TG_TEXTURE_SIZE - 1);
          const uint8_t texel =
              TEXTURED_GOURAUD_TEXTURE[static_cast<size_t>(tex_v) * TG_TEXTURE_SIZE + tex_u];
'''
    if old_sampler not in s:
        raise RuntimeError("Could not find cylindrical texture sampler")
    s = s.replace(
        old_sampler,
        '''          const int tex_u = std::clamp<int32_t>(u >> 16, 0, SUZANNE_BLENDER_TEXTURE_SIZE - 1);
          const int tex_v = std::clamp<int32_t>(v >> 16, 0, SUZANNE_BLENDER_TEXTURE_SIZE - 1);
          const uint8_t texel = SUZANNE_BLENDER_UV_TEXTURE[
              static_cast<size_t>(tex_v) * SUZANNE_BLENDER_TEXTURE_SIZE + tex_u];
''',
        1,
    )

    tri_start = s.index("  auto render_triangle =")
    face_start = s.index(
        "  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {", tri_start
    )
    triangle_block = '''  auto render_triangle = [&](size_t ia, size_t ib, size_t ic,
                           const SuzanneBlenderUVQ7 &uva,
                           const SuzanneBlenderUVQ7 &uvb,
                           const SuzanneBlenderUVQ7 &uvc) {
    const auto &pa = projected[ia];
    const auto &pb = projected[ib];
    const auto &pc = projected[ic];

    const int32_t e1x = pb.cx - pa.cx;
    const int32_t e1y = pb.cy - pa.cy;
    const int32_t e1z = pb.cz - pa.cz;
    const int32_t e2x = pc.cx - pa.cx;
    const int32_t e2y = pc.cy - pa.cy;
    const int32_t e2z = pc.cz - pa.cz;
    const int32_t nx = e1y * e2z - e1z * e2y;
    const int32_t ny = e1z * e2x - e1x * e2z;
    const int32_t nz = e1x * e2y - e1y * e2x;
    const int64_t view_dot = static_cast<int64_t>(nx) * pa.cx + static_cast<int64_t>(ny) * pa.cy +
                             static_cast<int64_t>(nz) * (CAMERA_Z + pa.cz);
    if (view_dot >= 0)
      return;

    PerspectiveRenderVertex a = pa;
    PerspectiveRenderVertex b = pb;
    PerspectiveRenderVertex c = pc;
    a.u_over_w = static_cast<int32_t>(uva.u) * a.inv_w;
    a.v_over_w = static_cast<int32_t>(uva.v) * a.inv_w;
    b.u_over_w = static_cast<int32_t>(uvb.u) * b.inv_w;
    b.v_over_w = static_cast<int32_t>(uvb.v) * b.inv_w;
    c.u_over_w = static_cast<int32_t>(uvc.u) * c.inv_w;
    c.v_over_w = static_cast<int32_t>(uvc.v) * c.inv_w;

    stats.visible_triangles++;
    fill_triangle(a, b, c);
    stats.rasterized_triangles++;
  };

'''
    s = s[:tri_start] + triangle_block + s[face_start:]

    face_start = s.index(
        "  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {", tri_start
    )
    dirty_start = s.index("  int dirty_x0;", face_start)
    face_block = '''  size_t uv_cursor = 0;
  for (size_t i = 0; i < SUZANNE_HALF_FACE_COUNT; i++) {
    const size_t a = gouraud_face_index(i, SUZANNE_FACES[i][0]);
    const size_t b = gouraud_face_index(i, SUZANNE_FACES[i][1]);
    const size_t c = gouraud_face_index(i, SUZANNE_FACES[i][2]);
    const size_t d = gouraud_face_index(i, SUZANNE_FACES[i][3]);
    const bool quad = SUZANNE_FACES[i][3] != SUZANNE_FACES[i][2];
    const size_t corner_count = quad ? 4u : 3u;

    const auto *left_uv = SUZANNE_BLENDER_UVS_Q7.data() + uv_cursor;
    const auto *right_uv = left_uv + corner_count;

    render_triangle(a, b, c, left_uv[0], left_uv[1], left_uv[2]);
    if (quad)
      render_triangle(a, c, d, left_uv[0], left_uv[2], left_uv[3]);

    const size_t ma = GOURAUD_HALF_VERTS + c;
    const size_t mb = GOURAUD_HALF_VERTS + b;
    const size_t mc = GOURAUD_HALF_VERTS + a;
    const size_t md = GOURAUD_HALF_VERTS + d;
    render_triangle(ma, mb, mc, right_uv[0], right_uv[1], right_uv[2]);
    if (quad)
      render_triangle(ma, mc, md, right_uv[0], right_uv[2], right_uv[3]);

    uv_cursor += corner_count * 2u;
  }

'''
    s = s[:face_start] + face_block + s[dirty_start:]

    if "uvp_base_" in s or "UVP_" in s:
        raise RuntimeError("Cylindrical UV helpers survived renderer conversion")

    RENDERER_PATH.write_text(s, encoding="utf-8")
    print(f"Wrote {RENDERER_PATH}")


def main() -> None:
    generate_uv_data()
    generate_renderer()


if __name__ == "__main__":
    main()
