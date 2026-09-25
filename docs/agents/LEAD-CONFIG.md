# LEAD-CONFIG — the project facts the shared process needs

The process this project runs on lives in `Magto/agent-workflow`: the `am-lead` and `am-worker`
skills, the reviewer, test-reviewer, miner and steward briefs, and the optional modules. Those
files name no project. Every `<KEY>` they use is a value in this file.

**A value that is not here is a stop, not a guess.** A session that needs a key it cannot find
says which key it needs and stops. Nobody infers a board id, a label name or a command from the
tree. A row that reads `unknown — needs Martin` is a stop too, until Martin fills it.

Adopted 2026-09-25 (#1), modelled on `Magto/mewgenics-coop`'s config.

## Repo

| Key | Value |
|---|---|
| `REPO` | `Magto/ffb-coop` |
| `DEFAULT_BRANCH` | `main` |
| `MAC_CLONE` | `none` — no Mac clone (Martin, 2026-09-25, PR #13) |
| `WIN_CLONE` | `C:\Users\marti\claude-project\ffb-coop` |
| `SESSION_GROUP` | `ffb-coop` — the agent-manager group does not exist yet (2026-09-25); the lead creates it (directory `/home/marti/claude-project/ffb-coop`, worktree on) before the next spawn |

## Board

| Key | Value |
|---|---|
| `BOARD_NUMBER` | `10` (FFB Co-op) |
| `BOARD_OWNER` | `Magto` |
| `BOARD_PROJECT_ID` | `PVT_kwHOAPRvas4BkpXy` |
| `STATUS_FIELD_ID` | `PVTSSF_lAHOAPRvas4BkpXyzhjZPTc` |
| `STALE_HOURS` | `24` — a card with no live session and nothing newer is stale |
| `COLUMNS` | in order: Triage `ed65a4a7` · Ready `8924c3ea` · In progress `15317a18` · Testing on Windows `dcd03263` · Done `add91a45` |

## Intake

| Key | Value |
|---|---|
| `ISSUE_FORMS_DIR` | `.github/ISSUE_TEMPLATE/` (`bug.yml`, `feature.yml`, `finding.yml`; blank issues disabled; `tools/issue_forms_check.py` checks them) |
| `ISSUE_FORM_HEADINGS` | Summary · Repro and evidence / What and why / What was observed, and where · Done looks like · Log check · Out of scope (not on a finding) · FFB Co-op version · Files or symbols involved · Read these · Needs game test · Proposed resolution (findings) |
| `TYPE_LABELS` | `type/bug`, `type/feature`, `type/finding` — exactly one per issue |
| `AREA_LABELS` | `area/launcher`, `area/update`, `area/server`, `area/tooling`, `area/docs` — exactly one per issue |
| `STATUS_LABELS` | `status/triage`, `status/ready`, `status/by-design`, `status/needs-game-test` |

## Tracking issues

GitHub pins at most three issues per repository, so `Metrics log` is on the board but not pinned.

| Key | Value |
|---|---|
| `STEWARD_LOG_TITLE` | `Steward log` (issue #8, pinned) |
| `MINER_LOG_TITLE` | `Miner log` (issue #9, pinned) |
| `MAIN_BUILDS_TITLE` | `Main builds` (issue #10, pinned) |
| `METRICS_LOG_TITLE` | `Metrics log` (issue #12, not pinned — see above) |
| `MINER_INTERVAL_DAYS` | `10` |
| `METRICS_INTERVAL_DAYS` | `7` |

## Sprint

This project runs continuous flow: `SPRINTS` is `off`, so there is no sprint goal, no sprint issue
and no not-this list, and the Ready bar keeps its plain shape. The other three keys are unused
until it is turned on.

| Key | Value |
|---|---|
| `SPRINTS` | `off` (continuous flow) |
| `SPRINT_LENGTH_DAYS` | n/a |
| `SPRINT_ISSUE_TITLE` | n/a |
| `SPRINT_LABEL` | n/a |

## Docs

| Key | Value |
|---|---|
| `RULES_FILE` | `docs/agents/RULES.md` |
| `START_FILE` | `docs/agents/START.md` |
| `BY_DESIGN_FILE` | `docs/agents/BY-DESIGN.md` |
| `TESTING_FILE` | `docs/agents/TESTING.md` |
| `CHANGELOG_FILE` | `docs/CHANGELOG.md` |
| `BEHAVIOUR_LOG_FILE` | `docs/BEHAVIOUR-LOG.md` |
| `SPEC_FILE` | `docs/SPEC.md` — what FFB Co-op.exe does; not a process key, listed so nobody looks for it elsewhere |
| `CLASS_RULE` | `none yet` — `RULES.md` is empty; only `VERDICT_OWNER` adds a rule (Martin, 2026-09-25, PR #13) |

## Gates and commands

| Key | Value |
|---|---|
| `DOC_RULES_CMD` | `tools/doc_rules.sh` |
| `SCAN_RULES_CMD` | `tools/scan_rules.sh` |
| `METRICS_CMD` | `tools/metrics.sh` — run it as `PROJECT_OWNER=Magto PROJECT_NUMBER=10 STALE_HOURS=24 tools/metrics.sh Magto/ffb-coop 7` |
| `LOGCHECK_TAGS_CMD` | `tools/logcheck_tags.sh` — every TAG an open issue's Log check field names exists in `SOURCE_PATHS`; `#N: TAG not found`, exit 1 on any, exit 2 without `gh`. Not a ship gate. The closed-issue `EXERCISED #N` check is rule 1 of `SCAN_RULES_CMD` |
| `DOC_RULES_CHECK` | `doc-rules` — the one check name (`.github/workflows/doc-rules.yml`); it runs both scripts and `tools/issue_forms_check.py` |
| `SHIP_CMD` | `tools/publish.py` (added by #7; Martin, 2026-09-25, PR #13) — it must run `DOC_RULES_CMD` and `SCAN_RULES_CMD` and refuse on a non-zero exit |
| `BUILD_CMD` | `cmake --build build --config Release` (from #2's Done looks like) — `cmake` is not on the Git Bash PATH on the build machine; use the BuildTools copy under `Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/` |
| `TEST_CMD` | `ctest --test-dir build -C Release [-R <test>]`, same BuildTools directory |
| `TEST_LIST_CMD` | `ctest --test-dir build -C Release -N` — the "Total Tests:" count |
| `LOOPBACK_CMD` | `none` — FFB Co-op.exe has no two-peer behaviour; the spec's tests are unit and by hand (`docs/SPEC.md`, Testing) |
| `LOOPBACK_MANUAL_STEPS` | n/a |
| `TEST_TIERS` | `code`, `manual` — lowest that can prove the fix |
| `SOURCE_PATHS` | `src/`, `tools/` |
| `TEST_PATHS` | `tests/` |

## Sessions

| Key | Value |
|---|---|
| `SESSION_CEILING` | `5` live sessions besides the lead, of any kind — **shared with mewgenics-coop**: one ceiling for both projects together, not five each |
| `WORKER_MODEL` | Opus |
| `REVIEWER_MODEL_SRC` | Fable |
| `REVIEWER_MODEL_DOCS` | Sonnet |
| `TEST_REVIEWER_MODEL` | Opus |
| `STEWARD_MODEL` | Sonnet |
| `MINER_MODEL` | Sonnet |
| `METRICS_MODEL` | Sonnet |
| `LOG_TRIAGE_MODEL` | n/a (module off) |
| `ESCALATION_MODEL` | Fable |
| `FULL_MCP_TOKEN` | `~/.config/agent-manager/full-mcp-once` |
| `AM_TMUX_SOCKET` | per lead machine: **WSL on Tubal-Cain** `/tmp/tmux-1000/agentmgr` (`ls /tmp/tmux-$(id -u)/`) · **Mac** not measured yet — a lead on the Mac stops on this key until it is filled. The lead types into a pane only on `OWNER`'s word; panes are `am_<session id>` |

## Modules

| Key | Value |
|---|---|
| `MODULE_WINDOWS_RUNNER` | **on** |
| `BUILD_HOST` | Tubal-Cain (the Windows box) |
| `RUNNER_MODE` | per lead machine: **Mac** `bridge` · **WSL on Tubal-Cain** `local` |
| `WIN_CLONE_WSL` | local mode: `/mnt/c/Users/marti/claude-project/ffb-coop` — never run WSL `git` in it |
| `RUNNER_LOCAL_TOOLS` | local mode: `/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin` |
| `RUNNER_SESSION` | bridge mode: `windows-runner` — the same session mewgenics-coop uses; the runner prompt `cd`s into `WIN_CLONE` first |
| `RUNNER_START_CMD` | bridge mode: `cd C:\Users\marti\claude-project\mewgenics-coop` then `claude --remote-control "windows-runner" --bg --strict-mcp-config` (the shared session, started as mewgenics-coop starts it) |
| `RUNNER_BRIDGE_CMD` | **Mac** `cd ~/claude-project/ccbridge && venv/bin/python -m ccbridge` · **WSL on Tubal-Cain** `cd /mnt/c/Users/marti/claude-project/ccbridge && venv/Scripts/python.exe -m ccbridge` — a Windows Python: a `--prompt-file` must be a Windows path under `C:\Users\marti\AppData\Local\Temp\` |
| `RUNNER_LOCK` | `C:\Users\marti\claude-project\ffb-coop\.runner.lock` (`win-run-local` takes `<clone>/.runner.lock`) |
| `RUNNER_STALE_MIN` | `45` |
| `RUNNER_MODEL` | Sonnet |
| `MODULE_LOG_TRIAGE` | **off** |
| `MODULE_SYMBOL_INDEX` | **off** |

## Humans and approvals

Process v27. Who may say yes to what, and how that yes reaches the session that acts.

| Key | Value |
|---|---|
| `HUMANS` | `single` — Martin runs the project from the lead pane |
| `OWNER` | `Martin` (key `owner`) |
| `CHAT_CHANNEL` | Matrix via `ask-martin` (`ask-martin ask` / `notify`, and the PreToolUse gate) |

**`single`.** The lead is `OWNER`'s voice to the agents. `OWNER`'s word in the lead pane — typed, or
an option picked from a multiple-choice prompt — is authoritative for every row below, design and
outward actions alike. The lead records it verbatim with its time on the issue or PR in the same
step, and a worker acts on the relay with no quote check and no second ask. Every row is `OWNER` ·
`relay`.

| Row (key) | Action | Decider | Channel |
|---|---|---|---|
| `DESIGN_DECIDER` | a design or scope decision a worker is blocked on; a Done-looks-like bullet dropped or changed | `OWNER` | `relay` |
| `READY_MOVER` | Triage → Ready | `OWNER` | `relay` |
| `MERGER` | merge a PR | `OWNER` | `relay` |
| `PUSHER` | push anywhere but the session's own `am/<name>` branch: the default branch, a tag, a force push, someone else's branch | `OWNER` | `relay` |
| `SHIPPER` | ship or publish: a release, `<SHIP_CMD>`, a package, a public artifact or page | `OWNER` | `relay` |
| `DELETER` | delete: a branch not the session's own, an issue, a release, data, a published artifact | `OWNER` | `relay` |
| `VERDICT_OWNER` | rule and practice verdicts | `OWNER` | `relay` |
| `REVIEW_POSTER` | posting an approving or change-requesting review | `OWNER` | `relay` |

## Process

| Key | Value |
|---|---|
| `PROCESS_REPO` | `Magto/agent-workflow` |
| `PROCESS_VERSION_FILE` | `docs/VERSION` in `Magto/agent-workflow` (v27) |
