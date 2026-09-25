#!/usr/bin/env bash
# Process metrics. Run it weekly: tools/metrics.sh <owner/repo> [days]
#
# Every number here is a `gh` query you can read in this file. Nothing comes from session
# data, nothing is estimated. Where the API cannot answer, the number is `?` and the table
# says why in one word -- a `?` is a fact about the API, not a gap to fill with a guess.
#
# Output: a table for the last <days> and the <days> before it, then one fixed-shape line
# a later reader can grep:
#
#   metrics <date>: merged n (Δ) · rounds/PR x.x (Δ) · Ready→merge median hh (Δ) · ...
#
# Config through the environment (the lead's LEAD-CONFIG holds the values):
#   PROJECT_OWNER, PROJECT_NUMBER  the board, for the Triage and stale counts
#   STALE_HOURS                    a card with nothing newer is stale (default 24)
#   TRIAGE_LABEL                   default status/triage
#   BYDESIGN_LABEL                 default status/by-design
#   READY_LABEL                    default status/ready
#   ESCALATION_PATTERN             matched case-insensitively against branch and body
set -u

REPO="${1:-}"
DAYS="${2:-7}"
if [ -z "$REPO" ]; then
    echo "usage: $(basename "$0") <owner/repo> [days]" >&2
    exit 2
fi

PROJECT_OWNER="${PROJECT_OWNER:-}"
PROJECT_NUMBER="${PROJECT_NUMBER:-}"
STALE_HOURS="${STALE_HOURS:-24}"
TRIAGE_LABEL="${TRIAGE_LABEL:-status/triage}"
BYDESIGN_LABEL="${BYDESIGN_LABEL:-status/by-design}"
READY_LABEL="${READY_LABEL:-status/ready}"
ESCALATION_PATTERN="${ESCALATION_PATTERN:-escalat}"

