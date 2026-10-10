#!/usr/bin/env bash
set -euo pipefail

# Installs the local Reforge Creator 5 Pro / ImageMap profile set into the
# ImageMap-specific Orca data directory. Override PRINTER_IP when DHCP changes:
#
#   PRINTER_IP=192.168.0.113 bash scripts/install_reforge_creator5pro_profiles.sh

PRINTER_IP="${PRINTER_IP:-192.168.0.113}"
MOONRAKER_URL="${MOONRAKER_URL:-http://${PRINTER_IP}:7125}"
WEBUI_URL="${WEBUI_URL:-http://${PRINTER_IP}:81/}"

DATA_DIR="${DATA_DIR:-$HOME/.local/share/orcaslicer-imagemap-data}"
SOURCE_DIR="${SOURCE_DIR:-$HOME/Desktop/Documents/3dprints/reforge_firmware_repo/local/creator5pro-orca-imagemap-profiles}"

MACHINE_DIR="$DATA_DIR/user/default/machine"
MACHINE_BASE_DIR="$DATA_DIR/user/default/machine/base"
PROCESS_DIR="$DATA_DIR/user/default/process"
FILAMENT_DIR="$DATA_DIR/user/default/filament"
CONF="$DATA_DIR/OrcaSlicer.conf"

SELECTED_MACHINE="Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe"
SELECTED_PROCESS="0.08mm Reforge ImageMap FullColour @C5P"

FILAMENTS=(
  "Elegoo PLA Cyan @Reforge C5P"
  "Elegoo PLA Magenta @Reforge C5P"
  "Elegoo PLA Yellow @Reforge C5P"
  "Elegoo PLA White @Reforge C5P"
)

MACHINES=(
  "Reforge Creator 5 Pro 0.4 nozzle.json"
  "Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe.json"
  "Reforge Creator 5 Pro 0.4 nozzle WarmTools 120C.json"
)

PROCESSES=(
  "0.08mm Reforge ImageMap FullColour @C5P.json"
  "0.20mm Reforge First Test @C5P.json"
  "0.20mm Reforge Quality Snapshot @C5P.json"
)

ALL_FILAMENT_FILES=(
  "Elegoo PLA @Reforge C5P.json"
  "Elegoo PLA Black K @Reforge C5P.json"
  "Elegoo PLA Cyan @Reforge C5P.json"
  "Elegoo PLA Magenta @Reforge C5P.json"
  "Elegoo PLA White @Reforge C5P.json"
  "Elegoo PLA Yellow @Reforge C5P.json"
)

timestamp() {
  date +%Y%m%d_%H%M%S
}

write_info() {
  local path="$1"
  cat > "$path" <<INFO
sync_info = create
user_id =
setting_id =
base_id =
updated_time = $(date +%s)
INFO
}

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "Missing required file: $path" >&2
    exit 1
  fi
}

patch_machine_json() {
  local file="$1"
  local display_name="$2"
  local tmp="${file}.tmp"

  jq \
    --arg name "$display_name" \
    --arg host "$MOONRAKER_URL" \
    --arg webui "$WEBUI_URL" \
    --arg process "$SELECTED_PROCESS" '
      .name = $name
      | .printer_settings_id = $name
      | .print_host = $host
      | .print_host_webui = $webui
      | .machine_tool_change_time = (.machine_tool_change_time | tostring)
      | .default_print_profile = $process
      | .filament_presets = [
          "Elegoo PLA Cyan @Reforge C5P",
          "Elegoo PLA Magenta @Reforge C5P",
          "Elegoo PLA Yellow @Reforge C5P",
          "Elegoo PLA White @Reforge C5P"
        ]
      | .layer_change_gcode = ";AFTER_LAYER_CHANGE\n;[layer_z]\nREFORGE_SNAPSHOT_EVERY_MINUTE INTERVAL=60"
    ' "$file" > "$tmp"
  jq empty "$tmp"
  mv "$tmp" "$file"
}

patch_process_json() {
  local file="$1"
  local tmp="${file}.tmp"

  jq '
    .compatible_printers = ["Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe"]
    | .compatible_printers_condition = ""
  ' "$file" > "$tmp"
  jq empty "$tmp"
  mv "$tmp" "$file"
}

patch_filament_json() {
  local file="$1"
  local tmp="${file}.tmp"

  jq '
    .compatible_printers = ["Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe"]
    | .compatible_printers_condition = ""
  ' "$file" > "$tmp"
  jq empty "$tmp"
  mv "$tmp" "$file"
}

echo "Installing Reforge Creator 5 Pro profiles into ImageMap Orca"
echo "Printer IP:   $PRINTER_IP"
echo "Moonraker:    $MOONRAKER_URL"
echo "Web UI:       $WEBUI_URL"
echo "Data dir:     $DATA_DIR"
echo "Source dir:   $SOURCE_DIR"

require_file "$CONF"
for machine in "${MACHINES[@]}"; do
  require_file "$SOURCE_DIR/$machine"
done
for process in "${PROCESSES[@]}"; do
  require_file "$SOURCE_DIR/$process"
done
for filament in "${ALL_FILAMENT_FILES[@]}"; do
  require_file "$SOURCE_DIR/$filament"
done

stamp="$(timestamp)"
backup_dir="$DATA_DIR/reforge-profile-backups/$stamp"
mkdir -p "$backup_dir"
mkdir -p "$MACHINE_DIR" "$MACHINE_BASE_DIR" "$PROCESS_DIR" "$FILAMENT_DIR"

