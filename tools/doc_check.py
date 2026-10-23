#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""doc_check.py - documentation drift guard for Project Bp.

The written state has to match the code, or the next session starts from a lie.
Eight checks, each one tied to a real drift this repository has already had:

  1  referenced paths exist            (AGENTS pointed at a retired HANDOFF)
  2  DEBT ledger counts are conserved  (hand-maintained totals drift)
  3  every open item has a verify      (closed criterion was prose)
  4  status is one of four values      (state and plan mixed in one cell)
  5  STATE protocol == version.h       (the one place a version bump forgets)
  6  STATE gate count == budgets       (17 appeared in five files)
  7  document size limits              (PROGRESS grew to 113 KB)
  8  STATE has <=1 WIP and no vague    (prose status cannot be checked)

Usage:
    python tools/doc_check.py [--root .] [--strict] [--selftest]

Exit code: 0 = no failures (warnings allowed), 1 = failures.
Deliberately English-only output: Windows PowerShell 5.1 mangles CJK here.
"""

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

OK, WARN, FAIL = "OK  ", "WARN", "FAIL"
results = []


def record(level, code, msg):
    results.append((level, code, msg))


def reset():
    del results[:]


# ---------------------------------------------------------------- helpers

def read(path: Path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def md_files(root: Path):
    for p in root.rglob("*.md"):
        rel = p.relative_to(root).as_posix()
        if (rel.startswith("docs/archive/") or "/archive/" in rel or
                rel.startswith("docs/audits/") or rel.startswith("build/")):
            continue  # dated evidence and archives may point at retired paths
        yield p, rel


def table_blocks(text):
    """Yield (section_title, header_cells, [row_cells]) for each markdown table."""
    section = ""
    header = None
    rows = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            section = stripped.lstrip("#").strip()
        if stripped.startswith("|"):
            cells = [c.strip() for c in stripped.strip("|").split("|")]
            if header is None:
                header = cells
                rows = []
                continue
            if all(re.fullmatch(r":?-{2,}:?", c) for c in cells if c):
                continue  # separator
            rows.append(cells)
        else:
            if header is not None:
                yield section, header, rows
                header, rows = None, []
    if header is not None:
        yield section, header, rows


def col(header, *names):
    for i, h in enumerate(header):
        low = h.lower()
        if any(n.lower() in low for n in names):
            return i
    return None


# ---------------------------------------------------------------- checks

PATH_RE = re.compile(
    r"(?<![\w/])"
    r"((?:docs|tools|sim|bridge|godot|build)/[^\s'\"()\[\]|,;]+?"
    r"\.(?:md|ps1|py|cpp|h|gd|toml|map|txt|bat))"
)
LINE_SUFFIX_RE = re.compile(r":\d+(?:-\d+)?$")
# lines that describe something not created yet (contracts, templates) are not drift
PLANNED_HINTS = ("new", "planned", "todo", "xxx", "000n", "<", "{",
                 "尚未创建", "新建", "待建", "未建", "计划", "时建")


def load_ignore(root: Path):
    p = root / ".doccheckignore"
    if not p.exists():
        return set()
    out = set()
    for line in (read(p) or "").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.add(line)
    return out


def check_paths(root: Path):
    bad = []
    future = []
    ignored = load_ignore(root)
    for p, rel in md_files(root):
        text = read(p) or ""
        for lineno, line in enumerate(text.splitlines(), 1):
            low = line.lower()
            if any(h in low for h in PLANNED_HINTS):
                continue
            for m in PATH_RE.finditer(line):
                ref = LINE_SUFFIX_RE.sub("", m.group(1)).rstrip(".,;:" + chr(96))
                if any(ch in ref for ch in "*<>$"):
                    continue
                if ref in ignored or ref.startswith("build/"):
                    continue
                if not (root / ref).exists():
                    entry = f"{rel}:{lineno} -> {ref}"
                    # milestone contracts describe files that do not exist yet
                    (future if rel.startswith("docs/contracts/") else bad).append(entry)
    for f in future:
        record(WARN, "PATH", f"planned path not created yet: {f}")
    if bad:
        for b in bad[:12]:
            record(FAIL, "PATH", f"missing referenced path: {b}")
        if len(bad) > 12:
            record(FAIL, "PATH", f"... and {len(bad) - 12} more")
    elif not future:
        record(OK, "PATH", "all referenced paths exist")
    else:
        record(OK, "PATH", "all existing-path references resolve")

STATUS_ENUM = {"open", "wip", "blocked", "closed"}


def check_debt(root: Path):
    p = root / "docs/DEBT.md"
    text = read(p)
    if text is None:
        record(WARN, "DEBT", "docs/DEBT.md not found (run migration first)")
        return

    declared = None
    m = re.search(r"未解决\s*\**\s*(\d+)", text)
    if m:
        declared = int(m.group(1))

    open_like = 0
    closed_like = 0
    bad_status = []
    missing_verify = []
    verify_column_seen = False

    for section, header, rows in table_blocks(text):
        # only debt tables carry a severity column; skip BACKLOG-style tables
        if col(header, "级别", "severity") is None:
            continue
        st_i = col(header, "状态", "status")
        vf_i = col(header, "verify", "验证命令")
        if vf_i is not None:
            verify_column_seen = True
        is_closed_section = "关闭" in section
        for cells in rows:
            if not cells or not re.match(r"^(AUD|DEBT|DESIGN|FEAT)-\d+", cells[0]):
                continue
            ident = cells[0]
            if st_i is not None and st_i < len(cells):
                raw = cells[st_i]
                head = re.split(r"[.|,;/]", raw)[0].strip().lower()
                if head not in STATUS_ENUM:
                    bad_status.append(f"{ident}: '{raw}'")
                elif head == "closed" or is_closed_section:
                    closed_like += 1
                else:
                    open_like += 1
            elif is_closed_section:
                closed_like += 1
            else:
                open_like += 1
            if vf_i is not None:
                val = cells[vf_i].strip() if vf_i < len(cells) else ""
                if val in ("", "-", "—", "n/a"):
                    missing_verify.append(ident)

    if bad_status:
        for b in bad_status[:8]:
            record(FAIL, "DEBT", f"status outside enum {{open,wip,blocked,closed}}: {b}")
    if missing_verify:
        for b in missing_verify[:8]:
            record(FAIL, "DEBT", f"open item without verify command: {b}")

    if declared is not None:
        if declared != open_like:
            record(
                FAIL,
                "DEBT",
                f"declared unresolved={declared} but table has {open_like} "
                f"(closed rows: {closed_like})",
            )
        else:
            record(OK, "DEBT", f"ledger conserved: {declared} unresolved / {closed_like} closed")
    else:
        record(WARN, "DEBT", "no 'unresolved N' counter found in DEBT.md")

    if not verify_column_seen:
        record(WARN, "DEBT", "no verify column yet - every open item should carry a runnable command")


def check_protocol(root: Path):
    vh = root / "sim/core/version.h"
    st = root / "docs/STATE.md"
    text_v, text_s = read(vh), read(st)
    if text_v is None:
        record(WARN, "PROTO", "sim/core/version.h not found")
        return
    m = re.search(r"#define\s+BP_PROTOCOL_VERSION\s+(\d+)", text_v)
    if not m:
        record(FAIL, "PROTO", "BP_PROTOCOL_VERSION not found in version.h")
        return
    code_ver = int(m.group(1))
    if text_s is None:
        record(WARN, "PROTO", "docs/STATE.md not found")
        return
    m2 = re.search(r"协议版本[^\n]*?v\s*(\d+)", text_s) or re.search(
        r"protocol[^\n]*?v\s*(\d+)", text_s, re.I
    )
    if not m2:
        record(WARN, "PROTO", "STATE.md has no 'protocol version' row")
        return
    state_ver = int(m2.group(1))
    if code_ver != state_ver:
        record(FAIL, "PROTO", f"STATE says v{state_ver}, version.h says v{code_ver}")
    else:
        record(OK, "PROTO", f"protocol version aligned at v{code_ver}")


def check_gates(root: Path):
    st = read(root / "docs/STATE.md")
    bg = read(root / "docs/budgets.md")
    smoke = read(root / "tools/smoke.ps1")
    if st is None or bg is None:
        record(WARN, "GATE", "STATE.md or budgets.md missing, gate count unchecked")
        return
    # "smoke | 19 项" - a bare N/N would collide with numbers like "2.40 / 4.20".
    m_state = re.search(r"smoke[^\n]*?(\d+)\s*项", st)
    m_budget = re.search(r"smoke\s*闸门数[^\n]*?(\d+)", bg)
    markers = len(re.findall(r"Write-Host\s+'==\s", smoke)) if smoke else None

    if m_state and m_budget:
        claimed = int(m_budget.group(1))
        total = int(m_state.group(1))
        if claimed != total:
            record(FAIL, "GATE", f"STATE gate total {total} != budgets {claimed}")
        else:
            record(OK, "GATE", f"gate count aligned at {claimed}")
    else:
        record(WARN, "GATE", "cannot read gate count from STATE/budgets")

    if markers is not None and m_budget:
        claimed = int(m_budget.group(1))
        # markers include the sources lint and bench; allow +/-2 slack
        if abs(markers - claimed) > 2:
            record(
                WARN,
                "GATE",
                f"smoke.ps1 has {markers} step markers, docs claim {claimed} gates - reconcile",
            )

    # The doc gate itself must be wired, or all of the above is decoration.
    if smoke is not None and "doc_check.py" not in smoke:
        record(FAIL, "GATE", "smoke.ps1 does not run doc_check.py")


SIZE_LIMITS = {
    "docs/PROGRESS.md": (150 * 1024, "bytes"),
    "docs/STATE.md": (120, "lines"),
    "docs/DEBT.md": (400, "lines"),
}


def check_sizes(root: Path):
    for rel, (limit, unit) in SIZE_LIMITS.items():
        p = root / rel
        text = read(p)
        if text is None:
            continue
        actual = len(text.encode("utf-8")) if unit == "bytes" else len(text.splitlines())
        if actual > limit:
            record(FAIL, "SIZE", f"{rel}: {actual} {unit} > {limit} - archive or split it")
        else:
            record(OK, "SIZE", f"{rel}: {actual}/{limit} {unit}")
    for p, rel in md_files(root):
        if rel in SIZE_LIMITS:
            continue
        size = p.stat().st_size
        if size > 120 * 1024:
            record(WARN, "SIZE", f"{rel}: {size // 1024} KB is getting heavy - consider splitting")


VAGUE_RE = re.compile(r"(全绿|已通|已完成|已达标|全部通过|ALL GREEN)")


def check_state_quality(root: Path):
    text = read(root / "docs/STATE.md")
    if text is None:
        return
    for section, header, rows in table_blocks(text):
        if "WIP" not in section.upper():
            continue
        real = [r for r in rows if r and not r[0].strip().startswith(("—", "-", "（", "("))]
        if len(real) > 1:
            record(FAIL, "STATE", f"{len(real)} WIP rows - keep exactly one")
        else:
            record(OK, "STATE", f"WIP rows: {len(real)} (limit 1)")

    vague = []
    for i, line in enumerate(text.splitlines(), 1):
        if VAGUE_RE.search(line) and not re.search(r"\d", line) and not line.strip().startswith(">"):
            vague.append(f"line {i}: {line.strip()[:60]}")
    if vague:
        for v in vague[:5]:
            record(WARN, "STATE", f"vague status without a number/command -> {v}")
    else:
        record(OK, "STATE", "no vague status lines")

# ---------------------------------------------------------------- selftest

def selftest():
    """Build a deliberately broken tree and require the checks to fail on it.

    A drift guard that cannot go red is decoration (lesson 15), so this proves
    the four highest-value checks bite: paths, ledger count, protocol, gates.
    """
    tmp = Path(tempfile.mkdtemp(prefix="doc_check_selftest_"))
    try:
        (tmp / "docs").mkdir(parents=True)
        (tmp / "sim/core").mkdir(parents=True)
        (tmp / "tools").mkdir(parents=True)
        (tmp / "AGENTS.md").write_text("# stub\n", encoding="utf-8")
        (tmp / "sim/core/version.h").write_text("#define BP_PROTOCOL_VERSION 6\n", encoding="utf-8")
        (tmp / "tools/smoke.ps1").write_text(
            "Write-Host '== a =='\nWrite-Host '== b =='\nWrite-Host '== c =='\n", encoding="utf-8"
        )
        # STATE claims v5 and 17 gates; DEBT declares 5 but carries 2 live rows.
        (tmp / "docs/STATE.md").write_text(
            "# STATE\n\n| 项 | 值 |\n|---|---|\n| 协议版本 | v5 |\n| 闸门 | 17/17 |\n\n"
            "## 1. WIP\n\n| ID | 一句话 |\n|---|---|\n| — | none |\n",
            encoding="utf-8",
        )
        (tmp / "docs/budgets.md").write_text(
            "# budgets\n\n| 资源 | 值 |\n|---|---|\n| smoke 闸门数 | 17 |\n", encoding="utf-8"
        )
        (tmp / "docs/DEBT.md").write_text(
            "# DEBT\n\n**当前计数**：未解决 **5** 条。\n\n"
            "| ID | 级别 | 症状 | verify | status |\n|---|---|---|---|---|\n"
            "| AUD-01 | P2 | x | cmd | open |\n"
            "| AUD-02 | P2 | y |  | open |\n"
            "| AUD-03 | P2 | z | cmd | 待修 |\n",
            encoding="utf-8",
        )
        (tmp / "docs/BAD.md").write_text("see docs/missing-file.md\n", encoding="utf-8")

        reset()
        check_paths(tmp)
        check_debt(tmp)
        check_protocol(tmp)
        check_gates(tmp)
        fails = [r for r in results if r[0] == FAIL]
        codes = {c for _, c, _ in fails}
        print("doc_check --selftest on a deliberately broken tree")
        for level, code, msg in results:
            print(f"[{level}] {code:<6} {msg}")
        want = {"PATH", "DEBT", "PROTO", "GATE"}
        if not want.issubset(codes):
            print(f"FAIL: selftest expected failures in {sorted(want)}, got {sorted(codes)}")
            return 1
        print(f"PASS: selftest produced {len(fails)} failures across {sorted(codes)}")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="repository root")
    ap.add_argument("--strict", action="store_true", help="treat warnings as failures")
    ap.add_argument("--selftest", action="store_true", help="prove the checks can fail")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    root = Path(args.root).resolve()
    if not (root / "AGENTS.md").exists() and not (root / "docs").exists():
        print(f"FAIL: {root} does not look like the Project Bp repo")
        return 1

    reset()
    check_paths(root)
    check_debt(root)
    check_protocol(root)
    check_gates(root)
    check_sizes(root)
    check_state_quality(root)

    fails = [r for r in results if r[0] == FAIL]
    warns = [r for r in results if r[0] == WARN]

    print(f"doc_check @ {root}")
    print("-" * 68)
    for level, code, msg in results:
        print(f"[{level}] {code:<6} {msg}")
    print("-" * 68)
    print(f"{len(results) - len(fails) - len(warns)} ok, {len(warns)} warn, {len(fails)} fail")

    if fails or (args.strict and warns):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
