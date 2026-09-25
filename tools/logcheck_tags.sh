#!/usr/bin/env bash
# Every TAG an open issue's Log check field names exists in the source. Run it: tools/logcheck_tags.sh
# Copy to the path in LOGCHECK_TAGS_CMD and set REPO and SOURCE_PATHS below from LEAD-CONFIG.
#
# Origin: process v25, Magto/agent-workflow#2 (approved 2026-09-24). Every issue form carries a
# Log check field -- `Symptom: TAG text fragment` / `Fixed: TAG text fragment`, or
# `manual only: <what a person looks at>` -- and a log watcher greps for exactly those lines.
# A TAG renamed in the source after the issue was written makes the watcher report "not
# exercised" on a fix that ran; this catches the drift before a play session does. The first
# copy is Magto/mewgenics-coop tools/logcheck_tags.sh.
#
# For every OPEN issue whose body has a `### Log check` section, each Symptom/Fixed line's
# TAG -- the first word after the prefix, backticks allowed, or the TAG column of a pasted
# `SEQ FRAME HH:MM:SS.mmm TAG text` log line -- must appear as a quoted string literal
# ("TAG") somewhere in SOURCE_PATHS. A prefix on a line of its own takes the bullet lines
# under it. `manual only` lines are skipped.
#
# EXERCISED is not looked up: the fix PR adds it by rule, so it never exists before the fix
# does. The closed-issue rule in scan_rules.sh polices EXERCISED lines instead.
#
# Not a ship gate: it reads issue bodies, not what is going out, so a typo in an unrelated
# open issue must not refuse a release. The lead runs it before moving a card to Ready.
#
# Prints `#N: TAG not found` (or `#N: no TAG in "<line>"`) per failure and exits 1 on any.
# An issue has no path and no line, so `#N` stands where `path:line` would.
# Needs gh; without it, or when the issue list cannot be read, it exits 2 -- fails closed.
set -u

cd "$(dirname "$0")/.."
REPO="Magto/ffb-coop"          # LEAD-CONFIG REPO
SOURCE_PATHS="src tools"      # LEAD-CONFIG SOURCE_PATHS, space-separated, no trailing slash

command -v gh >/dev/null 2>&1 || { echo "logcheck_tags: gh is not installed -- cannot read the open issues; failing closed"; exit 2; }
issues="$(gh issue list -R "$REPO" --state open --limit 1000 --json number --jq '.[].number' 2>/dev/null)" \
    || { echo "logcheck_tags: gh issue list failed (not logged in, or offline) -- failing closed"; exit 2; }

# The Log check section of an issue body: from `### Log check` to the next `### ` heading.
# Prints one "TAG<US>line" (US = \037, not a tab: read collapses a leading empty tab field) per Symptom/Fixed entry; TAG is empty when none could be read.
extract_tags() {
    tr -d '\r' | awk '
        /^###[[:space:]]+Log check[[:space:]]*$/ { in_sec = 1; mode = ""; next }
        /^###[[:space:]]/                        { in_sec = 0 }
        !in_sec                                  { next }
        {
            line = $0
            sub(/^[[:space:]]*([-*][[:space:]]+)?/, "", line)
            if (line == "" || line ~ /^_No response_$/) next
            if (tolower(line) ~ /^manual only/) { mode = ""; next }
            if (match(tolower(line), /^(symptoms?|fixed)[[:space:]]*:[[:space:]]*/)) {
                line = substr(line, RLENGTH + 1)
                mode = "on"
                if (line == "") next
            } else if (mode == "" || $0 !~ /^[[:space:]]*[-*][[:space:]]/) {
                mode = ""; next
            }
            raw = line
            gsub(/`/, "", line)
            # A pasted log line: SEQ FRAME HH:MM:SS.mmm TAG text
            if (match(line, /^[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9][0-9]:[0-9][0-9]:[0-9][0-9]\.[0-9]+[[:space:]]+/))
                line = substr(line, RLENGTH + 1)
            tag = ""
            if (match(line, /^[A-Z][A-Z0-9_]*/)) tag = substr(line, 1, RLENGTH)
            if (length(tag) < 2) tag = ""
            printf "%s\037%s\n", tag, raw
        }'
}

FAILURES=""
checked=0
for n in $issues; do
    body="$(gh issue view "$n" -R "$REPO" --json body --jq .body 2>/dev/null)" \
        || { FAILURES="${FAILURES}#${n}: could not read the issue body
"; continue; }
    printf '%s\n' "$body" | grep -qE '^###[[:space:]]+Log check[[:space:]]*$' || continue
    checked=$((checked + 1))
    while IFS="$(printf '\037')" read -r tag raw; do
        [ -n "$tag$raw" ] || continue
        if [ -z "$tag" ]; then
            FAILURES="${FAILURES}#${n}: no TAG in \"${raw}\" -- start the entry with the TAG as it is logged
"
            continue
        fi
        [ "$tag" = "EXERCISED" ] && continue
        git grep -qF "\"${tag}\"" -- $SOURCE_PATHS || FAILURES="${FAILURES}#${n}: ${tag} not found
"
    done <<EOF
$(printf '%s\n' "$body" | extract_tags)
EOF
done

if [ -n "$FAILURES" ]; then
    printf '%s' "$FAILURES"
    printf 'logcheck_tags: FAILED (%s open issue(s) with a Log check)\n' "$checked"
    exit 1
fi
printf 'logcheck_tags: all tags found (%s open issue(s) with a Log check)\n' "$checked"
