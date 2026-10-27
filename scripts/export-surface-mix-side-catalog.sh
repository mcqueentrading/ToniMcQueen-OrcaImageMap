#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
    printf 'Usage: %s calibration-audit.json [output-catalog.json]\n' "$0" >&2
    exit 2
fi
project_dir="$(dirname -- "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")")"
destination="${2:-$project_dir/resources/surfacemix/creator5-cmyw-side-offsets.v1.json}"
mkdir -p -- "$(dirname -- "$destination")"
temporary="$(mktemp -- "${destination}.tmp.XXXXXX")"
trap 'rm -f -- "$temporary"' EXIT
jq -ce '
    if .report_schema_version != "surfacemix.calibration-audit.v1" or .audit_passed != true
       or .catalog.schema_version != "surfacemix.calibration-catalog.v1"
       or .catalog.executable_assignment_enabled != false
    then error("requires a passing inspection-only Rust calibration audit")
    else .catalog | .sources |= map(select(.role == "side"))
      | if (.sources | length) == 0 then error("audit contains no side source") else . end
    end
' "$1" > "$temporary"
mv -- "$temporary" "$destination"
trap - EXIT
sha256sum -- "$1" "$destination"
