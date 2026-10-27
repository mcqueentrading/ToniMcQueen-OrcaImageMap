# Superseded: Preliminary ImageMap and SurfaceMix Notes

This preliminary note is superseded by `IMAGEMAP_SURFACEMIX_DEEP_DIVE_20261024.md`.
Its early claims about mapping correctness and the integration boundary were
not validated. Use the replacement report and its source-backed probe evidence.

Date: 2026-10-24

## Executive Finding

ImageMap already has the model-side data and a reusable sampling path: it loads
the OBJ texture/UV source, keeps triangle-aligned RGBA data, and projects the
texture into slice-space samples. The screenshots do **not** prove that the
projection is correct over every curved, side, underside, or occluded surface.
The observed failure therefore has two possible boundaries that must be
measured separately:

1. texture/UV/projection behaviour over the full 3D surface; and
2. conversion of sampled colour into calibrated CMYW recipes, stack ordering,
   transmission assumptions, and perimeter/top-surface execution.

We should not replace ImageMap's UV mapping. SurfaceMix should consume its
surface samples and provide a better recipe/execution decision layer.

## Evidence From the Screenshots

The Carnival ImageMap session shows:

- CMYW zone `F1/F2/F3/F4` selected.
- Image Texture surface pattern.
- All four CMYW filaments selected.
- `Preview Result Colors` enabled.
- ImageMap colour data contains a 4096 x 4096 image texture.
- RGBA data is present for approximately 1.89 million triangles.
- Vertex colour data is also present.
- Top calibration and side calibration profiles are loaded.
- Side calibration mode is `Calibrated: nearest measured sample`.
- `Trust color order for filaments` is enabled.
- Top-surface colouring is present but disabled in the shown state.
- Dithering is present as an option but disabled in the shown state.
- Visible line-width settings are approximately 0.42 mm outer/default and
  0.45 mm inner wall, with a 0.08 mm layer-height profile.

The model preview retains plausible texture placement from the displayed view.
This proves that the texture asset and at least one projection path are loaded;
it does not prove that UV/image projection is correct over the entire mesh.

## Existing Data Path

The active fork already contains the relevant implementation:

| Concern | Existing source |
|---|---|
| Zone/settings and calibration fields | `src/libslic3r/TextureMapping.hpp/.cpp` |
| Texture and UV sampling | `src/libslic3r/TextureMappingOffset.cpp` |
| Top-surface contoning and ordered stacks | `src/libslic3r/TextureMappingContoning.cpp` |
| Side/perimeter offsets and dithering | `src/libslic3r/TextureMappingOffset.cpp` |
| Raw top/side atlas decoding | `src/libslic3r/ImageMapRawFilamentOffsetAtlas.cpp/.hpp` |
| Texture/vertex/face colour ownership | `src/libslic3r/ModelTextureDataRemap.cpp/.hpp` |
| Architecture contract | `../OrcaSlicer-FullSpectrum-reference/doc/FullSpectrumImageMapArchitecture.md` |

## What ImageMap Already Solves

### 1. Texture-to-surface mapping data and path

`build_texture_mapping_offset_weight_field()` checks for valid imported RGBA,
UV validity and per-face UV records. It samples the texture bilinearly from UV
coordinates and preserves the sample colour before recipe selection.

This is the part SurfaceMix should audit and reuse rather than duplicate. It
still needs a full-surface validation, including curved and side-facing regions,
before we call it authoritative.

### 2. Triangle/layer sampling

The sampler creates physical XY samples from mesh triangles, uses layer height
and Z falloff, and can increase sampling density for high-resolution or
dithered operation. The output is a weighted sample field rather than a
single colour per object.

### 3. Raw calibrated payloads

The raw atlas decoder can preserve:

- top-surface layer labels;
- per-layer depths;
- filament slot mappings;
- side offset channels;
- projection masks;
- calibration metadata.

This is close to the SurfaceMix separation between a top-surface field and a
depth/side plan.

### 4. Top-surface and side paths are already separate

The code distinguishes flat/top-surface sampling from side sampling. Raw
top-surface labels can be sampled from a UV atlas, while side offsets are
handled through the perimeter/offset path.

## Where the Colour Can Go Wrong

### 0. Projection can be correct from one angle and wrong elsewhere

The screenshot's visible preview is not a proof of global correctness. A model
can show the expected texture from the front while still having errors caused
by UV seams, per-face UV indexing, back/side projection, occlusion handling,
coordinate transforms, texture orientation, or layer-space projection. Those
errors would feed wrong RGBA targets into an otherwise correct colour solver.

