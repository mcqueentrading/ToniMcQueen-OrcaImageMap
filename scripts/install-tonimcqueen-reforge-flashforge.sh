#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
reforge_root="$repo_root/resources/profiles/Reforge"
flashforge_root="$repo_root/resources/profiles/Flashforge"
source_model="$reforge_root/machine/Flashforge Creator 5 Pro.json"

model_name="ToniMcQueen Reforge"
safe_machine="ToniMcQueen Reforge 0.4 nozzle ImageMap FullColour Safe"
allow_machine="ToniMcQueen Reforge 0.4 nozzle ImageMap SAFE ALLOW"
safe_process="0.08mm ToniMcQueen Reforge ImageMap FullColour @C5P"
allow_process="0.08mm ToniMcQueen Reforge ImageMap SAFE ALLOW @C5P"
filament_names=(
    "Elegoo PLA @ToniMcQueen Reforge"
    "Elegoo PLA Black K @ToniMcQueen Reforge"
    "Elegoo PLA Cyan @ToniMcQueen Reforge"
    "Elegoo PLA Magenta @ToniMcQueen Reforge"
    "Elegoo PLA White @ToniMcQueen Reforge"
    "Elegoo PLA Yellow @ToniMcQueen Reforge"
)
filament_json="$(printf '%s\n' "${filament_names[@]}" | jq -R -s 'split("\n") | map(select(length > 0))')"

mkdir -p "$flashforge_root/machine" "$flashforge_root/process"
jq --arg name "$model_name" '.name = $name' "$source_model" > "$flashforge_root/machine/$model_name.json"

make_machine() {
    local source="$1" output="$2" name="$3" process="$4"
    jq --arg name "$name" --arg process "$process" --arg model "$model_name" --argjson filaments "$filament_json" '
        .name = $name
        | .printer_settings_id = $name
        | .printer_model = $model
        | .default_print_profile = $process
        | .default_filament_profile = $filaments
        | .filament_presets = $filaments
    ' "$source" > "$flashforge_root/machine/$output"
}

make_process() {
    local source="$1" output="$2" name="$3" printer="$4"
    jq --arg name "$name" --arg printer "$printer" '
        .name = $name
        | .print_settings_id = $name
        | .compatible_printers = (((.compatible_printers // []) + [$printer]) | unique)
    ' "$source" > "$flashforge_root/process/$output"
}

make_machine "$reforge_root/machine/Reforge Creator 5 Pro 0.4 nozzle ImageMap FullColour Safe.json" \
    "$safe_machine.json" "$safe_machine" "$safe_process"
make_machine "$reforge_root/machine/Reforge Creator 5 Pro 0.4 nozzle ImageMap SAFE ALLOW.json" \
    "$allow_machine.json" "$allow_machine" "$allow_process"
make_process "$reforge_root/process/0.08mm Reforge ImageMap FullColour @C5P.json" \
    "$safe_process.json" "$safe_process" "$safe_machine"
make_process "$reforge_root/process/0.08mm Reforge ImageMap FullColour SAFE ALLOW @C5P.json" \
    "$allow_process.json" "$allow_process" "$allow_machine"

for source_file in "$reforge_root"/filament/*.json; do
    base_name="$(basename "$source_file" .json)"
    alias_name="${base_name/@Reforge C5P/@ToniMcQueen Reforge}"
    jq --arg name "$alias_name" --arg printer "$safe_machine" '
        .name = $name
        | .filament_settings_id = $name
        | .compatible_printers = (((.compatible_printers // []) + [$printer]) | unique)
    ' "$source_file" > "$flashforge_root/filament/$alias_name.json"
done

append_entry() {
    local manifest="$1" section="$2" name="$3" sub_path="$4"
    local tmp
    tmp="$(mktemp "${manifest}.XXXXXX")"
    jq --arg section "$section" --arg name "$name" --arg sub_path "$sub_path" '
        .[$section] = ((.[$section] // [])
            | if any(.[]; .name == $name)
              then .
              else . + [{"name": $name, "sub_path": $sub_path}]
              end)
    ' "$manifest" > "$tmp"
    mv "$tmp" "$manifest"
}

append_entry "$repo_root/resources/profiles/Flashforge.json" machine_model_list "$model_name" "machine/$model_name.json"
append_entry "$repo_root/resources/profiles/Flashforge.json" machine_list "$safe_machine" "machine/$safe_machine.json"
append_entry "$repo_root/resources/profiles/Flashforge.json" machine_list "$allow_machine" "machine/$allow_machine.json"
append_entry "$repo_root/resources/profiles/Flashforge.json" process_list "$safe_process" "process/$safe_process.json"
append_entry "$repo_root/resources/profiles/Flashforge.json" process_list "$allow_process" "process/$allow_process.json"

data_root="${TONI_DATA_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/OrcaSlicer-ImageMap}"
custom_root="$data_root/system/Custom"
manifest="$custom_root/../Custom.json"
backup_dir="$data_root/profile-backups/tonimcqueen-reforge-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$backup_dir" "$custom_root/machine" "$custom_root/process" "$custom_root/filament"
cp -a "$manifest" "$backup_dir/Custom.json"
cp "$flashforge_root/machine/$model_name.json" "$custom_root/machine/$model_name.json"
cp "$flashforge_root/machine/$safe_machine.json" "$custom_root/machine/$safe_machine.json"
cp "$flashforge_root/machine/$allow_machine.json" "$custom_root/machine/$allow_machine.json"
cp "$flashforge_root/process/$safe_process.json" "$custom_root/process/$safe_process.json"
cp "$flashforge_root/process/$allow_process.json" "$custom_root/process/$allow_process.json"
for filament_name in "${filament_names[@]}"; do
    cp "$flashforge_root/filament/$filament_name.json" "$custom_root/filament/$filament_name.json"
    append_entry "$repo_root/resources/profiles/Flashforge.json" filament_list "$filament_name" "filament/$filament_name.json"
    append_entry "$manifest" filament_list "$filament_name" "filament/$filament_name.json"
done
append_entry "$manifest" machine_model_list "$model_name" "machine/$model_name.json"
append_entry "$manifest" machine_list "$safe_machine" "machine/$safe_machine.json"
append_entry "$manifest" machine_list "$allow_machine" "machine/$allow_machine.json"
append_entry "$manifest" process_list "$safe_process" "process/$safe_process.json"
append_entry "$manifest" process_list "$allow_process" "process/$allow_process.json"
jq empty "$repo_root/resources/profiles/Flashforge.json" "$manifest"
printf 'Registered %s under Flashforge.\n' "$model_name"
printf 'Normal preset: %s\n' "$safe_machine"
printf 'Explicit print-enabled preset: %s\n' "$allow_machine"
printf 'Active profile backup: %s\n' "$backup_dir/Custom.json"
