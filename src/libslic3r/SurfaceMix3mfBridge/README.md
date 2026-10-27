# SurfaceMix 3MF Bridge

This module is the removable adapter between the standalone
`tonimcqueen-surfacemix-quantizer` and ToniMcQueen-Slicer/ImageMap.

## Responsibility

The bridge does only four things when both quantizer metadata entries are
present in a 3MF:

1. Read `Metadata/tonimcqueen_surfacemix_recipe_report.json` and
   `Metadata/tonimcqueen_surfacemix_depth_stack_plan.json`.
2. Validate the three-layer, `0.24 mm` top execution contract.
3. Convert the selected measured top recipes into the existing SurfaceMix
   top-stack transport.
4. Store that transport in the imported ImageMap texture zone.

Orca/ImageMap still owns model loading, texture projection, slicing, purge
handling, tool changes, G-code generation, and preview. The bridge does not
modify mesh geometry, generate G-code, implement side-wall execution, or
enable dithering.

## Fallback behavior

- A normal 3MF without both metadata entries is unchanged.
- Malformed or incompatible metadata is rejected and logged; model loading
  continues without silently falling back to generic colour solving for the
  requested SurfaceMix metadata.
- Removing this directory, its two CMake source entries, and the one importer
  call cleanly removes the bridge without changing the ordinary ImageMap
  path.

## Current boundary

The imported transport is experimental and top-surface/build-Z only. The
quantizer metadata still records that physical colour, side-wall execution,
and G-code verification are not proven. This bridge therefore connects the
recipe data to the existing solver path; it is not a claim that arbitrary
curved surface-relative depth has been implemented.
