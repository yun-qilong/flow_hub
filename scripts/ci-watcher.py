#!/usr/bin/env python3
"""
Gerrit CI Watcher (poll mode) - polls Gerrit via SSH for new patchsets,
triggers CI, reports Verified label via SSH gerrit review.

No special Gerrit capabilities needed.

Usage:
  python3 ci-watcher.py --project flow_hub [options]
"""

import argparse
import json
import os
import shlex
import subprocess
import sys
import time
import logging

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("ci-watcher")


def ssh_base(host: str, port: int, user: str) -> list:
    return ["ssh", "-p", str(port), f"{user}@{host}", "gerrit"]


def query_open_changes(host: str, port: int, user: str, project: str) -> list:
    cmd = ssh_base(host, port, user) + [
        "query", "--format", "JSON", "--current-patch-set",
        "status:open", f"project:{project}",
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        results = []
        for line in proc.stdout.strip().split("\n"):
            if not line:
                continue
            try:
                obj = json.loads(line)
                if "type" not in obj and "number" in obj:
                    results.append(obj)
            except json.JSONDecodeError:
                pass
        return results
    except Exception as e:
        log.error("Query failed: %s", e)
        return []


def post_review(host: str, port: int, user: str, revision: str, verified: int,
                message: str) -> bool:
    cmd = ssh_base(host, port, user) + [
        "review", revision,
        "--label", f"Verified={verified}",
        "-m", shlex.quote(message),
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if proc.returncode == 0:
            log.info("Review posted: rev=%s verified=%d", revision[:8], verified)
            return True
        log.error("Review failed: %s", proc.stderr.strip())
        return False
    except Exception as e:
        log.error("Review exception: %s", e)
        return False


def run_ci(ci_script: str, ref: str, project: str, branch: str,
           gerrit_host: str, gerrit_port: int, work_dir: str):
    build_dir = os.path.join(work_dir, "build")
    cmd = [
        "bash", ci_script,
        "--ref", ref,
        "--project", project,
        "--branch", branch,
        "--gerrit-host", gerrit_host,
        "--gerrit-port", str(gerrit_port),
        "--build-dir", build_dir,
    ]
    log.info("Running CI: %s", " ".join(cmd))
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        exit_code = proc.returncode
        result_file = os.path.join(build_dir, "ci-result.json")
        if os.path.exists(result_file):
            with open(result_file) as f:
                results = json.load(f)
        else:
            results = [{"name": "ci", "status": "FAILED",
                        "summary": "no result file"}]
        return exit_code, results
    except subprocess.TimeoutExpired:
        return 1, [{"name": "ci", "status": "FAILED",
                    "summary": "CI timeout (30 min)"}]
    except Exception as e:
        return 1, [{"name": "ci", "status": "FAILED",
                    "summary": f"CI exception: {e}"}]


def build_comment(change_num: int, patchset_num: int, exit_code: int,
                  results: list) -> str:
    sep = "-" * 40
    status_icon = "PASS" if exit_code == 0 else "FAIL"
    summary_lines = []
    for item in results:
        name = item.get("name", "?")
        status = item.get("status", "?")
        summary = item.get("summary", "")
        icon = "+" if status == "SUCCESS" else "-"
        summary_lines.append(f"  [{icon}] {name}: {summary}")

    summary_block = "\n".join(summary_lines)

    return (
        f"Patch Set {patchset_num}: CI {status_icon}\n"
        f"{sep}\n"
        f"{summary_block}\n"
        f"{sep}"
    )


def load_state(state_file: str) -> set:
    if not os.path.exists(state_file):
        return set()
    try:
        with open(state_file) as f:
            return set(line.strip() for line in f if line.strip())
    except Exception:
        return set()


def save_state(state_file: str, seen: set):
    with open(state_file, "w") as f:
        for rev in seen:
            f.write(f"{rev}\n")


def main():
    parser = argparse.ArgumentParser(description="Gerrit CI Watcher (poll)")
    parser.add_argument("--gerrit-host", default="localhost")
    parser.add_argument("--gerrit-port", type=int, default=19418)
    parser.add_argument("--gerrit-user", default="qilyun")
    parser.add_argument("--ci-script", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "gerrit-ci.sh"))
    parser.add_argument("--project", required=True)
    parser.add_argument("--work-dir", default="/tmp/gerrit-ci")
    parser.add_argument("--poll-interval", type=int, default=60)
    parser.add_argument("--state-file",
                        default="/tmp/gerrit-ci-watcher-state.txt")
    args = parser.parse_args()

    os.makedirs(args.work_dir, exist_ok=True)
    known_revisions = load_state(args.state_file)

    log.info("Polling Gerrit every %ds (project: %s)", args.poll_interval,
             args.project)

    while True:
        try:
            changes = query_open_changes(
                args.gerrit_host, args.gerrit_port, args.gerrit_user,
                args.project)

            log.info("Poll: %d open change(s)", len(changes))

            for change in changes:
                ps = change.get("currentPatchSet", {})
                revision = ps.get("revision", "")
                if not revision or revision in known_revisions:
                    continue

                known_revisions.add(revision)
                save_state(args.state_file, known_revisions)

                change_num = change.get("number", 0)
                ps_num = ps.get("number", 0)
                ref = ps.get("ref", "")
                branch = change.get("branch", "main")
                uploader = ps.get("uploader", {}).get("name", "unknown")

                log.info(">>> New patchset: change=%s ps=%s ref=%s by=%s",
                         change_num, ps_num, ref, uploader)

                exit_code, results = run_ci(
                    args.ci_script, ref, args.project, branch,
                    args.gerrit_host, args.gerrit_port, args.work_dir)

                verified = 1 if exit_code == 0 else -1
                comment = build_comment(change_num, ps_num,
                                        exit_code, results)

                post_review(args.gerrit_host, args.gerrit_port,
                            args.gerrit_user, revision, verified, comment)
                log.info("<<< Done: change=%s ps=%s verified=%d",
                         change_num, ps_num, verified)

        except KeyboardInterrupt:
            log.info("Interrupted, exiting")
            break
        except Exception as e:
            log.error("Poll error: %s", e)

        time.sleep(args.poll_interval)


if __name__ == "__main__":
    main()
