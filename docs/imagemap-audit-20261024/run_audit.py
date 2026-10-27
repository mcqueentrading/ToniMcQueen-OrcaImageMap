#!/usr/bin/env python3
"""Read-only source/recipe audit; generates only its own diagnostic evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def extract(text: str, marker: str, *, trailing_semicolon: bool = False) -> tuple[str, int]:
    """Extract a pinned named source block, including nested braced statements."""
    start = text.index(marker)
    if text.count(marker) != 1:
        raise ValueError(f"source marker must be unique: {marker}")
    brace = text.index("{", start)
    depth = 0
    for offset in range(brace, len(text)):
        char = text[offset]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end = offset + 1 + int(trailing_semicolon)
                if trailing_semicolon and text[offset + 1] != ";":
                    raise ValueError("struct terminator not found")
                return text[start:end], text.count("\n", 0, start) + 1
    raise ValueError(f"unterminated block: {marker}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    paths = [
        "src/libslic3r/TextureMapping.cpp", "src/libslic3r/TextureMapping.hpp",
        "src/libslic3r/TextureMappingOffset.cpp", "src/libslic3r/TextureMappingOffset.hpp",
        "src/libslic3r/TextureMappingContoning.cpp", "src/libslic3r/ImageMapRawFilamentOffsetAtlas.cpp",
        "src/libslic3r/SurfaceRecipeField.cpp", "src/libslic3r/SurfaceRecipeField.hpp",
        "src/libslic3r/Fill/Fill.cpp", "src/libslic3r/LayerRegion.cpp", "src/libslic3r/GCode.cpp",
        "src/libslic3r/Model.cpp", "src/libslic3r/Format/OBJ.cpp", "src/libslic3r/Format/ImportedTexture.cpp",
        "src/slic3r/GUI/Plater.cpp", "src/slic3r/GUI/MMUPaintedTexturePreview.cpp",
        "resources/shaders/140/painted_texture_preview.fs", "src/libslic3r/libslic3r.h",
        "deps_src/colorsolver/ColorSolver.cpp", "deps_src/colorsolver/ColorSolver.hpp",
    ]
    before = {str(repo / p): digest(repo / p) for p in paths}
    if args.project:
        before[str(args.project.resolve())] = digest(args.project)
    (out / "before.json").write_text(json.dumps(before, indent=2, sort_keys=True) + "\n")

    cpp = (repo / "src/libslic3r/TextureMappingOffset.cpp").read_text()
    hpp = (repo / "src/libslic3r/TextureMappingOffset.hpp").read_text()
    blocks = [
        ("src/libslic3r/TextureMappingOffset.hpp", *extract(hpp, "struct TextureMappingOffsetWeightField {", trailing_semicolon=True)),
        ("src/libslic3r/TextureMappingOffset.cpp", *extract(cpp, "float clamp01f(float v)")),
        ("src/libslic3r/TextureMappingOffset.cpp", *extract(cpp, "std::vector<float> sample_weight_field_components_impl(")),
    ]
    epsilon_source = (repo / "src/libslic3r/libslic3r.h").read_text()
    epsilon = re.search(r"static constexpr double EPSILON = [^;]+;", epsilon_source)
    if not epsilon:
        raise ValueError("production EPSILON not found")
    prefix = "// Exact diagnostic extracts from ImageMap (original author: sentientstardust).\n// See extracted_sources.json for provenance; not a replacement implementation.\n#include <algorithm>\n#include <cmath>\n#include <cstdint>\n#include <limits>\n#include <vector>\nnamespace ImagemapProbe {\n"
    fragments = prefix + epsilon.group() + "\n" + "\n".join(b[1] for b in blocks) + "\n}\n"
    (out / "field_fragments.hpp").write_text(fragments)
    (out / "extracted_sources.json").write_text(json.dumps([
        {"source": p, "first_line": line, "block_sha256": hashlib.sha256(body.encode()).hexdigest()}
        for p, body, line in blocks
    ], indent=2) + "\n")
    command = ["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-I" + str(out),
        "-I" + str(repo / "src/libslic3r"), str(Path(__file__).with_name("recipe_boundary_probe.cpp")),
        str(repo / "src/libslic3r/SurfaceRecipeField.cpp"), "-o", str(out / "recipe_boundary_probe")]
    build = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
    (out / "build.log").write_text(build.stdout + build.stderr)
    (out / "build_command.json").write_text(json.dumps(command, indent=2) + "\n")
    if build.returncode:
        raise RuntimeError(f"probe build failed; see {out / 'build.log'}")
    probe = subprocess.run([str(out / "recipe_boundary_probe")], capture_output=True, text=True, timeout=30, check=False)
    (out / "probe.json").write_text(probe.stdout)
    (out / "probe.stderr").write_text(probe.stderr)
    if probe.returncode:
        raise RuntimeError("probe failed; diagnostic output retained")
    result = json.loads(probe.stdout)
    repeat = subprocess.run([str(out / "recipe_boundary_probe")], capture_output=True, text=True, timeout=30, check=False)
    (out / "probe-repeat.json").write_text(repeat.stdout)
    if repeat.returncode or repeat.stdout != probe.stdout:
        raise RuntimeError("probe output was not deterministic")

    if args.project:
        with zipfile.ZipFile(args.project) as archive:
            info = archive.getinfo("Metadata/project_settings.config")
            if info.file_size > 8 * 1024 * 1024:
                raise ValueError("project settings exceed audit admission limit")
            settings_bytes = archive.read(info)
        settings = json.loads(settings_bytes)
        zones = settings.get("texture_mapping_definitions", "[]")
        zones = json.loads(zones) if isinstance(zones, str) else zones
        summary = {"archive": str(args.project.resolve()), "relationship_to_print": "not established",
            "settings_entry_sha256": hashlib.sha256(settings_bytes).hexdigest(),
            "geometry_read": False, "layer_height": settings.get("layer_height"), "zones": []}
        for zone in zones:
            opts = zone.get("texture_options", {})
            top = opts.get("top_surface_color_calibration", {})
            side = opts.get("side_surface_color_calibration", {})
            if isinstance(top, str):
                top = json.loads(top)
            if isinstance(side, str):
                side = json.loads(side)
            model = top.get("surface_calibrations", {}).get("top_surface", {}).get("weights", {}).get("nearest_measured_sample_global", {})
            if not model:
                model = top.get("weights", {}).get("nearest_measured_sample_global", {})
            sample_count = len(model.get("samples", []))
            side_count = len(side.get("swatches", side.get("side_surface", {}).get("swatches", [])))
            keys = ["mode", "color_model", "modulation_mode", "top_surface_image_printing_enabled",
                "top_surface_image_printing_method", "dithering_enabled", "dithering_method", "ordered_roles",
                "transmission_distance_calibration", "top_surface_contoning_stack_layers",
                "top_surface_contoning_color_prediction_mode", "top_surface_color_calibration_name", "side_surface_color_calibration_name"]
            summary["zones"].append({"zone_id": zone.get("zone_id"),
                "component_filaments": zone.get("component_filaments"),
                "options": {key: opts.get(key) for key in keys},
                "stored_top_samples": sample_count, "stored_side_swatches": side_count,
                "stored_top_stack_depth": model.get("stack_depth"),
                "stored_top_layer_heights_mm": model.get("layer_heights_mm"),
                "loaded_profiles_do_not_prove_execution": True})
        (out / "saved_project_summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

    after = {p: digest(Path(p)) for p in before}
    preservation = {"unchanged": before == after, "before": before, "after": after}
    (out / "preservation.json").write_text(json.dumps(preservation, indent=2, sort_keys=True) + "\n")
    if not preservation["unchanged"]:
        raise RuntimeError("source or inspected archive changed during audit; inspect preservation.json")
    print(json.dumps({"evidence": str(out), "mechanism_checks_passed": result["mechanism_checks_passed"],
        "mechanism_check_count": len(result["mechanism_checks"]),
        "all_strict_contracts_satisfied": result["all_strict_contracts_satisfied"],
        "protected_inputs_unchanged": True}))


if __name__ == "__main__":
    main()
