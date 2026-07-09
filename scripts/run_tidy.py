#!/usr/bin/env python3
"""
Run clang-tidy on project source files from compile_commands.json.
Only checks project files (filters out CAF _deps entries).
Filters CAF-related diagnostics from output.
"""

import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def resolve_file(build_dir, fpath):
    file_path = Path(fpath)
    if not file_path.is_absolute():
        file_path = (build_dir / file_path).resolve()
    return file_path


def filter_stderr(stderr):
    noise_patterns = [
        "Suppressed",
        "Found compiler error",
        "Error while processing",
        "warnings and",
        "errors generated",
        "Use -header-filter",
        "Use -system-headers",
    ]
    for line in stderr.split("\n"):
        stripped = line.strip()
        if not stripped:
            continue
        if any(p in stripped for p in noise_patterns):
            continue
        if "/_deps/" in stripped:
            continue
        print(stripped, file=sys.stderr)


def main():
    build_dir = Path("build")
    ccdb_path = build_dir / "compile_commands.json"
    ccdb_full_path = build_dir / "compile_commands_full.json"

    if not ccdb_path.exists():
        print(f"ERROR: {ccdb_path} not found", file=sys.stderr)
        sys.exit(1)

    shutil.copy(ccdb_path, ccdb_full_path)

    with open(ccdb_full_path) as f:
        db = json.load(f)

    project_entries = [e for e in db if "/_deps/" not in e.get("file", "")]
    skipped = len(db) - len(project_entries)

    with open(ccdb_path, "w") as f:
        json.dump(project_entries, f, indent=2)

    print(f"Tidy scope: {len(project_entries)} project files"
          f" (excluded {skipped} CAF entries)")

    files = sorted(set(e["file"] for e in project_entries))
    tidy_config = Path(".clang-tidy")

    if not tidy_config.exists():
        print("No .clang-tidy config, skipping", file=sys.stderr)
        sys.exit(0)

    def check_file(fpath):
        file_path = resolve_file(build_dir, fpath)
        if not file_path.exists():
            return None
        result = subprocess.run(
            ["clang-tidy",
             f"--config-file={tidy_config}",
             f"-p={build_dir}",
             "--extra-arg=-ferror-limit=1",
             str(file_path)],
            capture_output=True,
            text=True)
        return (fpath, result.stdout, result.stderr)

    workers = min(6, len(files), os.cpu_count() or 4)
    print(f"Running clang-tidy on {len(files)} files ({workers} workers)")

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(check_file, f): f for f in files}
        for future in as_completed(futures):
            ret = future.result()
            if ret is None:
                continue
            fpath, stdout, stderr = ret
            if stderr:
                filter_stderr(stderr)
            if stdout:
                lines = stdout.split("\n")
                filtered = [l for l in lines if "/_deps/" not in l]
                shown = filtered[:20]
                for line in shown:
                    print(line)
                if len(filtered) > 20:
                    print(f"... ({len(filtered) - 20} more lines filtered)")

    print("Tidy check complete")


if __name__ == "__main__":
    main()
