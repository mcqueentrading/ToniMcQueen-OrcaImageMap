#!/usr/bin/env python3
"""Verify an already-built host using isolated, explicitly non-printable fixtures."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROTECTED = {
    ROOT / "resources/surfacemix/creator5-cmyw-top-preview.v1.json":
        "b6725b0bd5e995ad4415b347c9b5944271e16a44cbc6964925878ccbffed6efa",
    Path("/home/unknown/Desktop/Documents/3dprints/carnival_dancer/obj_texture_maps/carnival+dancer+3d+model.obj"):
        "f9931017dfbdc8c4adae28772e7c2865f9767212aa5406dcd91c5fbeeff931d4",
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"scope":"shipping native and controlled fixture execution, not physical colour/full coverage",
        "started_utc":datetime.now(timezone.utc).isoformat(), "passed":False, "steps":[],
        "gui_verified":False, "print_validated":False, "upload_performed":False, "printing_enabled":False}
    binary = ROOT / "build-surface-mix/src/orca-slicer-imagemap"
    try:
        report["binary_sha256"] = sha256(binary)
        report["protected_hashes"] = {str(path):sha256(path) for path in PROTECTED}
        if any(report["protected_hashes"][str(path)] != expected for path,expected in PROTECTED.items()):
            raise RuntimeError("protected original or top catalogue differs from the accepted baseline")
        native = ROOT / "build-surface-recipe-importer" / out.name
        commands = [
            ("focused-build",["cmake","--build","build-surface-recipe-importer","-j2"]),
            ("focused-tests",["ctest","--test-dir","build-surface-recipe-importer","--output-on-failure"]),
            ("native",[sys.executable,"scripts/check-surface-mix-native.py","--out",str(native)]),
            ("combined",[sys.executable,"scripts/surface-mix-side-offset-fixture.py","--out",str(out/"combined"),
                "--slice-with",str(binary),"--with-top-stacks"]),
            ("invalid-slots",[sys.executable,"scripts/surface-mix-side-offset-fixture.py","--out",str(out/"invalid-slots"),
                "--slice-with",str(binary),"--invalid-slot"]),
        ]
        for name, command in commands:
            with (out/(name+".log")).open("w") as log:
                result = subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=900,check=False)
            report["steps"].append({"name":name,"command":command,"exit_code":result.returncode})
            if result.returncode:
                raise RuntimeError(name+" failed; see "+str(out/(name+".log")))
        report["native"] = json.loads((native/"result.json").read_text())
        report["wall_paths"] = json.loads((out/"combined/side-wall-path-report.json").read_text())
        top = json.loads((out/"combined/path-binding-report.json").read_text())
        report["top_paths"] = {"passed":top["passed"], "missing":len(top["missing"]),
            "conflicts":len(top["conflicts"]),"interval_conflicts":len(top["interval_conflicts"])}
        report["invalid_slots"] = json.loads((out/"invalid-slots/rejection-report.json").read_text())
        report["protected_unchanged"] = all(sha256(path)==expected for path,expected in PROTECTED.items())
        report["passed"] = (report["wall_paths"]["passed"] and top["passed"] and
            report["invalid_slots"]["passed"] and report["protected_unchanged"])
    except Exception as error:
        report["error"] = str(error)
    report["finished_utc"] = datetime.now(timezone.utc).isoformat()
    (out/"SHIPPING_VERIFICATION.json").write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    print(out/"SHIPPING_VERIFICATION.json",flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
