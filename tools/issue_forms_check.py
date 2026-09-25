#!/usr/bin/env python3
"""Issue-form check (copied from Magto/mewgenics-coop at adoption, #1; origin there: mewgenics-coop#21): GitHub does not tell anyone when an issue form stops parsing -- the form just
disappears from the new-issue page and the next issue is filed free-text. This check fails loudly instead.

    python3 tools/issue_forms_check.py [dir]      # default .github/ISSUE_TEMPLATE

Each form parses to a mapping with `name`, `description` and a non-empty `body`; every body element has a
known `type`, a label (or, for markdown, a value) and a unique id; and every field the process reads is there,
required, under the heading it is read by. `config.yml` keeps `blank_issues_enabled: false`. Every failure
prints as `path:line: reason` (line 1 when the fault has no line of its own), and the script exits 1 on any.
Runs as a step of the `doc-rules` check (.github/workflows/doc-rules.yml). Needs PyYAML.
"""
import os, sys

try:
    import yaml
except ImportError:
    sys.exit("issue_forms_check: PyYAML is missing (pip install pyyaml, or apt install python3-yaml) -- "
             "the check does not pass by skipping")

# The fields the process reads, by form: id -> the label (heading) the issue body shows it under. Taken from
# LEAD-CONFIG's ISSUE_FORM_HEADINGS: Summary, the form's evidence field, Done looks like, Log check, Out of
# scope (not on a finding), FFB Co-op version, Files or symbols involved, Read these, Needs game
# test, and Proposed resolution on a finding. A form added here needs its row before it passes.
COMMON = {
    "summary": "Summary",
    "done-looks-like": "Done looks like",
    "log-check": "Log check",
    "versions": "FFB Co-op version",
    "symbols": "Files or symbols involved",
    "read-these": "Read these",
    "game-test": "Needs game test",
}
REQUIRED = {
    "bug.yml":     {**COMMON, "repro": "Repro and evidence", "out-of-scope": "Out of scope"},
    "feature.yml": {**COMMON, "what-and-why": "What and why", "out-of-scope": "Out of scope"},
    "finding.yml": {**COMMON, "observed": "What was observed, and where", "resolution": "Proposed resolution"},
}
TYPES = {"markdown", "input", "textarea", "dropdown", "checkboxes"}

def line_of(text, needle):
    """1-based line of the first line containing needle, else 1."""
    for i, l in enumerate(text.split("\n"), 1):
        if needle in l: return i
    return 1

def load(path, out):
    text = open(path, encoding="utf-8").read()
    try:
        return text, yaml.safe_load(text)
    except yaml.YAMLError as e:
        mark = getattr(e, "problem_mark", None)
        out.append(f"{path}:{mark.line + 1 if mark else 1}: does not parse as YAML -- {getattr(e, 'problem', e)}")
        return text, None

def check_form(path, name, out):
    text, doc = load(path, out)
    if doc is None: return
    if not isinstance(doc, dict):
        out.append(f"{path}:1: top level is not a mapping"); return
    for key in ("name", "description"):
        if not isinstance(doc.get(key), str) or not doc[key].strip():
            out.append(f"{path}:1: no `{key}` -- GitHub drops a form without one")
    body = doc.get("body")
    if not isinstance(body, list) or not body:
        out.append(f"{path}:{line_of(text, 'body')}: no `body` list -- GitHub drops a form without one"); return
    seen = {}
    for n, el in enumerate(body):
        where = f"{path}:{line_of(text, 'id: ' + str(el.get('id'))) if isinstance(el, dict) and el.get('id') else 1}"
        if not isinstance(el, dict):
            out.append(f"{path}:1: body[{n}] is not a mapping"); continue
        t, attrs, i = el.get("type"), el.get("attributes"), el.get("id")
        if t not in TYPES:
            out.append(f"{where}: body[{n}] has type {t!r}, not one of {sorted(TYPES)}")
        if not isinstance(attrs, dict):
            out.append(f"{where}: body[{n}] has no `attributes`"); continue
        if t == "markdown":
            if not attrs.get("value"): out.append(f"{where}: body[{n}] markdown has no `value`")
            continue
        if not isinstance(attrs.get("label"), str) or not attrs["label"].strip():
            out.append(f"{where}: body[{n}] has no `label`")
        if t == "dropdown" and not attrs.get("options"):
            out.append(f"{where}: body[{n}] dropdown has no `options`")
        if i is not None:
            if i in seen: out.append(f"{where}: id {i!r} appears twice -- GitHub drops the form")
            seen[i] = el
    want = REQUIRED.get(name)
    if want is None:
        out.append(f"{path}:1: form not known to tools/issue_forms_check.py -- add its required ids to REQUIRED")
        return
    for i, label in want.items():
        el = seen.get(i)
        if el is None:
            out.append(f"{path}:1: required field id {i!r} ({label}) is missing"); continue
        where = f"{path}:{line_of(text, 'id: ' + i)}"
        if el["attributes"].get("label") != label:
            out.append(f"{where}: id {i!r} is labelled {el['attributes'].get('label')!r}, the process reads it as {label!r}")
        if not (el.get("validations") or {}).get("required") is True:
            out.append(f"{where}: id {i!r} is not `validations: required: true`")

def check_config(path, _name, out):
    text, doc = load(path, out)
    if doc is None: return
    if not isinstance(doc, dict) or doc.get("blank_issues_enabled") is not False:
        out.append(f"{path}:{line_of(text, 'blank_issues_enabled')}: blank_issues_enabled must be false -- "
                   "free-text issues are what the forms exist to prevent")

def main(d):
    out = []
    names = sorted(f for f in os.listdir(d) if f.endswith((".yml", ".yaml")))
    for f in names:
        p = os.path.join(d, f)
        (check_config if f in ("config.yml", "config.yaml") else check_form)(p, f, out)
    for f in REQUIRED:
        if f not in names: out.append(f"{os.path.join(d, f)}:1: form is missing")
    if "config.yml" not in names: out.append(f"{os.path.join(d, 'config.yml')}:1: config.yml is missing")
    for l in out: print(l)
    print(f"issue_forms_check: {'FAILED' if out else 'all checks passed'} ({len(names)} files in {d})")
    return 1 if out else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else ".github/ISSUE_TEMPLATE"))
