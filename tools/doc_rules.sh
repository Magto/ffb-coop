#!/usr/bin/env bash
# Mechanical documentation checks. Run it before you hand over: tools/doc_rules.sh
# Every failure prints as `path:line: reason`; all rules run, then the script exits 1
# if any of them failed. No judgement rules live here -- only what a machine can decide.
# Copied from Magto/mewgenics-coop tools/doc_rules.sh at adoption (#1); its docs/history/ rule is
# dropped, since this repo has no docs/history/.
set -u

cd "$(dirname "$0")/.."
FAILURES=""

fail() { FAILURES="${FAILURES}$1
"; }

# The files this run looks at: the PR's changed files when we can work them out, else the whole tree.
SCOPE_LABEL="changed files"
CHANGED="$(git diff --name-only origin/main...HEAD 2>/dev/null || true)"
ADDED="$(git diff --name-only --diff-filter=A origin/main...HEAD 2>/dev/null || true)"
if [ -z "$CHANGED" ]; then
    SCOPE_LABEL="whole tree"
    CHANGED="$(git ls-files)"
    ADDED="$CHANGED"
fi

# Files that exist on disk, out of a newline-separated list.
existing() { while IFS= read -r f; do [ -n "$f" ] && [ -f "$f" ] && printf '%s\n' "$f"; done; }

# The doc files rules 2 and 4 police: CLAUDE.md, README.md, docs/SPEC.md, docs/agents/*.md, docs/reference/*.md.
prose_docs() {
    printf '%s\n' "$CHANGED" | grep -E '^(CLAUDE\.md|README\.md|docs/SPEC\.md|docs/agents/[^/]+\.md|docs/reference/[^/]+\.md)$' | existing
}

# Rule 1: no session-notes file under docs/ (START.md, "Findings are issues, never files").
check_no_session_notes() {
    printf '%s\n' "$ADDED" \
        | grep -E '^docs/' \
        | grep -E '/(HANDOFF|SESSION|RESEARCH|REPORT|FINDINGS|HANDOVER)-' \
        | while IFS= read -r f; do
            printf '%s:1: session-notes file under docs/ -- findings are issues, never files\n' "$f"
        done
}

# Rule 2: no line-number citations in the prose docs (START.md step 3, "cite symbols, never line numbers").
check_no_line_numbers() {
    prose_docs | while IFS= read -r f; do
        awk -v f="$f" '
            /^[[:space:]]*(```|~~~)/ { fence = !fence; next }
            fence { next }
            /[A-Za-z0-9_.\/-]+\.(h|cpp|c|py|ps1|md):[0-9]+/ {
                printf "%s:%d: line-number citation -- cite the symbol, not the line: %s\n", f, NR, $0
            }
        ' "$f"
    done
}

# Rule 3: CLAUDE.md stays short enough to be read every session (<= 160 lines).
check_claude_md_length() {
    n=$(wc -l < CLAUDE.md | tr -d ' ')
    if [ "$n" -gt 160 ]; then
        printf 'CLAUDE.md:%d: CLAUDE.md is %d lines, the limit is 160 -- move detail into docs/\n' "$n" "$n"
    fi
}

# Rule 4: every docs/... .md path named in the prose docs actually exists.
check_doc_links_resolve() {
    prose_docs | while IFS= read -r f; do
        grep -n -oE 'docs/[A-Za-z0-9_./-]+\.md(#[A-Za-z0-9_-]+)?' "$f" | while IFS= read -r hit; do
            line="${hit%%:*}"
            path="${hit#*:}"
            path="${path%%#*}"
            [ -f "$path" ] || printf '%s:%s: references %s, which does not exist\n' "$f" "$line" "$path"
        done
    done
}

# Rule 5: every BY-DESIGN.md table row cites the issue it was closed from (START.md, "Triage is not yours").
check_by_design_rows_cite_issues() {
    [ -f docs/agents/BY-DESIGN.md ] || return 0
    awk -v f=docs/agents/BY-DESIGN.md '
        /^\|[ -:|]*\|$/ { body = 1; next }
        body && /^\|/ && !/#[0-9]+/ {
            printf "%s:%d: BY-DESIGN row has no issue reference (#N): %s\n", f, NR, $0
        }
    ' docs/agents/BY-DESIGN.md
}

for check in \
    check_no_session_notes \
    check_no_line_numbers \
    check_claude_md_length \
    check_doc_links_resolve \
    check_by_design_rows_cite_issues
do
    out="$($check)"
    [ -n "$out" ] && fail "$out"
done

if [ -n "$FAILURES" ]; then
    printf '%s' "$FAILURES"
    printf 'doc_rules: FAILED (scope: %s)\n' "$SCOPE_LABEL"
    exit 1
fi
printf 'doc_rules: all checks passed (scope: %s)\n' "$SCOPE_LABEL"
