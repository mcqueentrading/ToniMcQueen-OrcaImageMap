#!/usr/bin/env python3
"""Isolated four-tool wall-path smoke, not optical or volume certification."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("top_fixture", ROOT / "scripts/surface-mix-top-stack-fixture.py")
top = importlib.util.module_from_spec(spec)
spec.loader.exec_module(top)


def prepare(out, invalid_slot, with_top_stacks):
    top.prepare(out)
    source = ROOT / "resources/surfacemix/creator5-cmyw-side-offsets.v1.json"
    catalog = json.loads(source.read_text())
    records = catalog["sources"][0]["records"]
    selected = [records[0], max(records, key=lambda r: sum(
        (a-b)**2 for a, b in zip(r["recipe"]["amplitudes_by_component"], records[0]["recipe"]["amplitudes_by_component"])))]
    colours = [[round(c*255) for c in r["measured_rgb"]["channels"]] for r in selected]
    profile = dict(catalog["sources"][0]["raw_document"])
    profile["surfacemix_side_offsets"] = {
        "schema": "surfacemix.imagemap-side-offsets.v1", "enabled": True, "experimental": True,
        "execution_model": "imagemap_independent_offsets", "assume_normalized_srgb": True,
        "canonical_to_filament_slot": [1, 1 if invalid_slot else 2, 3, 4], "catalogue": catalog,
    }
    process = json.loads((out / "process.json").read_text())
    zone = json.loads(process["texture_mapping_definitions"])[0]
    top_options = dict(zone["texture_options"])
    zone["texture_options"] = {
        "mode": "target_color", "color_model": "cmyw", "force_sequential_filaments": True,
        "top_surface_image_printing_enabled": False,
        "transmission_distance_calibration_mode": 3, "tone_gamma": 1.0,
        "dithering_enabled": False, "compact_offset_mode": False,
        "side_surface_color_calibration": profile,
    }
    process.update({"texture_mapping_definitions": json.dumps([zone]), "wall_filament": "99",
        "wall_loops": "3", "top_shell_layers": "3", "top_shell_thickness": "0",
        "texture_mapping_outer_wall_gradient_global_strength": "100",
        "texture_mapping_outer_wall_gradient_max_line_width": "0.95",
        "texture_mapping_outer_wall_gradient_min_line_width": "0.32"})
    if with_top_stacks:
        zone["texture_options"].update(top_options)
        process["texture_mapping_definitions"] = json.dumps([zone])
        process["top_shell_layers"] = "19"
        process["top_shell_thickness"] = "1.52"
        top_records = json.loads((ROOT / "resources/surfacemix/creator5-cmyw-top-preview.v1.json").read_text())["sources"][0]["records"]
        top_labs = [top.lab(r["measured_rgb"]["channels"]) for r in top_records]
        manifest = json.loads((out/"fixture-manifest.json").read_text())
        for i, rgb in enumerate(colours):
            target = top.lab([c/255 for c in rgb])
            nearest = min(range(len(top_records)), key=lambda j: sum((a-b)**2 for a,b in zip(target,top_labs[j])))
            r = top_records[nearest]
            manifest["cells"][i].update({"sample_id":r["sample_id"], "source_pointer":r["source_pointer"],
                "target_rgb8":rgb, "surface_to_deep":r["recipe"]["surface_to_deep"],
                "layer_heights_mm":r["recipe"]["layer_heights_mm"]})
        top.write_json(out/"fixture-manifest.json",manifest)
    top.write_json(out / "process.json", process)
    archive = out / "top-stack-fixture.3mf"
    with zipfile.ZipFile(archive) as z:
        entries = {name: z.read(name) for name in z.namelist()}
    entries["3D/Textures/targets.png"] = top.png_texture(colours)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries.items():
            info = zipfile.ZipInfo(name, date_time=(1980,1,1,0,0,0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    top.write_json(out / "side-fixture-manifest.json", {
        "scope": "EXPERIMENTAL SIDE WALL FIXTURE - DO NOT PRINT", "printing_enabled": False,
        "physical_colour_validated": False, "historical_execution_verified": False,
        "catalogue_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "catalogue_records": len(records), "invalid_slot_control": invalid_slot,
        "with_top_stacks":with_top_stacks,
        "cells": [{"source_pointer": r["source_pointer"], "sample_id": r["sample_id"], "target_rgb8": rgb,
            "recipe": r["recipe"]} for r,rgb in zip(selected,colours)]})


def inspect(out):
    tool = None
    feature = ""
    x = y = e = 0.
    relative = True
    moves = [[], []]
    for line in (out / "plate_1.gcode").read_text().splitlines():
        if line.startswith(";TYPE:"): feature = line[6:]
        if re.fullmatch(r"T\d+", line): tool = int(line[1:])
        code = line.split(";",1)[0].strip()
        if code == "M82": relative = False
        if code == "M83": relative = True
        values = {k:float(v) for k,v in re.findall(r"([XYE])([-+]?(?:\d+(?:\.\d*)?|\.\d+))", code)}
        if code.startswith("G92"): e = values.get("E",e)
        if not re.match(r"G[01](?:\s|$)", code): continue
        old_x, old_y = x,y
        x,y = values.get("X",x), values.get("Y",y)
        amount = values.get("E",0.) if relative else values.get("E",e)-e
        if "E" in values: e = e+values["E"] if relative else values["E"]
        if amount <= 0 or feature != "Outer wall": continue
        # Disjoint nearest-block ownership: the two margin boxes must not credit the same path twice.
        midpoint_x = (old_x+x)/2
        cell = 0 if midpoint_x < 113 else 1
        left = (100,114)[cell]
        if left-2 < midpoint_x < left+14 and 98 < (old_y+y)/2 < 110:
            moves[cell].append({"tool":tool,"start":[round(old_x-left,5),round(old_y-100,5)],
                "end":[round(x-left,5),round(y-100,5)]})
    tools = [sorted(set(m["tool"] for m in cell if m["tool"] is not None)) for cell in moves]
    passed = all(cell for cell in moves) and tools == [[0,1,2,3],[0,1,2,3]] and all(
        m["tool"] is not None for cell in moves for m in cell)
    top.write_json(out / "side-wall-path-report.json", {"passed":bool(passed),
        "scope":"wall-path/tool smoke only, not recipe coverage, exact optical execution or inward depth",
        "outer_wall_moves_per_cell":[len(c) for c in moves], "tools_per_cell":tools,
        "normalized_wall_path_sha256":[hashlib.sha256(json.dumps(c,sort_keys=True).encode()).hexdigest() for c in moves],
        "printing_enabled":False})
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--slice-with",type=Path,required=True)
    parser.add_argument("--invalid-slot",action="store_true")
    parser.add_argument("--with-top-stacks",action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    prepare(out,args.invalid_slot,args.with_top_stacks)
    command = [str(args.slice_with.resolve()),"--datadir",str(out/"isolated-data"),"--debug","3",
        "--load-settings",str(out/"machine.json")+";"+str(out/"process.json"),
        "--load-filaments",";".join(str(out/(name+".json")) for name in top.COMPONENTS),
        "--arrange","0","--orient","0","--slice","0","--outputdir",str(out),str(out/"top-stack-fixture.3mf")]
    top.write_json(out/"slice-command.json",command)
    with (out/"slice.stdout.log").open("w") as stdout, (out/"slice.stderr.log").open("w") as stderr:
        result = subprocess.run(command,cwd=out,stdout=stdout,stderr=stderr,timeout=600,check=False)
    if args.invalid_slot:
        text = (out/"slice.stdout.log").read_text()+(out/"slice.stderr.log").read_text()
        passed = result.returncode != 0 and "SurfaceMix side offsets" in text and not (out/"plate_1.gcode").exists()
        top.write_json(out/"rejection-report.json",{"passed":passed,"exit_code":result.returncode,"printing_enabled":False})
        return 0 if passed else 1
    if result.returncode: return result.returncode
    wall_passed = inspect(out)
    top_passed = top.inspect_gcode(out) if args.with_top_stacks else True
    return 0 if wall_passed and top_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
