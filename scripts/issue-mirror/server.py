import json
import subprocess
from flask import Flask, request
import markdown
import requests

import os
TOKEN_FILE = os.path.expanduser("~/.flowhub/mirror-token")
TOKEN = open(TOKEN_FILE).read().strip() if os.path.exists(TOKEN_FILE) else None

GERRIT_HOST = "localhost"
GERRIT_PORT = "29418"
GERRIT_USER = "qilyun"
GERRIT_PROJECT = "flow_hub"

app = Flask(__name__)


def gh_headers():
    h = {"Accept": "application/vnd.github.v3+json"}
    if TOKEN:
        h["Authorization"] = "Bearer {}".format(TOKEN)
    return h


def query_gerrit(issue_num):
    cmd = [
        "ssh", "-p", GERRIT_PORT, "{}@{}".format(GERRIT_USER, GERRIT_HOST),
        "gerrit", "query", "--format", "JSON", "--current-patch-set",
        'message:"#{}"'.format(issue_num),
        "project:{}".format(GERRIT_PROJECT),
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        changes = []
        for line in proc.stdout.strip().split("\n"):
            if not line:
                continue
            try:
                obj = json.loads(line)
                if "type" not in obj and "number" in obj:
                    changes.append({
                        "number": obj["number"],
                        "subject": obj.get("subject", ""),
                        "status": obj.get("status", ""),
                        "url": obj.get("url", ""),
                    })
            except json.JSONDecodeError:
                pass
        return changes
    except Exception:
        return []


def query_relationships(issue_num):
    url = "https://api.github.com/repos/yun-qilong/flow_hub/issues/{}/timeline?per_page=30".format(issue_num)
    headers = gh_headers()
    rels = {"parent": None, "blocked_by": [], "blocking": []}
    try:
        resp = requests.get(url, headers=headers, timeout=10)
        for event in (resp.json() if resp.status_code == 200 else []):
            ev = event.get("event", "")
            if ev == "parent_issue_added":
                pi = event.get("parent_issue", {})
                rels["parent"] = {"number": pi.get("number"), "title": pi.get("title", ""), "state": pi.get("state", "")}
            elif ev == "blocked_by_added":
                bi = event.get("blocked_by", {})
                rels["blocked_by"].append({"number": bi.get("number"), "title": bi.get("title", ""), "state": bi.get("state", "")})
            elif ev == "blocking_added":
                bi = event.get("blocking", {})
                rels["blocking"].append({"number": bi.get("number"), "title": bi.get("title", ""), "state": bi.get("state", "")})
    except Exception:
        pass
    return rels


def query_sub_issues(issue_num):
    url = "https://api.github.com/repos/yun-qilong/flow_hub/issues/{}/sub_issues".format(issue_num)
    headers = gh_headers()
    try:
        resp = requests.get(url, headers=headers, timeout=10)
        return resp.json() if resp.status_code == 200 else []
    except Exception:
        return []


@app.route("/issue")
def issue_list():
    filt = request.args.get("state", "open")
    page = 1
    all_issues = []
    while True:
        url = "https://api.github.com/repos/yun-qilong/flow_hub/issues?state={}&per_page=100&page={}".format(filt, page)
        resp = requests.get(url, headers=gh_headers(), timeout=10)
        if resp.status_code != 200:
            break
        data = resp.json()
        if not data:
            break
        all_issues.extend(data)
        page += 1

    all_issues.sort(key=lambda x: x["number"], reverse=True)

    items = ""
    for iss in all_issues:
        num = iss["number"]
        title = iss["title"]
        state = iss["state"]
        labels = iss.get("labels", [])
        label_html = "".join(
            '<span class="label" style="background:#{};color:#fff;">{}</span> '.format(
                l["color"], {"feature":"FT","fix":"FX","refine&improvement":"RI"}.get(l["name"], l["name"][:2].upper())
            )
            for l in labels
        )
        status_icon = '<span class="status-icon status-open"></span>' if state == "open" else '<span class="status-icon status-closed">✓</span>'
        blocked_icon = '<span class="status-icon status-blocked" title="Blocked">━</span>' if iss.get("locked") else ""

        items += """<div class="issue-row">
  <a href="/issue/{}" class="row-link">{}{} <span class="row-num">#{}</span> {} <span class="row-title">{}</span></a>
</div>""".format(num, status_icon, blocked_icon, num, label_html, title)

    open_selected = "selected" if filt == "open" else ""
    closed_selected = "selected" if filt == "closed" else ""
    all_selected = "selected" if filt == "all" else ""

    html = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Issues · FlowHub Mirror</title>
<style>
body {{ font-family: -apple-system, sans-serif; max-width: 960px; margin: 32px auto; padding: 0 24px; color: #1f2328; }}
h1 {{ font-size: 24px; border-bottom: 1px solid #d0d7de; padding-bottom: 12px; }}
.filters {{ margin: 16px 0; }}
.filters a {{ padding: 6px 14px; border-radius: 2em; text-decoration: none; color: #24292f; margin-right: 8px; font-size: 14px; }}
.filters a.selected {{ background: #0969da; color: #fff; }}
.issue-row {{ border-bottom: 1px solid #d0d7de; }}
.row-link {{ display: block; padding: 12px 8px; text-decoration: none; color: #1f2328; }}
.row-link:hover {{ background: #f6f8fa; }}
.row-num {{ color: #656d76; font-size: 13px; margin-right: 8px; }}
.row-title {{ font-weight: 600; }}
.label {{ display: inline-block; padding: 1px 8px; border-radius: 2em; font-size: 11px; font-weight: 600; margin-right: 3px; }}
.status-icon {{ display: inline-block; width: 14px; height: 14px; border-radius: 50%; vertical-align: middle; margin-right: 4px; }}
.status-open {{ background: #1f883d; border: 2px solid #1f883d; box-shadow: inset 0 0 0 3px #fff; }}
.status-closed {{ background: #8250df; color: #fff; text-align: center; line-height: 14px; font-size: 10px; font-weight: bold; }}
.status-blocked {{ background: #cf222e; color: #fff; text-align: center; line-height: 14px; font-size: 12px; font-weight: bold; }}
</style></head><body>
<h1>Issues</h1>
<div class="filters">
  <a href="/issue?state=open" class="{}">Open</a>
  <a href="/issue?state=closed" class="{}">Closed</a>
  <a href="/issue?state=all" class="{}">All</a>
</div>
{}
</body></html>""".format(open_selected, closed_selected, all_selected, items)
    return html


@app.route("/issue/<int:id>")
def issue(id):
    url = "https://api.github.com/repos/yun-qilong/flow_hub/issues/{}".format(id)
    try:
        data = requests.get(url, timeout=10).json()
    except Exception as e:
        return "<h1>GitHub API unreachable</h1><p>{}</p>".format(str(e)), 502
    if "title" not in data:
        return "<h1>GitHub API error</h1><pre>{}</pre>".format(json.dumps(data, indent=2)), 502
    title = data["title"]
    body = data.get("body", "")
    state = data["state"]
    labels = data.get("labels", [])
    label_html = "".join(
        '<span class="label" style="background:#{};color:#fff;">{}</span>'.format(l["color"], l["name"])
        for l in labels
    )
    html_url = data["html_url"]
    user = data["user"]["login"]
    created = data["created_at"][:10]

    # Relationships
    sub_issues = query_sub_issues(id)

    # Gerrit changes — only show those with "(#N)" in subject
    all_changes = query_gerrit(id)
    changes = [c for c in all_changes if "(#{})".format(id) in c["subject"]]

    # Build sidebar
    rels = query_relationships(id)
    any_blocked_open = any(b["state"] == "open" for b in rels["blocked_by"])
    blocked_badge = '<span class="state state-blocked">Blocked</span>' if any_blocked_open else ""

    def icon(r):
        if r.get("state") == "open":
            return '<span class="status-icon status-open" title="Open"></span> '
        return '<span class="status-icon status-closed" title="Closed">✓</span> '

    sidebar = ""
    if rels["parent"]:
        sidebar += '<div class="side-section"><h3>Parent Issue</h3>{}<a href="/issue/{}">#{} {}</a></div>'.format(
            icon(rels["parent"]), rels["parent"]["number"], rels["parent"]["number"], rels["parent"]["title"]
        )
    if rels["blocked_by"]:
        sidebar += '<div class="side-section blocked-section"><h3>Blocked by</h3>'
        for b in rels["blocked_by"]:
            sidebar += '{}<a href="/issue/{}">#{} {}</a><br>'.format(icon(b), b["number"], b["number"], b["title"])
        sidebar += '</div>'
    if rels["blocking"]:
        sidebar += '<div class="side-section"><h3>Blocking</h3>'
        for b in rels["blocking"]:
            sidebar += '{}<a href="/issue/{}">#{} {}</a><br>'.format(icon(b), b["number"], b["number"], b["title"])
        sidebar += '</div>'

    if sub_issues:
        sidebar += '<div class="side-section"><h3>Sub-issues</h3>'
        for sub in sub_issues:
            sn = sub.get("number", "")
            st = sub.get("title", "")
            ss = sub.get("state", "")
            si = '<span class="status-icon status-open" title="Open"></span> ' if ss == "open" else '<span class="status-icon status-closed" title="Closed">✓</span> '
            sidebar += '{}<a href="/issue/{}">#{} {}</a><br>'.format(si, sn, sn, st)
        sidebar += '</div>'

    sidebar += '<div class="side-section"><h3>Gerrit Changes</h3>'
    if changes:
        for c in changes:
            sidebar += '<div class="change"><a href="{}" target="_blank">{}</a><br><small>Change {} · {}</small></div>'.format(
                c["url"], c["subject"], c["number"], c["status"]
            )
    else:
        sidebar += '<p class="empty">No changes yet.</p>'
    sidebar += "</div>"

    html = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{} · Issue #{}</title>
<style>
body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; color: #1f2328; background: #fff; margin: 0; }}
.layout {{ display: flex; max-width: 1280px; margin: 0 auto; }}
.main {{ flex: 1; min-width: 0; padding: 32px 24px; }}
.sidebar {{ width: 280px; padding: 32px 20px; border-left: 1px solid #d0d7de; background: #f6f8fa; }}
.gh-link {{ float: right; font-size: 14px; color: #0969da; text-decoration: none; }}
.header {{ border-bottom: 1px solid #d0d7de; padding-bottom: 16px; margin-bottom: 16px; }}
.header h1 {{ font-size: 28px; font-weight: 600; margin: 0 0 8px 0; display: inline; }}
.header .num {{ color: #656d76; font-weight: 400; }}
.state {{ display: inline-block; padding: 3px 14px; border-radius: 2em; font-size: 14px; font-weight: 600; margin-left: 12px; vertical-align: middle; }}
.state-open {{ background: #1f883d; color: #fff; }}
.state-closed {{ background: #8250df; color: #fff; }}
.state-blocked {{ background: #cf222e; color: #fff; }}
.blocked-section h3 {{ color: #cf222e !important; }}
.status-icon {{ display: inline-block; width: 14px; height: 14px; border-radius: 50%; vertical-align: middle; margin-right: 4px; }}
.status-open {{ background: #1f883d; border: 2px solid #1f883d; box-shadow: inset 0 0 0 3px #fff; }}
.status-closed {{ background: #8250df; color: #fff; text-align: center; line-height: 14px; font-size: 10px; font-weight: bold; }}
.meta {{ color: #656d76; font-size: 14px; margin: 10px 0; }}
.labels {{ margin: 10px 0; }}
.label {{ display: inline-block; padding: 2px 10px; border-radius: 2em; font-size: 12px; font-weight: 600; margin-right: 4px; }}
.body {{ line-height: 1.6; }}
.body h1, .body h2 {{ border-bottom: 1px solid #d0d7de; padding-bottom: 8px; }}
table {{ border-collapse: collapse; margin: 12px 0; }}
th, td {{ border: 1px solid #d0d7de; padding: 8px 14px; text-align: left; }}
th {{ background: #f6f8fa; font-weight: 600; }}
pre {{ background: #f6f8fa; padding: 14px; border-radius: 6px; overflow-x: auto; }}
code {{ background: #f6f8fa; padding: 2px 5px; border-radius: 4px; font-size: 90%; }}
pre code {{ background: none; padding: 0; }}
.side-section {{ margin-bottom: 24px; }}
.side-section h3 {{ font-size: 14px; color: #656d76; margin: 0 0 8px 0; }}
.side-section ul {{ list-style: none; padding: 0; margin: 0; }}
.side-section li {{ padding: 4px 0; font-size: 13px; }}
.side-section a {{ color: #0969da; text-decoration: none; }}
.side-section a:hover {{ text-decoration: underline; }}
.change {{ padding: 8px; margin: 6px 0; background: #fff; border: 1px solid #d0d7de; border-radius: 6px; font-size: 13px; }}
.change small {{ color: #656d76; }}
.empty {{ color: #656d76; font-size: 13px; font-style: italic; }}
.footer {{ border-top: 1px solid #d0d7de; margin-top: 32px; padding-top: 16px; color: #656d76; font-size: 13px; }}
</style></head><body>
<div class="layout">
<div class="main">
<a class="gh-link" href="{}" target="_blank">Open in GitHub ↗</a>
<div class="header">
<h1>{}</h1><span class="num"> #{}</span>
<span class="state state-{}">{}</span>
{}
</div>
<p class="meta"><b>{}</b> opened on {}</p>
<div class="labels">{}</div>
<div class="body">{}</div>
<div class="footer">Mirror · read-only · edit on GitHub</div>
</div>
<div class="sidebar">
{}
</div>
</div>
</body></html>""".format(
        title, id,
        html_url,
        title, id,
        state, state.title(),
        blocked_badge,
        user, created,
        label_html,
        markdown.markdown(body, extensions=["tables", "fenced_code"]),
        sidebar,
    )
    return html


app.run(host="127.0.0.1", port=8091)
