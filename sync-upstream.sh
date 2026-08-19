#!/bin/zsh
# sync-upstream.sh — pull new retrodev BlastEm (Mercurial) changesets and land
# them in this git fork as real per-changeset commits, then merge into main.
#
# Why not hg-fast-export: the .git/hg2git-* marks from the original conversion
# went stale when history was rewritten with git-filter-repo, so an incremental
# fast-export would graft onto orphaned pre-rewrite commits. This script
# replays changesets patch-by-patch instead: for each new upstream rev it
# applies `hg diff prev cur` and commits with the original author/date/message.
# Tree equality with upstream is guaranteed by induction (each step reproduces
# exactly that revision's tree), and was verified byte-for-byte on the
# 2026-08 sync (137 changesets, e623ea1d5363 -> 732f5689d438).
#
# Usage:  ./sync-upstream.sh [path-to-hg-clone]
#   - run from the git repo root, on a clean working tree
#   - creates/advances branch `upstream-sync`, then merges it into the
#     current branch with a "sync: upstream blastem @<node>" message
#   - the merge message doubles as the sync marker for the next run
set -euo pipefail

HG_REPO=${1:-../blastem}
HG_URL=https://www.retrodev.com/repos/blastem

# ---- sanity ----------------------------------------------------------------
git rev-parse --is-inside-work-tree >/dev/null
if [ -n "$(git status --porcelain)" ]; then
  echo "error: working tree not clean" >&2; exit 1
fi
command -v hg >/dev/null || { echo "error: mercurial (hg) not installed" >&2; exit 1; }

# ---- find last synced upstream node & its git commit -----------------------
# Recorded in prior sync merge messages; falls back to the original conversion
# boundary (hg 2840 e623ea1d5363 == git 47db172).
last_sync=$(git log --grep='^sync: upstream blastem @' -1 --format='%s' || true)
if [ -n "$last_sync" ]; then
  BASE_NODE=${last_sync##*@}
  BASE_GIT=$(git rev-list -n1 upstream-sync 2>/dev/null || true)
else
  BASE_NODE=e623ea1d5363c33dc4b4f57ea8df3b4a28ae2ee6
  BASE_GIT=47db172bb94181fdcfc15797a1f7b5a71c6df0bc
fi
[ -n "$BASE_GIT" ] || { echo "error: branch upstream-sync missing; delete the sync marker or recreate it" >&2; exit 1; }

# ---- pull upstream ---------------------------------------------------------
REMOTE_TIP=$(hg identify -q "$HG_URL")
echo "upstream tip: $REMOTE_TIP   last synced: ${BASE_NODE:0:12}"
hg -R "$HG_REPO" pull -q -r "$REMOTE_TIP" "$HG_URL"
NEW=$(hg -R "$HG_REPO" log -r "sort(::$REMOTE_TIP and not ::$BASE_NODE, rev)" -T '{node}\n')
if [ -z "$NEW" ]; then echo "already up to date"; exit 0; fi
echo "new changesets: $(echo "$NEW" | wc -l | tr -d ' ')"

# ---- replay onto branch upstream-sync --------------------------------------
git checkout -q -B upstream-sync "$BASE_GIT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
prev=$BASE_NODE
echo "$NEW" | while read -r node; do
  hg -R "$HG_REPO" diff --git -X path:.hgtags -r "$prev" -r "$node" > "$tmp/p"
  [ -s "$tmp/p" ] && git apply --index --binary --whitespace=nowarn "$tmp/p"
  author=$(hg -R "$HG_REPO" log -r "$node" -T '{author}')
  adate=$(hg -R "$HG_REPO" log -r "$node" -T '{date|rfc822date}')
  hg -R "$HG_REPO" log -r "$node" -T '{desc}' > "$tmp/msg"
  GIT_COMMITTER_NAME="${author%% <*}" \
  GIT_COMMITTER_EMAIL="$(echo "$author" | sed -n 's/.*<\(.*\)>.*/\1/p')" \
  GIT_COMMITTER_DATE="$adate" \
  git commit -q --allow-empty --author="$author" --date="$adate" -F "$tmp/msg"
  prev=$node
done

# verify the replayed tree matches upstream exactly
hg -R "$HG_REPO" archive -r "$REMOTE_TIP" "$tmp/hgtip"
rm -f "$tmp/hgtip/.hg_archival.txt" "$tmp/hgtip/.hgtags"
if ! diff -r -q "$tmp/hgtip" . -x .git -x .gitignore -x .github >/dev/null; then
  echo "error: replayed tree differs from upstream archive — aborting before merge" >&2
  exit 1
fi
echo "tree verified against upstream archive"

# ---- merge into the original branch ----------------------------------------
git checkout -q -
git merge --no-ff -m "sync: upstream blastem @$REMOTE_TIP" upstream-sync
echo "done: merged upstream @$REMOTE_TIP"
