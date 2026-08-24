#!/usr/bin/env python3
"""Freeze Echo patch-host's filesystem and unified-diff cases as C data."""

import base64
import difflib
import json
import pathlib
import sys


def encoded(value: str) -> str:
    return base64.b64encode(value.encode("utf-8")).decode("ascii")


def write_entries(output, name, entries):
    output.write(f"static const EchoPatchEntry {name}[] = {{\n")
    for entry in entries:
        output.write(
            '    {"%s", "%s", %d, %s},\n'
            % (
                encoded(entry["path"]),
                entry.get("content_base64", ""),
                entry["mode"],
                "true" if entry["type"] == "directory" else "false",
            )
        )
    if not entries:
        output.write('    {NULL, NULL, 0, false},\n')
    output.write("};\n\n")


def file_entry(path: str, content: str) -> dict:
    return {
        "path": path,
        "content_base64": base64.b64encode(content.encode("utf-8")).decode("ascii"),
        "mode": 0o644,
        "type": "file",
    }


def directory_entry(path: str) -> dict:
    return {"path": path, "content_base64": "", "mode": 0o755, "type": "directory"}


def adapt_unified_diff_cases(corpus, first_id):
    alpha_before = (
        'def greet(name):\n    return "Hello, " + name\n\n'
        'def add(a, b):\n    return a + b\n\n'
        'def subtract(a, b):\n    return a - b'
    )
    alpha_after = (
        'def greet(name):\n    return f"Hello, {name}! 👋"\n\n'
        'def add(a, b):\n    return a + b  # integer or float addition\n\n'
        'def subtract(a, b):\n    return a - b'
    )
    trees = [
        (
            [file_entry("one.txt", "old one\n"), file_entry("two.txt", "old two\n")],
            [file_entry("one.txt", "new one\n"), file_entry("two.txt", "new two\n")],
        ),
        ([], [file_entry("add.txt", "added\n")]),
        ([], [file_entry("add.txt", "added\n")]),
        (
            [file_entry("remove.txt", "remove me\n"), file_entry("old.txt", "old\n")],
            [file_entry("add.txt", "added\n"), file_entry("new.txt", "new\n")],
        ),
        ([file_entry("alpha.py", alpha_before)], [file_entry("alpha.py", alpha_after)]),
    ]
    cases = []
    for offset, (case, (before, after)) in enumerate(zip(corpus["cases"], trees)):
        cases.append(
            {
                "id": first_id + offset,
                "test_name": "unified-diff parser > " + case["name"],
                "cwd": "",
                "patch": case["patch"],
                "before": {"entries": before},
                "after": {"entries": after},
                "result": {"ok": True},
            }
        )
    return cases


def long_lines(label: str, count: int):
    return [
        f"{label} record {index:04d} value-{index * 37 % 10007:05d} stable payload\n"
        for index in range(count)
    ]


def unified_patch(path: str, before, after) -> str:
    return "".join(
        difflib.unified_diff(before, after, fromfile=path, tofile=path, n=2)
    )


def adapt_long_patch_cases(first_id):
    single_before = long_lines("single", 3500)
    single_after = single_before.copy()
    for index in range(4, 3496, 10):
        single_after[index] = single_after[index].replace("stable", "updated")
    single_patch = unified_patch("long/single.txt", single_before, single_after)

    insertion_before = ["header anchor\n", "middle anchor\n", "footer anchor\n"]
    inserted = [
        f"inserted record {index:04d} value-{index * 53 % 10009:05d} payload\n"
        for index in range(2800)
    ]
    insertion_after = [insertion_before[0], *inserted, *insertion_before[1:]]
    insertion_patch = unified_patch(
        "long/insertion.txt", insertion_before, insertion_after
    )

    multi_before = []
    multi_after = []
    multi_sources = []
    multi_targets = []
    multi_patch = ""
    for file_index in range(7):
        path = f"long/multi/file-{file_index}.txt"
        before = long_lines(f"multi-{file_index}", 600)
        after = before.copy()
        for index in range(5, 596, 12):
            after[index] = after[index].replace("stable", "updated")
        multi_before.append(file_entry(path, "".join(before)))
        multi_after.append(file_entry(path, "".join(after)))
        multi_sources.append(before)
        multi_targets.append(after)
        multi_patch += unified_patch(path, before, after)

    missing_before = multi_before[:6]
    missing_patch = "".join(
        unified_patch(entry["path"], multi_sources[index], multi_targets[index])
        for index, entry in enumerate(missing_before)
    )
    missing_patch += unified_patch(
        "long/missing.txt", ["missing old\n"], ["missing new\n"]
    )
    malformed_patch = single_patch + (
        "--- long/malformed.txt\n+++ long/malformed.txt\n"
        "@@ -1,1 +1,1 @@\n?not-an-operation\n"
    )

    cases = [
        ("long single-file multi-hunk patch", single_patch,
         [directory_entry("long"), file_entry("long/single.txt", "".join(single_before))],
         [directory_entry("long"), file_entry("long/single.txt", "".join(single_after))], True),
        ("long single-hunk insertion patch", insertion_patch,
         [directory_entry("long"), file_entry("long/insertion.txt", "".join(insertion_before))],
         [directory_entry("long"), file_entry("long/insertion.txt", "".join(insertion_after))], True),
        ("long multi-file patch", multi_patch,
         [directory_entry("long"), directory_entry("long/multi"), *multi_before],
         [directory_entry("long"), directory_entry("long/multi"), *multi_after], True),
        ("long patch with missing file near end", missing_patch,
         [directory_entry("long"), directory_entry("long/multi"), *missing_before],
         [directory_entry("long"), directory_entry("long/multi"), *missing_before], False),
        ("long malformed patch after valid files", malformed_patch,
         [directory_entry("long"), file_entry("long/single.txt", "".join(single_before))],
         [directory_entry("long"), file_entry("long/single.txt", "".join(single_before))], False),
    ]
    result = []
    for offset, (name, patch, before, after, ok) in enumerate(cases):
        patch_bytes = len(patch.encode("utf-8"))
        assert 64 * 1024 <= patch_bytes <= 124 * 1024, (name, patch_bytes)
        result.append({
            "id": first_id + offset, "test_name": name, "cwd": "", "patch": patch,
            "before": {"entries": before}, "after": {"entries": after},
            "result": {"ok": ok},
        })
    return result


