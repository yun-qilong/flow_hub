#!/usr/bin/env python3
"""
Run clang-tidy on project source files from compile_commands.json.
Only checks project files (filters out CAF _deps entries).
Generates an HTML report in build/tidy-report.html.
"""

import fnmatch
import html as html_mod
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path

DIAG_PATTERN = re.compile(
    r"^(.+?):(\d+):(\d+):\s+(warning|error):\s+(.+)\s+\[(.+?)\]$")


@dataclass
class Diag:
    file: str
    line: int
    col: int
    level: str
    message: str
    check: str


@dataclass
class FileResult:
    path: str
    diags: list = field(default_factory=list)

    @property
    def errors(self):
        return [d for d in self.diags if d.level == "error"]

    @property
    def warnings(self):
        return [d for d in self.diags if d.level == "warning"]


def resolve_file(build_dir, fpath):
    file_path = Path(fpath)
    if file_path.is_absolute():
        return file_path if file_path.exists() else None
    candidates = [
        (build_dir / file_path).resolve(),
        Path(fpath).resolve(),
    ]
    for p in candidates:
        if p.exists():
            return p
    return None


def parse_diags(file_path, stdout):
    results = []
    for line in stdout.split("\n"):
        m = DIAG_PATTERN.match(line.strip())
        if not m:
            continue
        if "/_deps/" in m.group(1):
            continue
        results.append(Diag(
            file=m.group(1),
            line=int(m.group(2)),
            col=int(m.group(3)),
            level=m.group(4),
            message=m.group(5),
            check=m.group(6),
        ))
    return results