command -v gh >/dev/null || { echo "metrics: gh is not on PATH" >&2; exit 2; }
command -v jq >/dev/null || { echo "metrics: jq is not on PATH" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# --- windows ------------------------------------------------------------------------
iso_ago() {  # days -> UTC ISO8601; BSD date on the Mac, GNU date elsewhere
    if date -u -v-1d +%Y >/dev/null 2>&1; then
        date -u -v-"$1"d +%Y-%m-%dT%H:%M:%SZ
    else
        date -u -d "$1 days ago" +%Y-%m-%dT%H:%M:%SZ
    fi
}
NOW="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
TODAY="$(date -u +%Y-%m-%d)"
A_START="$(iso_ago "$DAYS")"                 # this window: [A_START, NOW)
B_START="$(iso_ago $((DAYS * 2)))"           # the one before: [B_START, A_START)
STALE_BEFORE="$(iso_ago 0)"
if date -u -v-1d +%Y >/dev/null 2>&1; then
    STALE_BEFORE="$(date -u -v-"$STALE_HOURS"H +%Y-%m-%dT%H:%M:%SZ)"
else
    STALE_BEFORE="$(date -u -d "$STALE_HOURS hours ago" +%Y-%m-%dT%H:%M:%SZ)"
fi

# --- raw pulls ----------------------------------------------------------------------
gh pr list -R "$REPO" --state all --limit 300 \
   --json number,title,body,state,createdAt,mergedAt,headRefName,closingIssuesReferences,comments,reviews \
   > "$TMP/prs.json" || exit 1

# The PRs either window can touch: created or merged since B_START.
jq -r --arg b "$B_START" '[.[] | select(.createdAt >= $b or (.mergedAt // "") >= $b)] | .[].number' \
   "$TMP/prs.json" > "$TMP/window_prs.txt"

# --- per-PR facts -------------------------------------------------------------------
# One record per PR in the two windows, with the things the list JSON cannot give:
# the Ready timestamp (issue timeline), the inline review comments, the runner's revert verdict.
: > "$TMP/facts.ndjson"
while read -r N; do
    [ -n "$N" ] || continue
    PR="$(jq --argjson n "$N" '.[] | select(.number == $n)' "$TMP/prs.json")"

    ISSUE="$(printf '%s' "$PR" | jq -r '.closingIssuesReferences[0].number // empty')"
    if [ -z "$ISSUE" ]; then
        ISSUE="$(printf '%s' "$PR" | jq -r '.headRefName' | sed -n 's/.*issue[-_]\{0,1\}\([0-9][0-9]*\).*/\1/p')"
    fi

    READY_AT=""; READY_SRC="none"; ISSUE_AT=""
    if [ -n "$ISSUE" ]; then
        gh api "repos/$REPO/issues/$ISSUE/timeline" --paginate \
            > "$TMP/tl.json" 2>/dev/null || echo '[]' > "$TMP/tl.json"
        READY_AT="$(jq -r --arg l "$READY_LABEL" \
            '[.[] | select(.event == "labeled" and .label.name == $l) | .created_at] | last // empty' \
            "$TMP/tl.json")"
        [ -n "$READY_AT" ] && READY_SRC="label"
        ISSUE_AT="$(gh api "repos/$REPO/issues/$ISSUE" --jq '.created_at' 2>/dev/null || true)"
        if [ -z "$READY_AT" ] && [ -n "$ISSUE_AT" ]; then
            READY_AT="$ISSUE_AT"; READY_SRC="issue-open"
        fi
    fi

    gh api "repos/$REPO/pulls/$N/comments" --paginate --jq '[.[].body] | join("\n")' \
        > "$TMP/inline.txt" 2>/dev/null || : > "$TMP/inline.txt"

    printf '%s' "$PR" | jq \
        --rawfile inline "$TMP/inline.txt" \
        --arg ready "$READY_AT" --arg readysrc "$READY_SRC" \
        --arg esc "$ESCALATION_PATTERN" '
      def reviewbodies: [.reviews[]?.body // ""];
      def commentbodies: [.comments[]?.body // ""];
      # A review by the reviewer briefs: a body headed "## ... review ...".
      def isreview: test("(?i)^\\s*##[^\\n]*review");
      def approved_idx:
        ( [ reviewbodies | to_entries[] | select(.value | test("(?i)\\*\\*approved\\*\\*|verdict:\\s*approved")) | .key ] | first );
      . as $pr
      | (reviewbodies) as $rv
      | (approved_idx) as $ai
      | ( if $ai == null then [ $rv[] | select(isreview) ] | length
          else [ $rv[0:$ai][] | select(isreview) ] | length end ) as $rounds
      | ( ($rv + commentbodies + [$inline]) | join("\n") ) as $alltext
      # The reviewer briefs put the tag on its own line: `rule: 17`, `rule: proposed <tag>`,
      # `rule: none`, `keep: <tag> — prevented …`. Anchored to the line start (bullet and bold
      # markers allowed) so prose like "The rule: the claim is dropped" is not a tag.
      | def tag: sub("^proposed\\s+"; "") | split(" ")[0] | ascii_downcase;
        ( [ $alltext | scan("(?im)^[ \\t>*_-]*\\*{0,2}rule:\\s*\\*{0,2}([A-Za-z0-9][A-Za-z0-9 _.-]*)")
            | .[0] | tag ]
          | map(select(. != "none")) | unique ) as $ruletags
      | ( [ $alltext | scan("(?im)^[ \\t>*_-]*\\*{0,2}keep:\\s*\\*{0,2}([A-Za-z0-9][A-Za-z0-9 _.-]*)")
            | .[0] | tag ]
          | unique ) as $keeptags
      | ( [ commentbodies[] | select(test("(?i)^\\s*##\\s*windows evidence")) ] | join("\n") ) as $winev
      | ( ($rv + commentbodies) | join("\n") ) as $revtext
      | {
          number: .number,
          createdAt: .createdAt,
          mergedAt: .mergedAt,
          state: .state,
          branch: .headRefName,
          ready_at: (if $ready == "" then null else $ready end),
          ready_src: $readysrc,
          rounds: $rounds,
          approved: ($ai != null),
          escalated: ((.headRefName + "\n" + (.body // "") + "\n" + $alltext) | test("(?i)" + $esc)),
          evidence: ( (.body // "")
                      | if test("(?i)evidence:[^\\n]*\\bPROVEN\\b") then "PROVEN"
                        elif test("(?i)evidence:[^\\n]*CODE-ONLY") then "CODE-ONLY"
                        elif test("(?i)evidence:[^\\n]*NOT TESTED") then "NOT TESTED"
                        else null end ),
          revert: ( ($winev + "\n" + $revtext)
                    | if test("(?i)\"result\"\\s*:\\s*\"(FAIL|COMPILE-FAIL)\"") then "caught"
                      elif test("(?i)\"result\"\\s*:\\s*\"PASS\"") then "passed"
                      else null end ),
          has_windows_evidence: ($winev != ""),
          rule_tags: $ruletags,
          keep_tags: $keeptags,
          has_class: ((.body // "") | test("(?m)^##\\s*Class\\b")),
          class_spilled: ((.body // "") | test("(?i)filed as #[0-9]"))
        }' >> "$TMP/facts.ndjson"
done < "$TMP/window_prs.txt"
jq -s '.' "$TMP/facts.ndjson" > "$TMP/facts.json"

# --- issues -------------------------------------------------------------------------
gh issue list -R "$REPO" --state all --limit 500 \
   --json number,title,body,createdAt,closedAt,state,stateReason,labels,comments \
   > "$TMP/issues.json" || exit 1

# --- board --------------------------------------------------------------------------
BOARD_OK=0
echo 'null' > "$TMP/board.json"
if [ -n "$PROJECT_OWNER" ] && [ -n "$PROJECT_NUMBER" ]; then
    if gh api graphql -f owner="$PROJECT_OWNER" -F number="$PROJECT_NUMBER" -f query='
      query($owner:String!, $number:Int!) {
        user(login:$owner) { projectV2(number:$number) {
          items(first:100) {
            totalCount
            nodes {
              updatedAt
              fieldValueByName(name:"Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
              content { __typename
                ... on Issue { number updatedAt state }
                ... on PullRequest { number updatedAt state } }
            } } } } }' > "$TMP/board.json" 2>/dev/null \
       && [ "$(jq -r '.data.user.projectV2.items.nodes | length' "$TMP/board.json" 2>/dev/null || echo null)" != "null" ]; then
        BOARD_OK=1
    else
        echo 'null' > "$TMP/board.json"
    fi
fi

# --- the numbers --------------------------------------------------------------------
# Every count below is one jq over the JSON fetched above. `win` picks a window by merge
# or creation date; nothing else decides membership.
jq -n \
  --slurpfile facts "$TMP/facts.json" \
  --slurpfile issues "$TMP/issues.json" \
  --arg board_ok "$BOARD_OK" \
  --arg a "$A_START" --arg b "$B_START" --arg now "$NOW" --arg today "$TODAY" \
  --arg stale_before "$STALE_BEFORE" --arg stale_hours "$STALE_HOURS" \
  --arg days "$DAYS" --arg repo "$REPO" \
  --arg triage "$TRIAGE_LABEL" --arg bydesign "$BYDESIGN_LABEL" \
  --slurpfile board_in "$TMP/board.json" '
  ($facts[0]) as $F | ($issues[0]) as $I |
  def med($xs): ($xs | sort) as $s | ($s | length) as $n |
      if $n == 0 then null
      elif $n % 2 == 1 then $s[($n-1)/2]
      else (($s[$n/2 - 1] + $s[$n/2]) / 2) end;
  def hours($from; $to): (($to | fromdateiso8601) - ($from | fromdateiso8601)) / 3600;

  def merged_in($lo; $hi): [ $F[] | select(.mergedAt != null and .mergedAt >= $lo and .mergedAt < $hi) ];
  def opened_in($lo; $hi): [ $F[] | select(.createdAt >= $lo and .createdAt < $hi) ];

  def stats($lo; $hi):
    (merged_in($lo; $hi)) as $m
    | {
      merged: ($m | length),
      opened: (opened_in($lo; $hi) | length),
      ready_median: med([ $m[] | select(.ready_at != null) | hours(.ready_at; .mergedAt) ]),
      ready_n: ([ $m[] | select(.ready_at != null) ] | length),
      ready_label_n: ([ $m[] | select(.ready_src == "label") ] | length),
      ready_fallback_n: ([ $m[] | select(.ready_src == "issue-open") ] | length),
      rounds_avg: (if ($m | length) == 0 then null
                   else (([ $m[] | .rounds ] | add) / ($m | length)) end),
      escalated: ([ $m[] | select(.escalated) ] | length),
      revert_caught: ([ $m[] | select(.revert == "caught") ] | length),
      revert_passed: ([ $m[] | select(.revert == "passed") ] | length),
      revert_none: ([ $m[] | select(.revert == null) ] | length),
      proven: ([ $m[] | select(.evidence == "PROVEN") ] | length),
      codeonly: ([ $m[] | select(.evidence == "CODE-ONLY") ] | length),
      nottested: ([ $m[] | select(.evidence == "NOT TESTED") ] | length),
      noevidence: ([ $m[] | select(.evidence == null) ] | length),
      rule_total: ([ $m[] | .rule_tags | length ] | add // 0),
      rule_distinct: ([ $m[] | .rule_tags[] ] | unique | length),
      keep_total: ([ $m[] | .keep_tags | length ] | add // 0),
      keep_distinct: ([ $m[] | .keep_tags[] ] | unique | length),
      class_sections: ([ $m[] | select(.has_class) ] | length),
      class_spilled: ([ $m[] | select(.has_class and .class_spilled) ] | length),
      issues_filed: ([ $I[] | select(.createdAt >= $lo and .createdAt < $hi) ] | length),
      issues_closed: ([ $I[] | select(.closedAt != null and .closedAt >= $lo and .closedAt < $hi) ] | length),
      issues_dup: ([ $I[] | select(.closedAt != null and .closedAt >= $lo and .closedAt < $hi)
                     | select(((.comments // []) | map(.body) | join("\n")) + "\n" + (.body // "")
                              | test("(?i)duplicate of #[0-9]")) ] | length),
      issues_bydesign: ([ $I[] | select(.closedAt != null and .closedAt >= $lo and .closedAt < $hi)
                          | select((.labels // []) | map(.name) | index($bydesign)) ] | length),
      proposals: ([ $I[] | select(.createdAt >= $lo and .createdAt < $hi)
                    | select((.labels // []) | map(.name)
                             | (index("rule-proposal") != null or index("practice-proposal") != null)) ] | length)
    };

  (stats($a; $now)) as $A | (stats($b; $a)) as $B |
  ($board_in[0] // null) as $board |
  (if $board_ok == "1" and $board != null
   then ([ $board.data.user.projectV2.items.nodes[]
           | select((.fieldValueByName.name // "") | ascii_downcase | test("triage")) ] | length)
   else ([ $I[] | select(.state == "OPEN")
           | select((.labels // []) | map(.name) | index($triage)) ] | length) end) as $triage_n |
  (if $board_ok == "1" and $board != null
   then ([ $board.data.user.projectV2.items.nodes[]
           | select((.fieldValueByName.name // "") | ascii_downcase
                    | test("in progress|testing|ready"))
           | select(((.content.updatedAt // .updatedAt)) < $stale_before) ] | length)
   else null end) as $stale_n |
  (if $board_ok == "1" and $board != null
   then ($board.data.user.projectV2.items.totalCount > 100) else false end) as $board_trunc |
  ($board_trunc) as $board_trunc |
  (if $board_trunc then null else $stale_n end) as $stale_n |
  {a: $A, b: $B, triage: (if $board_trunc then null else $triage_n end),
   board_trunc: $board_trunc,
   triage_src: (if $board_ok == "1" and $board != null then "board" else "label" end),
   stale: $stale_n, days: ($days | tonumber), repo: $repo, today: $today,
   window_a: $a, window_b: $b, now: $now, stale_hours: $stale_hours,
   board_ok: ($board_ok == "1")}
' > "$TMP/out.json"

# --- print --------------------------------------------------------------------------
jq -r '
  def n(x): if x == null then "?" else (x | tostring) end;
  def one(x): if x == null then "?" else ((x * 10 | round) / 10 | tostring) end;
  def delta(cur; prev):
    if cur == null or prev == null then "(?)"
    elif (cur - prev) == 0 then "(=)"
    elif (cur - prev) > 0 then "(+" + (((cur - prev) * 10 | round) / 10 | tostring) + ")"
    else "(−" + (((prev - cur) * 10 | round) / 10 | tostring) + ")" end;
  .a as $A | .b as $B |
  "metrics \(.repo) · last \(.days) days (\(.window_a) → \(.now)) vs the \(.days) before",
  "",
  "| Number | last \(.days)d | prior \(.days)d | how it is counted |",
  "|---|---|---|---|",
  "| PRs merged | \(n($A.merged)) | \(n($B.merged)) | `gh pr list` mergedAt in the window |",
  "| PRs opened | \(n($A.opened)) | \(n($B.opened)) | createdAt in the window |",
  "| Ready→merge median (h) | \(one($A.ready_median)) | \(one($B.ready_median)) | issue timeline `labeled status/ready` (\($A.ready_label_n) PRs); issue-open fallback (\($A.ready_fallback_n)); no linked issue = excluded |",
  "| Review rounds / merged PR | \(one($A.rounds_avg)) | \(one($B.rounds_avg)) | review bodies headed `## … review` before the first **Approved** |",
  "| PRs that needed escalation | \(n($A.escalated)) | \(n($B.escalated)) | branch, body, reviews or comments matching /escalat/i — session names are not in GitHub data and the process names no escalation session, so this is a floor, not a count |",
  "| Revert check caught (FAIL/COMPILE-FAIL) | \(n($A.revert_caught)) | \(n($B.revert_caught)) | `revert.result` in the `## Windows evidence` sentinel |",
  "| Revert check failed to fail (PASS) | \(n($A.revert_passed)) | \(n($B.revert_passed)) | same field, `PASS` — the case the process exists to catch |",
  "| Merged PRs with no revert verdict | \(n($A.revert_none)) | \(n($B.revert_none)) | no `## Windows evidence` comment (docs-only PRs are n/a) |",
  "| Evidence: PROVEN | \(n($A.proven)) | \(n($B.proven)) | `Evidence:` line in the PR body |",
  "| Evidence: CODE-ONLY | \(n($A.codeonly)) | \(n($B.codeonly)) | same line |",
  "| Evidence: NOT TESTED | \(n($A.nottested)) | \(n($B.nottested)) | same line |",
  "| Merged PRs with no Evidence: line | \(n($A.noevidence)) | \(n($B.noevidence)) | the line is process v20 and older PRs predate it |",
  "| `rule:` tags (total / distinct) | \(n($A.rule_total)) / \(n($A.rule_distinct)) | \(n($B.rule_total)) / \(n($B.rule_distinct)) | review bodies, PR comments and inline comments; one tag counted once per PR, `rule: none` dropped |",
  "| `keep:` tags (total / distinct) | \(n($A.keep_total)) / \(n($A.keep_distinct)) | \(n($B.keep_total)) / \(n($B.keep_distinct)) | same sources, same dedupe |",
  "| Proposals filed | \(n($A.proposals)) | \(n($B.proposals)) | issues labelled `rule-proposal` or `practice-proposal` |",
  "| Issues filed / closed | \(n($A.issues_filed)) / \(n($A.issues_closed)) | \(n($B.issues_filed)) / \(n($B.issues_closed)) | `gh issue list` createdAt / closedAt |",
  "| …closed as duplicate | \(n($A.issues_dup)) | \(n($B.issues_dup)) | a closing comment or the body saying `duplicate of #N` |",
  "| …closed as by-design | \(n($A.issues_bydesign)) | \(n($B.issues_bydesign)) | the `status/by-design` label |",
  "| `## Class` sections (of them, spilled) | \(n($A.class_sections)) / \(n($A.class_spilled)) | \(n($B.class_sections)) / \(n($B.class_spilled)) | a `## Class` heading; spilled = its Disposition says `filed as #N`, the only machine-readable sign the search found more than one occurrence |",
  "| Triage size (now) | \(n(.triage)) | — | \(if .board_trunc then "? truncated — the board has more than the 100 items one query returns" elif .triage_src == "board" then "board Status = Triage" else "open issues labelled status/triage — PROJECT_OWNER/PROJECT_NUMBER not set, so the board was not read" end) |",
  "| Stale cards (now) | \(n(.stale)) | — | \(if .board_trunc then "? truncated — more than 100 board items" elif .stale == null then "? board-unset — needs PROJECT_OWNER and PROJECT_NUMBER" else "board cards in Ready/In progress/Testing with nothing newer than \(.stale_hours) h; session liveness is not visible to the API, so a live worker on an old card counts as stale" end) |",
  "",
  "metrics \(.today): merged \(n($A.merged)) \(delta($A.merged; $B.merged)) · rounds/PR \(one($A.rounds_avg)) \(delta($A.rounds_avg; $B.rounds_avg)) · Ready→merge median \(one($A.ready_median))h \(delta($A.ready_median; $B.ready_median)) · PROVEN \(n($A.proven))/CODE-ONLY \(n($A.codeonly))/NOT TESTED \(n($A.nottested)) · rule tags \(n($A.rule_total)) · keep tags \(n($A.keep_total)) · issues +\(n($A.issues_filed))/−\(n($A.issues_closed)) · stale \(n(.stale))"
' "$TMP/out.json"
