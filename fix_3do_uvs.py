#!/usr/bin/env python3
"""
fix_3do_uvs.py - Fix UVs and model radius in Jedi Knight 3DO files.

Targets 3DO 2.1 files exported by the Blender Sith addon.

  - Divides texture vertex UVs by a scale factor (default 4) to convert
    1024-range pixel UVs into the 256 range the stock Sith engine expects.
  - Copies the first mesh's RADIUS to the model-level RADIUS.

Usage:
    python fix_3do_uvs.py input.3do                   # overwrites in place, scale=4
    python fix_3do_uvs.py input.3do -o output.3do     # writes to new file
    python fix_3do_uvs.py input.3do -s 2              # custom scale divisor
"""

import argparse
import re


def normalize_mat_name(name):
    """Lowercase and strip .mat suffix for comparison."""
    name = name.strip().lower()
    if name.endswith(".mat"):
        name = name[:-4]
    return name


def fix_3do(input_path, output_path, scale, skip_mats=None):
    if skip_mats is None:
        skip_mats = set()

    with open(input_path, "r") as f:
        lines = f.readlines()

    # --- First pass: find the first mesh's RADIUS and material map ---
    mesh_radius = None
    in_mesh = False
    mat_name_to_idx = {}
    in_materials = False

    for line in lines:
        stripped = line.strip()

        # Parse MATERIALS section
        if stripped.startswith("MATERIALS"):
            in_materials = True
            continue
        if in_materials:
            mat_match = re.match(r"\s*(\d+):\s*(\S+)", stripped)
            if mat_match:
                idx = int(mat_match.group(1))
                name = mat_match.group(2)
                mat_name_to_idx[name] = idx
            elif stripped and not stripped.startswith("#"):
                in_materials = False

        # Find first mesh RADIUS
        if stripped.startswith("MESH "):
            in_mesh = True
        elif in_mesh and stripped.startswith("RADIUS") and mesh_radius is None:
            m = re.match(r"RADIUS\s+([\d.]+)", stripped)
            if m:
                mesh_radius = m.group(1)

    # Build skip_indices from skip_mats and material map
    skip_indices = set()
    norm_skip = {normalize_mat_name(s) for s in skip_mats if s}
    for name, idx in mat_name_to_idx.items():
        if normalize_mat_name(name) in norm_skip:
            skip_indices.add(idx)

    if skip_mats:
        print(f"Skipping materials: {skip_mats} -> indices {skip_indices}")

    # Re-parse faces now that skip_indices is populated
    skip_texverts = set()
    in_faces = False
    faces_count = 0
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("FACES "):
            fm = re.search(r"FACES\s+(\d+)", stripped)
            if fm:
                faces_expected = int(fm.group(1))
                faces_count = 0
                in_faces = True
            continue
        if in_faces:
            face_match = re.match(r"\s*\d+:\s+(\d+)\s+", stripped)
            if face_match:
                mat_idx = int(face_match.group(1))
                if mat_idx in skip_indices:
                    pairs = re.findall(r"(\d+),\s*(\d+)", stripped)
                    for _, tv in pairs:
                        skip_texverts.add(int(tv))
                faces_count += 1
                if faces_count >= faces_expected:
                    in_faces = False
                continue
            elif stripped == "" or stripped.startswith("#"):
                continue
            else:
                in_faces = False

    if skip_texverts:
        print(f"Skipping {len(skip_texverts)} texture vertices used by skipped materials")

    # --- Second pass: fix model radius + UVs ---
    out = []
    in_tex_verts = False
    tex_vert_count = 0
    tex_verts_expected = 0
    model_radius_fixed = False
    hit_first_mesh = False

    for line in lines:
        stripped = line.strip()

        if stripped.startswith("MESH "):
            hit_first_mesh = True

        # Fix model-level RADIUS (before any MESH)
        if (not hit_first_mesh and not model_radius_fixed
                and stripped.startswith("RADIUS") and mesh_radius):
            old = re.search(r"RADIUS\s+([\d.]+)", stripped)
            if old:
                leading = line[:len(line) - len(line.lstrip())]
                out.append(f"{leading}RADIUS    {mesh_radius}\n")
                model_radius_fixed = True
                print(f"Fixed model RADIUS: {old.group(1)} -> {mesh_radius}")
                continue

        # Detect TEXTURE VERTICES block
        if stripped.startswith("TEXTURE VERTICES"):
            m = re.search(r"TEXTURE VERTICES\s+(\d+)", stripped)
            if m:
                tex_verts_expected = int(m.group(1))
                tex_vert_count = 0
                in_tex_verts = True
            out.append(line)
            continue

        # Scale UV lines
        if in_tex_verts:
            uv = re.match(r"^(\s*(\d+):\s+)(-?[\d.]+)\s+(-?[\d.]+)\s*$", stripped)
            if uv:
                tv_idx = int(uv.group(2))
                if tv_idx in skip_texverts:
                    out.append(line)
                else:
                    prefix = uv.group(1)
                    u = float(uv.group(3)) / scale
                    v = float(uv.group(4)) / scale
                    leading = line[:len(line) - len(line.lstrip())]
                    out.append(f"{leading}{prefix}{u:.6f} {v:.6f}\n")
                tex_vert_count += 1
                if tex_vert_count >= tex_verts_expected:
                    in_tex_verts = False
                continue
            elif stripped == "" or stripped.startswith("#"):
                out.append(line)
                continue
            else:
                in_tex_verts = False

        out.append(line)

    with open(output_path, "w", newline="\r\n") as f:
        f.writelines(out)

    print(f"Fixed {tex_vert_count} texture vertices (divided by {scale})")
    print(f"Written to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Fix UVs and model radius in Jedi Knight .3DO files."
    )
    parser.add_argument("input", help="Input .3do file")
    parser.add_argument("-o", "--output", help="Output .3do file (default: overwrite input)")
    parser.add_argument(
        "-s", "--scale", type=float, default=4.0,
        help="UV divisor (default: 4 for 1024->256)"
    )
    parser.add_argument(
        "--skipmats", default="",
        help="Semicolon-separated material names to skip UV scaling (e.g. 'dynscreen;other')"
    )
    args = parser.parse_args()
    output = args.output if args.output else args.input
    skip_mats = {s for s in args.skipmats.split(";") if s} if args.skipmats else set()
    fix_3do(args.input, output, args.scale, skip_mats)


if __name__ == "__main__":
    main()
