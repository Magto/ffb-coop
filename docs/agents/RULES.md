# RULES — the rules that generalise

The failure classes this project keeps paying for. Read this file before touching `src/`, `tools/`.
Findings cite a rule by number (`rule: 3`) or propose a new one (`rule: proposed <kebab-tag>`);
a tag under `## Rejected proposals` has already been ruled on and is not to be flagged again.
Only Martin adds, retires or rejects a rule; nothing here is edited by a worker on its
own.

## Rules that generalise

Each of these was learned the expensive way and applies to work that has not been done yet.

<!-- One numbered entry each: the bold sentence first, then the evidence (PR numbers, what it
     cost), then the mechanical check if one exists. Evidence before opinion, always. -->

**A rule with a gate says so.** When a rule here is enforced by a check, its entry ends with
`— gated by scan_rules.sh rule N` (or the doc-rules rule that covers it). The sentence stays, so the
reason survives; what stops is anyone having to hold it in their head.

## Kept practices

One line each, `tag — one sentence — exemplar PR #n`. Adopted from a `practice-proposal` the miner
filed and Martin approved.

## Retired

Practices moved here with the date they were retired.

## Rejected proposals

`tag — reason — date`. A reviewer must not flag a case listed here.
