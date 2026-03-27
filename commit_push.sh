#!/usr/bin/env bash
# commit_push.sh — stage, commit, and push changes to the current branch.
#
# Usage:
#   ./commit_push.sh                    # prompts for commit message
#   ./commit_push.sh "My message here"  # message as argument
#
# Files never auto-staged: .claude/ (Claude Code internal settings)

set -e
cd "$(dirname "$0")"

# ── 1. Show what has changed ───────────────────────────────────────────────
echo ""
echo "=== Modified / untracked files ==="
git status -s --untracked-files=normal | grep -v "^?? \.claude/"
echo ""

# ── 2. Stage everything except .claude/ ───────────────────────────────────
git add -A -- ':!.claude/'

echo "=== Files to be committed ==="
git --no-pager diff --cached --stat
echo ""

# Abort early if nothing staged
if git diff --cached --quiet; then
    echo "Nothing to commit."
    exit 0
fi

# ── 3. Commit message ─────────────────────────────────────────────────────
if [ -n "$1" ]; then
    MSG="$1"
else
    printf "Commit message: "
    read -r MSG
fi

if [ -z "$MSG" ]; then
    echo "Aborting: empty commit message."
    exit 1
fi

git commit -m "$MSG"

# ── 4. Push ───────────────────────────────────────────────────────────────
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
echo ""
echo "Pushing to origin/$BRANCH ..."
git push origin "$BRANCH"

echo ""
echo "Done. Latest commits:"
git log --oneline -5
