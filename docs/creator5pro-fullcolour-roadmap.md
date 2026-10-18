# Creator 5 Pro ImageMap Full-Colour Roadmap

Date: 2026-09-30

Branch: `creator5pro-imagemap-fullcolour-tuning`

Base repo: `sentientstardust-dev/OrcaSlicer-ImageMap`

Printer target: FlashForge Creator 5 Pro running the local Reforge full-colour fork.

## Why This Fork Exists

The ImageMap workflow is now part of the Creator 5 Pro full-colour pipeline.
We need repeatable settings for CMYK/CMYW filament mapping, calibration-card
feedback, prime tower behaviour, and Reforge-safe G-code output.

## Current Local Workflow

1. Generate or import textured model.
2. Use ImageMap to preserve texture/RGBA data instead of hard nearest-colour triangle assignment.
3. Use Creator 5 Pro Reforge profiles from:

```text
/home/unknown/Desktop/Documents/3dprints/reforge_firmware_repo/local/creator5pro-orca-imagemap-profiles
```

4. For full-colour prints, use the explicit all-tool profile:

```text
Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe
```

5. If the printer IP changes, update the ImageMap app profile/cache with:

```bash
PRINTER_IP=100.65.18.84 bash /home/unknown/Documents/scripts/projectcampaign/OrcaSlicer-ImageMap/scripts/reforge_creator5pro_profiles.sh
```

6. Print the ImageMap calibration target, photograph it, and use the calibration tool to tune the filament gamut.

Calibration tool checkout:

```text
/home/unknown/Documents/scripts/projectcampaign/ImageMap-Calibration-Tool
```

Installed release copy:

```text
/home/unknown/Desktop/Documents/3dprints/imagemap_calibration/imagemap-calibration-tool-1.0.0
```

## Fork Goals

- Keep a Creator 5 Pro / Reforge profile set in sync with the firmware fork.
- Keep ImageMap's separate `~/.local/share/orcaslicer-imagemap-data` profile cache in sync with the same Reforge profile set.
- Add or document defaults that avoid accidental FlashForge-stock profile use.
- Improve CMYK/CMYW filament gamut setup from calibration photos.
- Make prime tower and brim settings obvious for full-colour textured prints.
- Preserve texture/RGBA data reliably for OBJ/MTL/PNG and glTF/GLB inputs.
- Avoid profile output that cools participating tools to `0 C` during active multi-tool prints.
- Keep generated G-code compatible with Reforge `START_PRINT` and toolchanger macros.

## Near-Term Work

1. Keep `scripts/reforge_creator5pro_profiles.sh` as the single entry point for ImageMap profile/IP updates.
2. Print ImageMap calibration square/card.
3. Photograph under controlled light.
4. Generate/update filament gamut calibration.
5. Compare calibrated output against the current Theo skin-tone issue.
6. Identify whether missing colour accuracy belongs in ImageMap solver settings, calibration data, slicer profile, or Reforge purge/temperature behaviour.

## ImageMap Profile Installer

ImageMap Orca does not use the normal Orca-Flashforge config path. Its active user profiles and selected printer cache live here:

```text
/home/unknown/.local/share/orcaslicer-imagemap-data
```

The installer copies the canonical Reforge profiles from:

```text
/home/unknown/Desktop/Documents/3dprints/reforge_firmware_repo/local/creator5pro-orca-imagemap-profiles
```

It then patches:

- Machine `print_host` to `http://<PRINTER_IP>:7125`.
- Machine `print_host_webui` to `http://<PRINTER_IP>:81/`.
- Selected printer to `Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe`.
- Process to `0.08mm Reforge ImageMap FullColour @C5P`.
- Filaments to CMYW: Cyan, Magenta, Yellow, White.
- ImageMap `dev_map` to remove the old `192.168.0.112:7125` entry.

Use:

```bash
PRINTER_IP=100.65.18.84 bash /home/unknown/Documents/scripts/projectcampaign/OrcaSlicer-ImageMap/scripts/reforge_creator5pro_profiles.sh
```

The installer creates timestamped backups under:

```text
/home/unknown/.local/share/orcaslicer-imagemap-data/reforge-profile-backups
```

## Related Local Fork

Reforge firmware fork:

```text
/home/unknown/Desktop/Documents/3dprints/reforge_firmware_repo
```

Current Reforge branch:

```text
creator5pro-local-fullcolour-tuning
```
