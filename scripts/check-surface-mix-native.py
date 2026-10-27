#!/usr/bin/env python3
"""Link the native check against the same libraries and flags as the slicer."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build-surface-mix")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--isolate-gui", action="store_true", help="compile the two changed GUI adapters independently while the full GUI builds")
    parser.add_argument("--core-override", action="append", default=[], help="independently compile a changed libslic3r source for isolated checks")
    args = parser.parse_args()
    build, out = args.build.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    definitions = dict(line.split(" = ", 1) for line in
        (build / "src/libslic3r/CMakeFiles/libslic3r.dir/flags.make").read_text().splitlines() if " = " in line)
    compile_args = ["c++"] + shlex.split(" ".join(definitions.get(k, "") for k in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS")))
    compile_args += ["-O0", "-c", str(ROOT / "tests/surface_recipe_field/native_top_stack_check.cpp"), "-o", str(out / "native-check.o")]
    link_args = shlex.split((build / "src/CMakeFiles/OrcaSlicer.dir/link.txt").read_text())
    main_index = next(i for i, part in enumerate(link_args) if part.endswith("OrcaSlicer.cpp.o"))
    link_args[main_index] = str(out / "native-check.o")
    link_args[link_args.index("-o") + 1] = str(out / "native-check")
    link_args = ["-Wl,--dependency-file=" + str(out / "link.d") if part.startswith("-Wl,--dependency-file=") else part for part in link_args]
    extra_commands = []
    for source in args.core_override:
        source_path = (ROOT / "src/libslic3r" / source).resolve()
        source_path.relative_to(ROOT / "src/libslic3r")
        name = source.replace("/", "_").replace(".cpp", "")
        command = ["c++"] + shlex.split(" ".join(definitions.get(k, "") for k in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS")))
        command += ["-include", str(build / "src/libslic3r/CMakeFiles/libslic3r.dir/cmake_pch.hxx"),
                    "-c", str(source_path), "-o", str(out / (name + ".o"))]
        extra_commands.append((name, command, ROOT))
        library = next(i for i, part in enumerate(link_args) if part.endswith("liblibslic3r.a"))
        link_args.insert(library, str(out / (name + ".o")))
    if args.isolate_gui:
        gui_flags = dict(line.split(" = ", 1) for line in
            (build / "src/slic3r/CMakeFiles/libslic3r_gui.dir/flags.make").read_text().splitlines() if " = " in line)
        for source in ("SurfaceMixRecipePreviewPanel", "SurfaceMixSideOffsetPanel", "PartPlate"):
            command = ["c++"] + shlex.split(" ".join(gui_flags.get(k, "") for k in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS")))
            command += ["-O0", "-c", str(ROOT / ("src/slic3r/GUI/" + source + ".cpp")), "-o", str(out / (source + ".o"))]
            extra_commands.append((source, command, ROOT))
            library = next(i for i, part in enumerate(link_args) if part.endswith("liblibslic3r_gui.a"))
            link_args.insert(library, str(out / (source + ".o")))
    (out / "commands.json").write_text(json.dumps({"compile": compile_args, "link": link_args, "link_cwd": str(build / "src"),
        "adapters": [{"label": label, "command": command, "cwd": str(cwd)} for label, command, cwd in extra_commands]}, indent=2) + "\n")
    for label, command, cwd in [("compile", compile_args, ROOT)] + extra_commands + [("link", link_args, build / "src")]:
        with (out / (label + ".log")).open("w") as log:
            result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            print(label + " failed: " + str(out / (label + ".log")))
            return result.returncode
    with (out / "result.json").open("w") as stdout, (out / "runtime.stderr.log").open("w") as stderr:
        result = subprocess.run([str(out / "native-check"), str(ROOT / "resources/surfacemix/creator5-cmyw-top-preview.v1.json")],
                                cwd=ROOT, stdout=stdout, stderr=stderr, timeout=90, check=False)
    print(out / "result.json")
    if result.returncode or not args.isolate_gui:
        return result.returncode
    cli_link = list(link_args)
    cli_link[cli_link.index(str(out / "native-check.o"))] = "CMakeFiles/OrcaSlicer.dir/OrcaSlicer.cpp.o"
    (out / "bin").mkdir(exist_ok=True)
    cli_link[cli_link.index("-o") + 1] = str(out / "bin/isolated-top-stack-cli")
    with (out / "cli-link.log").open("w") as log:
        result = subprocess.run(cli_link, cwd=build / "src", stdout=log, stderr=subprocess.STDOUT, check=False)
    if not (out / "resources").exists():
        (out / "resources").symlink_to(ROOT / "resources", target_is_directory=True)
    print(out / "bin/isolated-top-stack-cli")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