def filter_stderr(stderr):
    noise_patterns = [
        "Suppressed", "Found compiler error",
        "Error while processing", "warnings and",
        "errors generated", "Use -header-filter",
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


def build_html(results, whitelist, repo_root):
    lines = []
    lines.append("<!DOCTYPE html>")
    lines.append('<html lang="en"><head><meta charset="UTF-8">')
    lines.append("<title>clang-tidy report</title>")
    lines.append("<style>")
    lines.append("body{font-family:monospace;background:#1e1e1e;color:#d4d4d4;"
                 "margin:0;padding:20px}")
    lines.append(".summary{background:#2d2d2d;padding:16px;border-radius:8px;"
                 "margin-bottom:20px;display:flex;gap:24px;flex-wrap:wrap}")
    lines.append(".summary div{text-align:center}")
    lines.append(".summary .num{font-size:32px;font-weight:bold}")
    lines.append(".summary .err{color:#f44747}")
    lines.append(".summary .warn{color:#cca700}")
    lines.append(".file-block{background:#2d2d2d;border-radius:8px;"
                 "margin-bottom:12px;overflow:hidden}")
    lines.append(".file-header{background:#3c3c3c;padding:10px 16px;"
                 "font-weight:bold;display:flex;justify-content:space-between}")
    lines.append(".diag{padding:6px 16px 6px 32px;border-bottom:1px solid #3c3c3c}")
    lines.append(".diag:last-child{border-bottom:none}")
    lines.append(".diag.new{border-left:3px solid #f44747}")
    lines.append(".diag.mismatch{border-left:3px solid #cca700}")
    lines.append(".diag .loc{color:#569cd6;margin-right:8px}")
    lines.append(".diag .check{color:#808080;margin-left:12px}")
    lines.append(".diag .msg{color:#d4d4d4}")
    lines.append("</style></head><body>")
    lines.append("<h1>clang-tidy report</h1>")

    total_files = len(results)
    total_violations = 0

    for fr in results:
        rp = rel_path(fr.path, repo_root)
        wl_checks = whitelist.get(rp, {})
        actual_counts = {}
        for d in fr.diags:
            actual_counts[d.check] = actual_counts.get(d.check, 0) + 1

        violations_count = 0
        for d in fr.diags:
            expected = wl_checks.get(d.check)
            if expected is None or expected != actual_counts[d.check]:
                violations_count += 1
                total_violations += 1

    lines.append('<div class="summary">')
    lines.append(f'<div><div class="num">{total_files}</div>product files</div>')
    lines.append(f'<div><div class="num err">{total_violations}</div>violations</div>')
    lines.append("</div>")

    for fr in results:
        rp = rel_path(fr.path, repo_root)
        wl_checks = whitelist.get(rp, {})
        actual_counts = {}
        for d in fr.diags:
            actual_counts[d.check] = actual_counts.get(d.check, 0) + 1

        violations_in_file = []
        for d in fr.diags:
            expected = wl_checks.get(d.check)
            if expected is None:
                violations_in_file.append(("new", d))
            elif expected != actual_counts[d.check]:
                violations_in_file.append(("mismatch", d))

        if not violations_in_file:
            continue

        lines.append('<div class="file-block">')
        lines.append(f'<div class="file-header"><span>{html_mod.escape(rp)}'
                     f'</span><span>{len(violations_in_file)} violations</span></div>')

        shown_checks = set()
        for vtype, d in violations_in_file:
            if vtype == "mismatch" and d.check in shown_checks:
                continue
            shown_checks.add(d.check)
            cls = "new" if vtype == "new" else "mismatch"
            label = "NEW" if vtype == "new" else "MISMATCH"
            lines.append(f'<div class="diag {cls}">')
            lines.append(f'<span class="loc">L{d.line}:{d.col}</span>')
            lines.append(f'<span class="msg">[{label}] {html_mod.escape(d.message)}</span>')
            lines.append(f'<span class="check">[{d.check}]</span>')
            lines.append("</div>")

        lines.append("</div>")

    if total_violations == 0:
        lines.append('<p style="color:#4ec9b0">All clear &#10003;</p>')

    lines.append("</body></html>")
    return "\n".join(lines)


def load_whitelist(path):
    if not path.exists():
        return {}
    with open(path) as f:
        return json.load(f)


def rel_path(file_path, repo_root):
    sp = str(file_path)
    rp = str(repo_root)
    if sp.startswith(rp):
        sp = sp[len(rp):].lstrip("/")
    return sp


def load_ignore_rules(path):
    if not path.exists():
        return {"ignored_checks": [], "path_rules": []}
    with open(path) as f:
        return json.load(f)


def should_ignore(rpath, check, ignore_rules):
    """Check if a (relative_path, check_name) pair should be ignored."""
    if check in ignore_rules.get("ignored_checks", []):
        return True
    for rule in ignore_rules.get("path_rules", []):
        if fnmatch.fnmatch(rpath, rule["pattern"]):
            ignored = rule.get("ignored_checks", [])
            if "*" in ignored or check in ignored:
                return True
    return False


def check_whitelist(results, whitelist, repo_root):
    violations = []
    has_new = False

    actual_counts = {}
    for fr in results:
        rp = rel_path(fr.path, repo_root)
        for d in fr.diags:
            key = (rp, d.check)
            actual_counts[key] = actual_counts.get(key, 0) + 1

    for (rp, check), count in sorted(actual_counts.items()):
        expected = whitelist.get(rp, {}).get(check)
        if expected is None:
            violations.append(
                f"  NEW: {rp}: {check} ({count} occurrences)")
            has_new = True
        elif expected != count:
            violations.append(
                f"  MISMATCH: {rp}: {check} "
                f"(whitelist: {expected}, actual: {count})")
            has_new = True

    stale_items = []
    for rp, checks in whitelist.items():
        for check, expected in checks.items():
            key = (rp, check)
            if key not in actual_counts:
                stale_items.append(
                    f"  stale: {rp}: {check} "
                    f"(whitelist: {expected}, actual: 0)")

    if stale_items:
        print(f"\n--- STALE WHITELIST ENTRIES ({len(stale_items)}) ---")
        for s in stale_items:
            print(s)

    return has_new, violations


def main():
    build_dir = Path("build")
    ccdb_path = build_dir / "compile_commands.json"
    ccdb_full_path = build_dir / "compile_commands_full.json"
    whitelist_path = Path(".tidy-whitelist.json")

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
    tidy_bin = os.environ.get("CLANG_TIDY_BIN", "clang-tidy")

    if not tidy_config.exists():
        print("No .clang-tidy config, skipping", file=sys.stderr)
        sys.exit(0)

    def check_file(fpath):
        file_path = resolve_file(build_dir, fpath)
        if file_path is None:
            return None
        result = subprocess.run(
            [tidy_bin,
             f"--config-file={tidy_config}",
             f"-p={build_dir}",
             "--extra-arg=-ferror-limit=1",
             str(file_path)],
            capture_output=True,
            text=True)
        return (fpath, result.stdout, result.stderr)

    workers = min(6, len(files), os.cpu_count() or 4)
    print(f"Running clang-tidy on {len(files)} files ({workers} workers)")

    all_results = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(check_file, f): f for f in files}
        for future in as_completed(futures):
            ret = future.result()
            if ret is None:
                continue
            fpath, stdout, stderr = ret
            if stderr:
                filter_stderr(stderr)
            diags = parse_diags(fpath, stdout)
            all_results.append(FileResult(path=fpath, diags=diags))

    all_results.sort(key=lambda r: r.path)
    repo_root = Path.cwd()

    ignore_rules = load_ignore_rules(Path(".tidy-ignore.json"))

    for fr in all_results:
        rp = rel_path(fr.path, repo_root)
        fr.diags = [d for d in fr.diags
                     if not should_ignore(rel_path(d.file, repo_root),
                                          d.check, ignore_rules)]

    ut_results = [r for r in all_results if "/ut/" in str(r.path)]
    prod_results = [r for r in all_results if "/ut/" not in str(r.path)]

    print(f"\nUT files: {len(ut_results)} (skipped)")
    print(f"Product files: {len(prod_results)}")

    whitelist = load_whitelist(whitelist_path)
    update_wl = "--update-whitelist" in sys.argv

    if update_wl:
        new_wl = {}
        for fr in prod_results:
            rp = rel_path(fr.path, repo_root)
            check_counts = {}
            for d in fr.diags:
                check_counts[d.check] = check_counts.get(d.check, 0) + 1
            if check_counts:
                new_wl[rp] = dict(sorted(check_counts.items()))
        whitelist_path.write_text(json.dumps(new_wl, indent=2) + "\n")
        print(f"Whitelist updated: {whitelist_path}")
        print("Tidy check complete")
        return

    violated, violations = check_whitelist(prod_results, whitelist, repo_root)

    if violations:
        print(f"\n--- WHITELIST VIOLATIONS ---")
        for v in violations:
            print(v)
        print(f"--- {len(violations)} violations ---\n")

    report_path = build_dir / "tidy-report.html"
    html_content = build_html(prod_results, whitelist, repo_root)
    report_path.write_text(html_content)
    print(f"Report: {report_path}")

    print("Tidy check complete")

    if violated:
        print("ERROR: whitelist violations detected", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
