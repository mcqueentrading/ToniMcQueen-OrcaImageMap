#!/usr/bin/env python3
"""Create an isolated textured top-stack integration fixture; never upload or print."""

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
INPUTS = Path("/home/unknown/Desktop/Documents/3dprints/OUR-TRIPO-TO-PRINT-pipeline/surfacemix-orca-observation-20261024/input-final")
CORE = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
MAT = "http://schemas.microsoft.com/3dmanufacturing/material/2015/02"
COMPONENTS = ["cyan", "magenta", "yellow", "white"]


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def lab(rgb):
    values = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb]
    xyz = [sum(a * b for a, b in zip(row, values)) / white for row, white in zip(
        [(0.4124564, 0.3575761, 0.1804375), (0.2126729, 0.7151522, 0.0721750), (0.0193339, 0.1191920, 0.9503041)],
        [0.95047, 1.0, 1.08883])]
    x, y, z = [v ** (1 / 3) if v > 216 / 24389 else (24389 / 27 * v + 16) / 116 for v in xyz]
    return 116 * y - 16, 500 * (x - y), 200 * (y - z)


def png_texture(colours):
    def chunk(name, value):
        return struct.pack(">I", len(value)) + name + value + struct.pack(">I", zlib.crc32(name + value))
    rows = b"".join(b"\0" + bytes(colours[0] + [255]) * 32 + bytes(colours[1] + [255]) * 32 for _ in range(32))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 32, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def fixture_mesh():
    ET.register_namespace("", CORE)
    ET.register_namespace("m", MAT)
    model = ET.Element(f"{{{CORE}}}model", unit="millimeter")
    ET.SubElement(model, f"{{{CORE}}}metadata", name="Application").text = "SurfaceMix isolated test fixture generator"
    ET.SubElement(model, f"{{{CORE}}}metadata", name="Title").text = "SurfaceMix top-stack TEST ONLY - NOT PRINT VALIDATED"
    resources = ET.SubElement(model, f"{{{CORE}}}resources")
    ET.SubElement(resources, f"{{{MAT}}}texture2d", id="10", path="/3D/Textures/targets.png", contenttype="image/png")
    group = ET.SubElement(resources, f"{{{MAT}}}texture2dgroup", id="11", texid="10")
    for u in (0.25, 0.75):
        ET.SubElement(group, f"{{{MAT}}}tex2coord", u=str(u), v="0.5")
    obj = ET.SubElement(resources, f"{{{CORE}}}object", id="1", type="model")
    mesh = ET.SubElement(obj, f"{{{CORE}}}mesh")
    vertices = ET.SubElement(mesh, f"{{{CORE}}}vertices")
    triangles = ET.SubElement(mesh, f"{{{CORE}}}triangles")
    faces = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
             (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    for cell, x in enumerate((100, 114)):
        points = [(x, 100, 0), (x + 12, 100, 0), (x + 12, 108, 0), (x, 108, 0),
                  (x, 100, 3.2), (x + 12, 100, 3.2), (x + 12, 108, 3.2), (x, 108, 3.2)]
        for px, py, pz in points:
            ET.SubElement(vertices, f"{{{CORE}}}vertex", x=str(px), y=str(py), z=str(pz))
        for a, b, c in faces:
            ET.SubElement(triangles, f"{{{CORE}}}triangle", v1=str(a + cell * 8), v2=str(b + cell * 8),
                          v3=str(c + cell * 8), pid="11", p1=str(cell), p2=str(cell), p3=str(cell))
    build = ET.SubElement(model, f"{{{CORE}}}build")
    ET.SubElement(build, f"{{{CORE}}}item", objectid="1")
    return ET.tostring(model, encoding="utf-8", xml_declaration=True)


def prepare(out):
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT / "resources/surfacemix/creator5-cmyw-top-preview.v1.json"
    catalogue = json.loads(source.read_text())
    records = catalogue["sources"][0]["records"]
    selected = [next(r for r in records if r["sample_id"] == name) for name in ("patch_001", "patch_002")]
    colours = [[round(c * 255) for c in r["measured_rgb"]["channels"]] for r in selected]
    # Independently resolve the actual 8-bit texture targets, rather than assume round-to-byte is colour-neutral.
    labs = [lab(r["measured_rgb"]["channels"]) for r in records]
    resolved = []
    for colour in colours:
        target = lab([v / 255 for v in colour])
        index = min(range(len(records)), key=lambda i: sum((a - b) ** 2 for a, b in zip(target, labs[i])))
        resolved.append(records[index])
    assert set().union(*(set(r["recipe"]["surface_to_deep"]) for r in resolved)) == set(COMPONENTS)
    calibration = copy.deepcopy(catalogue["sources"][0]["raw_document"])
    calibration["surfacemix_top_stacks"] = {
        "schema": "surfacemix.imagemap-top-stacks.v1", "enabled": True, "experimental": True,
        "coordinate_mode": "build_z_top", "assume_normalized_srgb": True,
        "canonical_to_filament_slot": [1, 2, 3, 4], "catalogue": catalogue,
    }
    zone = {"schema": 2, "uid": 2026102401, "zone_id": 99, "enabled": True, "surface_pattern": "image_texture",
            "anchor_filaments": [1, 2], "component_filaments": [1, 2, 3, 4], "display_color": "#00BBAA",
            "texture_options": {"mode": "target_color", "color_model": "cmyw", "top_surface_image_printing_enabled": True,
                "top_surface_image_printing_method": "contoning", "top_surface_contoning_stack_layers": 19,
                "top_surface_contoning_pattern_filaments": 19, "top_surface_contoning_colored_surfaces": "upper",
                "top_surface_contoning_layer_phase_enabled": False, "top_surface_contoning_blue_noise_error_diffusion_enabled": False,
                "top_surface_contoning_min_feature_mm": 0.4, "top_surface_color_calibration": calibration}}
    process = json.loads((INPUTS / "process.aligned-solid.json").read_text())
    process.update({"layer_height": "0.08", "initial_layer_print_height": "0.08", "top_shell_layers": "19",
                    "top_shell_thickness": "1.52", "sparse_infill_density": "100%", "enable_prime_tower": "0",
                    "solid_infill_filament": "99", "wall_filament": "1", "texture_mapping_definitions": json.dumps([zone])})
    write_json(out / "process.json", process)
    machine = json.loads((INPUTS / "machine.offline.json").read_text())
    machine.update({"filament_map": ["1", "2", "3", "4"], "filament_map_mode": "Manual",
                    "change_filament_gcode": "; EXPERIMENTAL FIXTURE ONLY - DO NOT PRINT\nT[next_extruder]\n"})
    write_json(out / "machine.json", machine)
    filament_base = json.loads((INPUTS / "filament.default-existing-cyan.json").read_text())
    for name, colour in zip(COMPONENTS, ("#00AEEF", "#EC008C", "#FFF200", "#FFFFFF")):
        filament = copy.deepcopy(filament_base)
        filament.update({"name": "SurfaceMix TEST " + name, "filament_settings_id": ["SurfaceMix TEST " + name],
                         "filament_colour": [colour], "filament_is_support": ["0"]})
        write_json(out / (name + ".json"), filament)
    settings = '<?xml version="1.0"?><config><object id="1"><metadata key="name" value="SurfaceMix TEST ONLY"/>'
    settings += '<metadata key="extruder" value="99"/><metadata face_count="24"/><part id="1" subtype="normal_part">'
    settings += '<metadata key="extruder" value="99"/></part></object></config>'
    content_types = ('<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
        '<Default Extension="png" ContentType="image/png"/></Types>')
    rels = ('<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(out / "top-stack-fixture.3mf", "x", zipfile.ZIP_DEFLATED) as archive:
        for name, value in {"[Content_Types].xml": content_types, "_rels/.rels": rels,
                "3D/3dmodel.model": fixture_mesh(), "3D/Textures/targets.png": png_texture(colours),
                "Metadata/model_settings.config": settings}.items():
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, value)
    manifest = {"scope": "EXPERIMENTAL FIXTURE ONLY", "printing_enabled": False, "product_obj_used": False,
        "physical_colour_validated": False, "catalogue_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "canonical_to_filament_slot": [1, 2, 3, 4], "fixture_height_mm": 3.2,
        "cells": [{"sample_id": r["sample_id"], "source_pointer": r["source_pointer"], "target_rgb8": colour,
            "surface_to_deep": r["recipe"]["surface_to_deep"], "layer_heights_mm": r["recipe"]["layer_heights_mm"]}
            for r, colour in zip(resolved, colours)]}
    write_json(out / "fixture-manifest.json", manifest)
    return manifest


def inspect_gcode(out):
    manifest = json.loads((out / "fixture-manifest.json").read_text())
    tool, current_z, e, relative_e, x, y, move_z, height = None, None, 0.0, True, 0.0, 0.0, None, None
    witnesses = [dict() for _ in range(2)]
    conflicts = []
    interval_conflicts = []
    for number, raw in enumerate((out / "plate_1.gcode").read_text().splitlines(), 1):
        if raw.startswith((";Z_HEIGHT:", ";Z:")):
            current_z = float(raw.split(":", 1)[1])
        if raw.startswith(";HEIGHT:"):
            height = float(raw.split(":", 1)[1])
        line = raw.split(";", 1)[0].strip()
        if re.fullmatch(r"T\d+", line):
            tool = int(line[1:])
        if line == "M82":
            relative_e = False
        if line == "M83":
            relative_e = True
        fields = {k: float(v) for k, v in re.findall(r"([XYZE])([-+]?(?:\d+(?:\.\d*)?|\.\d+))", line)}
        if line.startswith("G92"):
            e = fields.get("E", e)
            move_z = fields.get("Z", move_z)
        if not re.match(r"G[01](?:\s|$)", line):
            continue
        old_x, old_y = x, y
        x, y = fields.get("X", x), fields.get("Y", y)
        move_z = fields.get("Z", move_z)
        amount = fields.get("E", 0.0) if relative_e else fields.get("E", e) - e
        if "E" in fields:
            e = fields["E"] if not relative_e else e + fields["E"]
        if amount <= 0 or current_z is None or math.hypot(x - old_x, y - old_y) < 1e-6:
            continue
        depth = round((3.2 - current_z) / 0.08)
        if depth < 0 or depth >= 19:
            continue
        for cell, left in enumerate((100, 114)):
            # Interior segment midpoints exclude perimeter sweeps; this is not a volume-coverage measurement.
            if left + 2 < (x + old_x) / 2 < left + 10 and 102 < (y + old_y) / 2 < 106:
                if (abs(current_z - (3.2 - depth * 0.08)) > 1e-6 or height is None or abs(height - 0.08) > 1e-6 or
                        move_z is None or abs(move_z - current_z) > 1e-6):
                    interval_conflicts.append({"cell": cell, "depth": depth, "line": number, "layer_z": current_z,
                                               "commanded_z": move_z, "height": height})
                    continue
                expected = COMPONENTS.index(manifest["cells"][cell]["surface_to_deep"][depth])
                witnesses[cell].setdefault(depth, []).append({"line": number, "tool": tool})
                if tool != expected:
                    conflicts.append({"cell": cell, "depth": depth, "expected_tool": expected, "actual_tool": tool, "line": number})
    missing = [{"cell": cell, "depth": depth} for cell in range(2) for depth in range(19) if depth not in witnesses[cell]]
    report = {"scope": "interior path witnesses, not full-volume or physical-colour certification",
              "passed": not missing and not conflicts and not interval_conflicts,
              "witnesses": witnesses, "missing": missing, "conflicts": conflicts, "interval_conflicts": interval_conflicts,
              "printing_enabled": False}
    write_json(out / "path-binding-report.json", report)
    return report["passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--slice-with", type=Path)
    parser.add_argument("--inspect-existing", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    if args.inspect_existing:
        return 0 if inspect_gcode(out) else 1
    prepare(out)
    if args.slice_with:
        command = [str(args.slice_with.resolve()), "--datadir", str(out / "isolated-data"), "--debug", "3",
            "--load-settings", str(out / "machine.json") + ";" + str(out / "process.json"),
            "--load-filaments", ";".join(str(out / (name + ".json")) for name in COMPONENTS),
            "--arrange", "0", "--orient", "0", "--slice", "0", "--outputdir", str(out), str(out / "top-stack-fixture.3mf")]
        write_json(out / "slice-command.json", command)
        with (out / "slice.stdout.log").open("w") as stdout, (out / "slice.stderr.log").open("w") as stderr:
            result = subprocess.run(command, cwd=out, stdout=stdout, stderr=stderr, timeout=600, check=False)
        if result.returncode:
            return result.returncode
        return 0 if inspect_gcode(out) else 1
    print(out / "top-stack-fixture.3mf")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
