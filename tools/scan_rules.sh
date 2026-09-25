#!/usr/bin/env bash
# Mechanical source checks. Run it before you hand over: tools/scan_rules.sh
# Every failure prints as `path:line: reason`; all rules run, then the script exits 1
# if any of them failed. Each rule cites the issue or PR it came from and says what a
# legitimate exception looks like. No judgement rules live here -- only what a machine
# can decide. A false positive is worse than a missing rule: the first thing anyone
# does with a noisy gate is stop reading it.
#
# Rules are added only from an approved `gate:` proposal, never written ahead.
set -u

cd "$(dirname "$0")/.."
FAILURES=""

fail() { FAILURES="${FAILURES}$1
"; }

# The files this run looks at: the PR's changed files when we can work them out, else the whole tree.
SCOPE_LABEL="changed files"
CHANGED="$(git diff --name-only origin/main...HEAD 2>/dev/null || true)"
if [ -z "$CHANGED" ]; then
    SCOPE_LABEL="whole tree"
    CHANGED="$(git ls-files)"
fi

# Files that exist on disk, out of a newline-separated list.
existing() { while IFS= read -r f; do [ -n "$f" ] && [ -f "$f" ] && printf '%s\n' "$f"; done; }

# Rule 1 (process rule v25, copied at adoption, #1; rule 3 in Magto/mewgenics-coop): no EXERCISED
# line names a closed issue.
#
# Origin: Magto/agent-workflow#2 (approved 2026-09-24). Every fix PR adds one
# `log_line("EXERCISED", "#<issue> <what ran>")` on the fixed path so a log watcher can tell
# "exercised" from "not exercised"; the line is temporary, and a separate cleanup PR removes it
# once the play check says HOLDS. The issue may not reach Done while its line is live, so a live
# line naming a CLOSED issue means the cleanup was forgotten -- and this rule, run by CI and by
# SHIP_CMD, is what stops it shipping. It is the one rule here written ahead of a gate: proposal,
# because the process that creates EXERCISED lines is also what makes them forgettable.
#
# Whole tree, not scoped to CHANGED: an issue closes without any diff touching the line.
# State comes from `gh issue view <N> --json state`. No gh, or gh cannot answer, fails closed:
# a gate that cannot look up the state has not checked it. With no EXERCISED line in the tree
# gh is never called, so a machine without gh passes as long as there is nothing to look up.
# CI needs a token for gh (GH_TOKEN: github.token, permissions issues: read) before the first
# EXERCISED line lands, or this rule fails closed on every PR.
# Legitimate exception: none for a closed issue -- reopen it (the fix is still being proven) or
# remove the line. An issue that is reopened keeps its line, because it is OPEN again.
EXERCISED_REPO="Magto/ffb-coop"          # LEAD-CONFIG REPO
EXERCISED_PATHS="src tools"             # LEAD-CONFIG SOURCE_PATHS, space-separated
# The project's log call up to the TAG argument, as an ERE. Empty: FFB Co-op.exe has no log call
# yet (docs/SPEC.md), so any `"EXERCISED", "#N` pair counts. Narrow it once one exists.
EXERCISED_CALL=''
check_no_exercised_for_closed_issue() {
    hits="$(git grep -nE "${EXERCISED_CALL}\"EXERCISED\",[[:space:]]*\"#[0-9]+" -- $EXERCISED_PATHS 2>/dev/null || true)"
    [ -n "$hits" ] || return 0
    have_gh=1
    command -v gh >/dev/null 2>&1 || have_gh=0
    printf '%s\n' "$hits" | while IFS= read -r hit; do
        f="${hit%%:*}"; rest="${hit#*:}"; line="${rest%%:*}"
        n="$(printf '%s' "$rest" | sed -nE 's/.*"EXERCISED",[[:space:]]*"#([0-9]+).*/\1/p')"
        if [ "$have_gh" = "0" ]; then
            printf '%s:%s: EXERCISED line for #%s, and gh is not installed -- cannot look up whether the issue is closed; this rule fails closed\n' "$f" "$line" "$n"
            continue
        fi
        state="$(gh issue view "$n" -R "$EXERCISED_REPO" --json state --jq .state 2>/dev/null)"
        case "$state" in
            OPEN) ;;
            CLOSED) printf '%s:%s: EXERCISED line for #%s, which is CLOSED -- the cleanup PR that removes it after the play check was forgotten (agent-workflow#2); remove the line, or reopen the issue if the fix is still being proven\n' "$f" "$line" "$n" ;;
            *) printf '%s:%s: EXERCISED line for #%s, and `gh issue view %s` gave no state (not logged in, offline, or no such issue) -- this rule fails closed\n' "$f" "$line" "$n" "$n" ;;
        esac
    done
}

# Add one shell function per rule, numbered, printing `path:line: reason` and nothing
# when it passes. Scope a text rule to $CHANGED; scope an invariant about one file to
# that file. Then name the function in the loop below.

for check in \
    check_no_exercised_for_closed_issue
do
    out="$($check)"
    [ -n "$out" ] && fail "$out"
done

if [ -n "$FAILURES" ]; then
    printf '%s' "$FAILURES"
    printf 'scan_rules: FAILED (scope: %s)\n' "$SCOPE_LABEL"
    exit 1
fi
printf 'scan_rules: all checks passed (scope: %s)\n' "$SCOPE_LABEL"
