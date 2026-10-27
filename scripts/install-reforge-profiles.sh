#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_root="${repo_root}/resources/profiles/Reforge"
data_root="${TONI_DATA_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/OrcaSlicer-ImageMap}"
custom_root="${data_root}/system/Custom"
manifest="${custom_root}/../Custom.json"

[[ -d "$source_root" ]] || { printf 'Missing source profile directory: %s\n' "$source_root" >&2; exit 1; }
[[ -f "$manifest" ]] || { printf 'Missing active profile manifest: %s\n' "$manifest" >&2; exit 1; }

stamp="$(date +%Y%m%d-%H%M%S)"
backup_dir="${data_root}/profile-backups/reforge-${stamp}"
mkdir -p "$backup_dir" "$custom_root/machine" "$custom_root/process" "$custom_root/filament"
cp -a "$manifest" "$backup_dir/Custom.json"

model_source="$source_root/machine/Flashforge Creator 5 Pro.json"
model_target="$custom_root/machine/Flashforge Creator 5 Pro.json"
cp -f "$model_source" "$model_target"
model_name="$(jq -er '.name' "$model_source")"
tmp="$(mktemp "${manifest}.XXXXXX")"
jq --arg name "$model_name" --arg sub_path "machine/Flashforge Creator 5 Pro.json" '
    .machine_model_list = ((.machine_model_list // [])
        | if any(.[]; .name == $name)
          then .
          else . + [{"name": $name, "sub_path": $sub_path}]
          end)
' "$manifest" > "$tmp"
mv "$tmp" "$manifest"

for kind in machine process filament; do
    while IFS= read -r -d '' source_file; do
        [[ "$kind" != machine || "$(basename "$source_file")" != "Flashforge Creator 5 Pro.json" ]] || continue
        target_file="${custom_root}/${kind}/$(basename "$source_file")"
        if [[ -e "$target_file" ]]; then
            cp -a "$target_file" "${backup_dir}/$(basename "$target_file")" 2>/dev/null || true
        fi
        cp -f "$source_file" "$target_file"
        name="$(jq -er '.name' "$source_file")"
        sub_path="${kind}/$(basename "$source_file")"
        tmp="$(mktemp "${manifest}.XXXXXX")"
        jq --arg section "${kind}_list" --arg name "$name" --arg sub_path "$sub_path" '
            .[$section] = ((.[$section] // [])
                | if any(.[]; .name == $name)
                  then .
                  else . + [{"name": $name, "sub_path": $sub_path}]
                  end)
        ' "$manifest" > "$tmp"
        mv "$tmp" "$manifest"
    done < <(find "$source_root/$kind" -maxdepth 1 -type f -name '*.json' -print0 | sort -z)
done

jq empty "$manifest"
printf 'Installed Reforge profiles into %s\n' "$custom_root"
printf 'Manifest backup: %s\n' "$backup_dir/Custom.json"
printf 'Normal default remains unchanged; SAFE ALLOW is registered but not selected.\n'