def write_unified_diff_cases(output, corpus):
    for index, case in enumerate(corpus["cases"]):
        output.write(f"static const EchoUnifiedPath echo_unified_paths_{index}[] = {{\n")
        for old_path, new_path in case.get("expected_paths", []):
            output.write(f'    {{"{encoded(old_path)}", "{encoded(new_path)}"}},\n')
        if not case.get("expected_paths"):
            output.write("    {NULL, NULL},\n")
        output.write("};\n")
        for field in ("expected_first_hunk_lines", "format_contains"):
            name = f"echo_unified_{field}_{index}"
            values = case.get(field, [])
            output.write(f"static const char *const {name}[] = {{\n")
            for value in values:
                output.write(f'    "{encoded(value)}",\n')
            if not values:
                output.write("    NULL,\n")
            output.write("};\n")
        output.write("\n")

    output.write("const EchoUnifiedDiffCase echo_unified_diff_cases[] = {\n")
    for index, case in enumerate(corpus["cases"]):
        old_start = case.get("expected_old_start", 0)
        old_lines = case.get("expected_old_lines", 0)
        new_lines = case.get("expected_new_lines", 0)
        output.write(
            '    {"%s", "%s", echo_unified_paths_%d, %d, '
            "echo_unified_expected_first_hunk_lines_%d, %d, "
            "%d, %d, %d, %s, %s, %s, "
            "echo_unified_format_contains_%d, %d, %s, \"%s\", %s},\n"
            % (
                encoded(case["name"]), encoded(case["patch"]), index,
                len(case.get("expected_paths", [])), index,
                len(case.get("expected_first_hunk_lines", [])),
                old_start, old_lines, new_lines,
                "true" if "expected_old_start" in case else "false",
                "true" if "expected_old_lines" in case else "false",
                "true" if "expected_new_lines" in case else "false",
                index, len(case.get("format_contains", [])),
                "true" if case.get("format_twice_equal") else "false",
                encoded(case.get("normalized_header", "")),
                "true" if case.get("parse_before_normalization_fails") else "false",
            )
        )
    output.write("};\n\n")
    output.write(
        "const size_t echo_unified_diff_case_count = "
        "sizeof(echo_unified_diff_cases) / sizeof(echo_unified_diff_cases[0]);\n\n"
    )


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: generate_echo_patch_cases.py PATCH_CASES.json "
            "UNIFIED_DIFF_CASES.json OUTPUT.inc"
        )

    corpus = json.loads(pathlib.Path(sys.argv[1]).read_text())
    unified = json.loads(pathlib.Path(sys.argv[2]).read_text())
    cases = corpus["cases"]
    cases += adapt_unified_diff_cases(unified, max(case["id"] for case in cases) + 1)
    cases += adapt_long_patch_cases(max(case["id"] for case in cases) + 1)
    output_path = pathlib.Path(sys.argv[3])

    with output_path.open("w") as output:
        output.write(
            "/* Generated from Echo patch tests plus deterministic Remotify long cases. */\n"
        )
        write_unified_diff_cases(output, unified)
        for case in cases:
            write_entries(output, f"echo_patch_before_{case['id']}", case["before"]["entries"])
            write_entries(output, f"echo_patch_after_{case['id']}", case["after"]["entries"])

        output.write("static const EchoPatchCase echo_patch_cases[] = {\n")
        for case in cases:
            expected_ok = case.get("result", {}).get("ok", False)
            output.write(
                '    {%d, "%s", "%s", "%s", %s, '
                "echo_patch_before_%d, %d, echo_patch_after_%d, %d},\n"
                % (
                    case["id"],
                    encoded(case["test_name"]),
                    encoded(case["cwd"]),
                    encoded(case["patch"]),
                    "true" if expected_ok else "false",
                    case["id"],
                    len(case["before"]["entries"]),
                    case["id"],
                    len(case["after"]["entries"]),
                )
            )
        output.write("};\n")


if __name__ == "__main__":
    main()
