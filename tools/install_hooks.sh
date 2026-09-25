#!/usr/bin/env bash
# Install this clone's git hooks. Run it once per clone and once per worktree:
#
#     tools/install_hooks.sh
#
# Hooks live in .git/ and are never cloned, so a fresh worktree has none -- which is
# exactly why the hook is a courtesy and CI (the doc-rules check) is the gate. All this buys is
# finding a mechanical-check failure before the push instead of in review.
set -eu

cd "$(dirname "$0")/.."

# In a worktree, .git is a file pointing at the real hooks dir; git resolves both.
hooks="$(git rev-parse --git-path hooks)"
mkdir -p "$hooks"
target="$hooks/pre-push"

if [ -e "$target" ] && ! grep -q "doc_rules.sh" "$target" 2>/dev/null; then
    echo "$target exists and is not ours -- move it aside, then run this again" >&2
    exit 1
fi

cat > "$target" <<'HOOK'
#!/usr/bin/env bash
# Installed by tools/install_hooks.sh. Runs the mechanical checks before a push:
# doc_rules.sh for the prose docs, scan_rules.sh for the source. Either one failing
# stops the push. Skip a known-bad push with `git push --no-verify`; the doc-rules
# check will still refuse it.
set -eu
repo="$(git rev-parse --show-toplevel)"
for script in doc_rules.sh scan_rules.sh; do
    [ -f "$repo/tools/$script" ] || continue
    bash "$repo/tools/$script"
done
HOOK

chmod +x "$target"
echo "installed $target"