cp "$CONF" "$backup_dir/OrcaSlicer.conf"
for dir in "$MACHINE_DIR" "$MACHINE_BASE_DIR" "$PROCESS_DIR" "$FILAMENT_DIR"; do
  if [[ -d "$dir" ]]; then
    mkdir -p "$backup_dir/${dir#$DATA_DIR/}"
    find "$dir" -maxdepth 1 -type f \( -name '*Reforge*.json' -o -name '*Reforge*.info' -o -name '*Elegoo*.json' -o -name '*Elegoo*.info' \) \
      -exec cp -p {} "$backup_dir/${dir#$DATA_DIR/}/" \;
  fi
done

for machine in "${MACHINES[@]}"; do
  src="$SOURCE_DIR/$machine"
  display="${machine%.json}"
  cp "$src" "$MACHINE_DIR/$machine"
  cp "$src" "$MACHINE_BASE_DIR/$machine"
  patch_machine_json "$MACHINE_DIR/$machine" "$display"
  patch_machine_json "$MACHINE_BASE_DIR/$machine" "$display"
  write_info "$MACHINE_DIR/${machine%.json}.info"
  write_info "$MACHINE_BASE_DIR/${machine%.json}.info"
done

for process in "${PROCESSES[@]}"; do
  cp "$SOURCE_DIR/$process" "$PROCESS_DIR/$process"
  patch_process_json "$PROCESS_DIR/$process"
  write_info "$PROCESS_DIR/${process%.json}.info"
done

for filament in "${ALL_FILAMENT_FILES[@]}"; do
  cp "$SOURCE_DIR/$filament" "$FILAMENT_DIR/$filament"
  patch_filament_json "$FILAMENT_DIR/$filament"
  write_info "$FILAMENT_DIR/${filament%.json}.info"
done

conf_tmp="$CONF.tmp"
jq \
  --arg selected_machine "$SELECTED_MACHINE" \
  --arg selected_process "$SELECTED_PROCESS" \
  --arg old_dev "192.168.0.112:7125" \
  --arg new_dev "${PRINTER_IP}:7125" '
    .dev_map = (((.dev_map // {}) | del(.[$old_dev])) + {($new_dev): {"dev_ip": $new_dev, "dev_name": $new_dev}})
    | .local_machines = (((.local_machines // {}) | del(.[$old_dev])) + {($new_dev): {"dev_ip": $new_dev, "dev_name": $new_dev, "printer_type": "Flashforge Creator 5 Pro"}})
    | .models = (((.models // []) | map(select(.model != "Reforge Creator 5 Pro 0.4 nozzle"))) + [
        {"model":"Reforge Creator 5 Pro 0.4 nozzle","nozzle_diameter":"0.4","vendor":"Flashforge"}
      ])
    | .presets.machine = $selected_machine
    | .presets.filaments = [
        "Elegoo PLA Cyan @Reforge C5P",
        "Elegoo PLA Magenta @Reforge C5P",
        "Elegoo PLA Yellow @Reforge C5P",
        "Elegoo PLA White @Reforge C5P"
      ]
    | .orca_presets = (
        ((.orca_presets // []) | map(select(.machine != $selected_machine))) + [{
          "curr_bed_type":"4",
          "extruder_ams_count":"1#0|4#0,1#0|4#0",
          "filament":"Elegoo PLA Cyan @Reforge C5P",
          "filament_01":"Elegoo PLA Cyan @Reforge C5P",
          "filament_02":"Elegoo PLA Magenta @Reforge C5P",
          "filament_03":"Elegoo PLA Yellow @Reforge C5P",
          "filament_04":"Elegoo PLA White @Reforge C5P",
          "filament_color_types":"1,1,1,1",
          "filament_colors":"#00AEEF,#EC008C,#FFF200,#FFFFFF",
          "filament_multi_colors":"#00AEEF,#EC008C,#FFF200,#FFFFFF",
          "flush_multiplier":"0.900000|0.900000|0.900000|0.900000",
          "flush_volumes_matrix":"0.000000|280.000000|280.000000|280.000000|280.000000|0.000000|280.000000|280.000000|280.000000|280.000000|0.000000|280.000000|280.000000|280.000000|280.000000|0.000000",
          "flush_volumes_vector":"140.000000|140.000000|140.000000|140.000000|140.000000|140.000000|140.000000|140.000000",
          "machine":$selected_machine,
          "process":$selected_process
        }]
      )
  ' "$CONF" > "$conf_tmp"
jq empty "$conf_tmp"
mv "$conf_tmp" "$CONF"

echo "Validating install"
rg -n '192\.168\.0\.112' "$MACHINE_DIR" "$MACHINE_BASE_DIR" "$PROCESS_DIR" "$FILAMENT_DIR" "$CONF" \
  --glob '!*.bak-*' --glob '!*.backup-*' && {
    echo "Stale 192.168.0.112 remains in active ImageMap profile/config files" >&2
    exit 1
  } || true

jq -e --arg selected "$SELECTED_MACHINE" '.presets.machine == $selected' "$CONF" >/dev/null
jq -e --arg host "$MOONRAKER_URL" '.print_host == $host' "$MACHINE_DIR/$SELECTED_MACHINE.json" >/dev/null
jq -e --arg webui "$WEBUI_URL" '.print_host_webui == $webui' "$MACHINE_DIR/$SELECTED_MACHINE.json" >/dev/null

echo "Done."
echo "Backup: $backup_dir"
echo "Selected ImageMap printer: $SELECTED_MACHINE"
