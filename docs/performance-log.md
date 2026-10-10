# ToniMcQueen ImageMap Performance Log

Date: 2026-10-10

Branch: `creator5pro-imagemap-fullcolour-tuning`

## Current Problem

Large textured models can make OrcaSlicer-ImageMap look frozen while only one CPU
core is pinned at 100 percent. Total CPU and RAM stay low because the hot path is
serial, not memory-starved.

Observed sample:

```text
/home/unknown/Desktop/Documents/3dprints/jacky/boobs/jacky.3mf
```

Measured source geometry:

```text
Compressed 3MF:        65 MB
Uncompressed model XML: ~570 MB
OBJ source:            196 MB
Vertices:              987,123
Triangles:             1,974,253
Texture sidecars:      basecolor, normal, roughness, metallic
```

Reduced test copies:

```text
/home/unknown/Desktop/Documents/3dprints/jacky/boobs/jacky_obj_simplified_900k/jacky_simplified_900k.obj
Triangles: 899,999
```

## Working Diagnosis

This is a single-thread bottleneck, not a general system load problem.

Likely hot paths:

- 3MF XML parsing in `src/libslic3r/Format/bbs_3mf.cpp`.
- OBJ/3MF mesh conversion into `indexed_triangle_set` and `TriangleMesh`.
- Texture payload restore and UV data assignment for ImageMap imports.
- Plate/G-code thumbnail rendering in `src/slic3r/GUI/GLCanvas3D.cpp` and
  `src/slic3r/GUI/Plater.cpp`.

The slicer already uses Intel TBB in many slicing stages. The observed one-core
spike means this workload has not reached those parallel slicing stages yet, or
is blocked in a GUI/render/import path that is still serial.

## Immediate Workarounds

- Simplify very large textured meshes before import.
- Prefer OBJ + MTL + basecolor texture for the current ImageMap workflow when
  the 3MF project wrapper is too slow.
- Disable or bypass thumbnail generation for giant textured imports.
- Keep original dense assets untouched and create named reduced copies.

## Patch Plan

1. Add timing logs around:
   - 3MF archive open/read.
   - XML model parse.
   - mesh conversion.
   - texture restore/decode.
   - plate thumbnail update.
   - slice start and export.
2. Add an ImageMap-safe environment flag to skip expensive thumbnail generation
   for giant textured projects.
3. Add a user-facing warning when importing very large textured models.
4. Only then parallelize safe post-parse conversion stages.

## Do Not Do First

- Do not blindly parallelize raw XML parsing.
- Do not rewrite the geometry kernel before we have timing data and regression
  tests.
- Do not overwrite known-good Creator 5 Pro FullSpectrum/ImageMap profiles.

## Longer-Term Architecture

Keep Orca's C++ slicer core for now, but move surrounding automation into
separate Rust actor services:

- printer/Moonraker API monitor
- upload and print job orchestration
- timelapse cataloguing
- ImageMap profile manager
- calibration runner
- model preflight/profiling sidecar

Rust should first wrap and supervise the workflow. Replacing C++ geometry code is
only sensible after we have repeatable tests proving identical generated output.
