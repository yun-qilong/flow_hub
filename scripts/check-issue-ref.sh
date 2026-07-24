#!/usr/bin/env bash
# Validate commit message references a valid GitHub Issue with matching Tag.
# Default: warn only (exit 0). With --strict: fail on error (exit 1).
#
# Usage:
#   bash scripts/check-issue-ref.sh <commit-ref>          (CI, non-blocking)
#   bash scripts/check-issue-ref.sh --strict <commit-ref> (commit hook, blocking)

set -euo pipefail

STRICT=false
COMMIT_REF="HEAD"

for arg in "$@"; do
  case "$arg" in
    --strict) STRICT=true ;;
    *) COMMIT_REF="$arg" ;;
  esac
done

GITHUB_REPO="yun-qilong/flow_hub"

warn() {
  echo "=========================================" >&2
  echo "ISSUE CHECK WARNING: $*" >&2
  echo "=========================================" >&2
}

die() {
  if $STRICT; then
    echo "=========================================" >&2
    echo "ISSUE CHECK FAILED: $*" >&2
    echo "=========================================" >&2
    exit 1
  else
    warn "$*"
    exit 0
  fi
}

# ---- extract fields from commit message ------------------------------
COMMIT_MSG=$(git log -1 --format=%B "$COMMIT_REF" 2>/dev/null || true)

if [[ -z "$COMMIT_MSG" ]]; then
  die "Cannot read commit message for $COMMIT_REF"
fi

SUBJECT=$(echo "$COMMIT_MSG" | head -1)

# Extract first [...] as Tag
TAG=$(echo "$SUBJECT" | grep -oP '^\[[^]]+\]' | head -1 || true)

if [[ -z "$TAG" ]]; then
  die "Subject must start with [Tag].
  Subject: $SUBJECT
  Example: [RI0001] Set up task tracking system (#2)"
fi

# [None] bypasses all checks
if [[ "$TAG" == "[None]" ]]; then
  echo "✓ [None] — issue check bypassed"
  exit 0
fi

# Extract issue number from (#N)
ISSUE_NUM=$(echo "$SUBJECT" | grep -oP '\(#\d+\)' | grep -oP '\d+' | head -1 || true)

if [[ -z "$ISSUE_NUM" ]]; then
  die "Subject must reference a GitHub Issue with (#N).
  Subject: $SUBJECT"
fi

echo "Commit: $SUBJECT"
echo "Tag:    $TAG"
echo "Issue:  #$ISSUE_NUM"

# ---- cross-validate: GitHub API --------------------------------------
GITHUB_API="https://api.github.com/repos/${GITHUB_REPO}/issues/${ISSUE_NUM}"
HTTP_CODE=$(curl -s -o /tmp/issue_body.json -w "%{http_code}" \
  -H "Accept: application/vnd.github.v3+json" \
  "$GITHUB_API" 2>/dev/null || echo "000")

if [[ "$HTTP_CODE" != "200" ]]; then
  die "GitHub Issue #${ISSUE_NUM} not found (HTTP $HTTP_CODE).
  Create it first: https://github.com/${GITHUB_REPO}/issues/new"
fi

# Strip brackets for comparison: [RI0001] → RI0001
TAG_CLEAN=$(echo "$TAG" | tr -d '[]')
EXPECTED_TAG_LINE="**Tag**: $TAG_CLEAN"

if grep -qP "\*\*Tag\*\*:\s*${TAG_CLEAN}(?![-A-Za-z0-9])" /tmp/issue_body.json 2>/dev/null; then
  echo "✓ Tag-Issue cross-validation passed"

  # Check if issue is closed (strict mode only)
  if $STRICT; then
    ISSUE_STATE=$(grep -oP '"state"\s*:\s*"\K\w+' /tmp/issue_body.json | head -1)
    if [[ "$ISSUE_STATE" == "closed" ]]; then
      rm -f /tmp/issue_body.json
      die "Issue #${ISSUE_NUM} is CLOSED.
  Reopen it or use a different issue."
    fi
  fi

  rm -f /tmp/issue_body.json
  exit 0
else
  rm -f /tmp/issue_body.json
  die "Issue #${ISSUE_NUM} body does NOT contain: $EXPECTED_TAG_LINE
  Add it to the issue body."
fi