The deep dive must therefore compare, for the same sampled surface points:

- source UV and bilinear texture sample;
- triangle/face identity and corner UV indices;
- transformed 3D position and normal;
- projected slice-space position;
- visible/occluded surface classification;
- resulting RGBA target.

Only after those agree can a colour error be assigned to recipe selection or
execution.

### A. Generic colour solver versus measured recipe

The normal path builds solver candidates from filament colours and generic
mixing rules. That can produce a visually plausible preview while selecting a
recipe that does not match the measured CMYW calibration.

SurfaceMix should make the calibrated recipe ID and component sequence explicit
before any generic fallback is allowed.

### B. Nearest-measured mode can fall back

The nearest-measured contoning model requires the actual stack depth and each
layer height to match the measured calibration. If stack count, visible depth,
or layer heights do not match, the implementation records a fallback and can
use free-alpha or depth-kernel models instead.

That fallback is safe as a diagnostic, but it may explain why a print has the
right texture and the wrong colour. SurfaceMix must expose this as a visible
status rather than silently treating the fallback as calibrated execution.

### C. Dithering is not always active

The binary dither path is disabled when calibrated side candidates are present,
when raw values are active, or when the selected method is a halftone mode.
The screenshots also show dithering disabled in the UI.

Therefore “calibration loaded” does not mean “lateral CMYW alternation was
executed.” The output needs separate flags for:

- top recipe selected;
- side recipe selected;
- lateral striation enabled;
- actual path-level alternation emitted.

### D. Side colour is an offset/path operation, not a complete wall-volume plan

The current side path builds a weighted field for XY layer samples and applies
component-dependent perimeter/line-width modulation. It does not by itself
prove that a local surface has 1.26 mm of usable inward material, nor does it
reserve a complete ordered volumetric stack against cavities or opposing
surfaces.

That is the exact place for `WallDepthEligibilityMap` and the adaptive
SurfaceMix recipe field.

## Correct Integration Boundary

```text
ImageMap OBJ + UV + texture source
        |
        v
ImageMap SurfaceSampler / RGBA samples
        |
        v
SurfaceMix local recipe decision
  - top-surface stack
  - lateral striation/dithering
  - wall-depth eligibility
  - reduced recipe for thin regions
        |
        v
ImageMap/Orca path and perimeter execution
        |
        v
coverage audit and physical print validation
```

The existing ImageMap sampler remains authoritative for source colour and
texture placement. SurfaceMix becomes authoritative only for the calibrated
recipe decision and its eligibility/status metadata.

## Proposed SurfaceMix Outputs

For each surface cell or path region, emit:

- source UV/RGBA sample;
- selected calibrated top recipe ID;
- ordered top stack component IDs and layer heights;
- local available inward depth;
- wall eligibility: `rich_wall`, `reduced_wall`, `top_only`, or `unknown`;
- lateral striation pattern and spacing, if enabled;
- backing/opposing reservation status;
- path coverage status;
- fallback reason, if any.

The top-surface field should remain usable even when the wall-depth field is
unknown. An unresolved internal defect should not block top colour unless it
affects an exterior sampled region or a real inward path.

## First Safe Implementation Pass

Do not change the existing ImageMap mapping or enable automatic assignment.

1. Add an observation-only export of ImageMap's sampled surface field.
2. Record whether each sample came from RGBA texture, raw top labels, raw side
   offsets, generic solver, or calibrated fallback.
3. Add SurfaceMix's `TopSurfaceColourField` over those samples.
4. Add `WallDepthEligibilityMap` as a separate diagnostic overlay.
5. Compare the existing ImageMap output against the SurfaceMix decision without
   changing G-code.
6. Only then add a controlled execution adapter for regions whose recipe and
   path coverage are both verified.

## Important Non-Goals

- Do not extract code from the unrelated `rea` checkout.
- Do not rewrite ImageMap UV mapping.
- Do not repair or rewrite the original OBJ for this colour pass.
- Do not treat a preview colour as physical colour validation.
- Do not silently accept nearest-measured fallback as calibrated output.
- Do not enable dithering merely because a dithering control exists.

## Current Conclusion

The project should continue as an ImageMap extension, not a new slicer or a
new texture-mapping implementation. ImageMap already supplies the correct
surface source and much of the execution scaffolding. SurfaceMix should add
local calibrated recipe selection, explicit top/depth/striation separation,
wall-depth eligibility, and auditable fallback/status reporting.
