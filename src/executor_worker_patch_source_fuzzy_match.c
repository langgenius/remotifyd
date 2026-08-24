#include "executor_worker_patch_source_fuzzy_match.h"
#include "executor_worker_patch_source_fuzzy_unicode.h"

#include <stdlib.h>
#include <string.h>

static bool patch_source_fuzzy_source_line_count(
    const uint8_t *source,
    size_t source_length,
    size_t *line_count
) {
    size_t count = 0;
    size_t index;

    if (source_length == 0) {
        *line_count = 0;
        return true;
    }
    for (index = 0; index < source_length; index++) {
        if (source[index] == '\n') {
            if (count == SIZE_MAX) return false;
            count++;
        }
    }
    if (source[source_length - 1] != '\n') {
        if (count == SIZE_MAX) return false;
        count++;
    }
    *line_count = count;
    return true;
}

static bool patch_source_fuzzy_plan_init(
    PatchSourceFuzzyMatchPlan *result,
    const PatchFile *file,
    const uint8_t *source,
    size_t source_length
) {
    PatchSourceFuzzyMatchPlan plan = {0};
    PatchSlice *source_lines;
    size_t match_bytes;
    size_t line_bytes;
    size_t cursor = 0;
    size_t index;

    if (!patch_source_fuzzy_source_line_count(
        source, source_length, &plan.source_line_count
    ) || file->hunk_count > SIZE_MAX / sizeof(*plan.hunks) ||
        plan.source_line_count > SIZE_MAX / sizeof(*source_lines)) {
        return false;
    }
    match_bytes = file->hunk_count * sizeof(*plan.hunks);
    line_bytes = plan.source_line_count * sizeof(*source_lines);
    if (line_bytes > SIZE_MAX - match_bytes) return false;
    plan.hunks = malloc(match_bytes + line_bytes);
    if (plan.hunks == NULL) return false;
    source_lines = (PatchSlice *)((uint8_t *)plan.hunks + match_bytes);
    plan.source_lines = source_lines;

    for (index = 0; index < plan.source_line_count; index++) {
        const uint8_t *newline = memchr(
            source + cursor, '\n', source_length - cursor
        );
        size_t end = newline == NULL
            ? source_length : (size_t)(newline - source);

        source_lines[index].bytes = source + cursor;
        source_lines[index].length = end - cursor;
        if (source_lines[index].length != 0 && source[end - 1] == '\r') {
            source_lines[index].length--;
        }
        cursor = end + (newline == NULL ? 0 : 1);
    }
    if (cursor != source_length) {
        free(plan.hunks);
        return false;
    }
    *result = plan;
    return true;
}

static PatchSourceFuzzyMatchResult patch_source_fuzzy_expected_build(
    PatchSlice **raw,
    uint8_t **context,
    size_t *count,
    const PatchFile *file
) {
    size_t total = 0;
    size_t hunk_index;
    PatchSlice *lines;
    uint8_t *contexts;

    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        size_t old_count = 0;
        size_t line_index;

        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            uint8_t operation = hunk->lines[line_index].operation;
            if (operation == ' ' || operation == '-') old_count++;
            else if (operation != '+') return PATCH_FUZZY_MATCH_REJECTED;
        }
        if (old_count == 0 || old_count != hunk->old_length) {
            return PATCH_FUZZY_MATCH_REJECTED;
        }
        if (old_count > SIZE_MAX - total) return PATCH_FUZZY_MATCH_FAILED;
        total += old_count;
    }
    if (total > SIZE_MAX / sizeof(*lines) ||
        total > SIZE_MAX - total * sizeof(*lines)) {
        return PATCH_FUZZY_MATCH_FAILED;
    }
    lines = malloc(total * sizeof(*lines) + total);
    if (lines == NULL) return PATCH_FUZZY_MATCH_FAILED;
    contexts = (uint8_t *)(lines + total);
    total = 0;
    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        size_t line_index;
        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            const PatchLine *line = &hunk->lines[line_index];
            if (line->operation == '+') continue;
            lines[total] = line->content;
            contexts[total] = line->operation == ' ';
            total++;
        }
    }
    *raw = lines;
    *context = contexts;
    *count = total;
    return PATCH_FUZZY_MATCH_SUCCEEDED;
}

static bool patch_source_fuzzy_slice_equal(PatchSlice left, PatchSlice right) {
    return left.length == right.length && (left.length == 0 ||
        memcmp(left.bytes, right.bytes, left.length) == 0);
}

static size_t patch_source_fuzzy_saturating_add(
    size_t value,
    size_t added,
    size_t limit
) {
    return added >= limit - value ? limit : value + added;
}

static bool patch_source_fuzzy_match_at(
    const PatchSourceFuzzyMatchPlan *plan,
    const PatchSourceFuzzyLines *source_comparable,
    const PatchSlice *expected_raw,
    const uint8_t *expected_context,
    const PatchSourceFuzzyLines *expected_comparable,
    size_t expected_offset,
    size_t expected_count,
    size_t source_start,
    uint8_t budget,
    uint8_t *edits
) {
    size_t strong_characters = 0;
    size_t strong_count = 0;
    size_t raw_anchor_characters = 0;
    uint8_t character_edits = 0;
    bool raw_mismatch = false;
    size_t index;

    for (index = 0; index < expected_count; index++) {
        size_t expected_index = expected_offset + index;
        size_t source_index = source_start + index;
        bool raw_exact = patch_source_fuzzy_slice_equal(
            expected_raw[expected_index], plan->source_lines[source_index]
        );
        uint8_t distance = 0;

        if (!raw_exact && !patch_source_fuzzy_bounded_distance(
            &expected_comparable->lines[expected_index],
            &source_comparable->lines[source_index],
            (uint8_t)(budget - character_edits), &distance
        )) return false;
        character_edits = (uint8_t)(character_edits + distance);
        if (!raw_exact) raw_mismatch = true;
        if ((expected_context[expected_index] || raw_exact) && distance == 0 &&
            patch_source_fuzzy_strong_anchor(
                &expected_comparable->lines[expected_index]
            )) {
            size_t graphemes = expected_comparable
                ->lines[expected_index].grapheme_count;
            strong_characters = patch_source_fuzzy_saturating_add(
                strong_characters, graphemes, 32
            );
            strong_count = patch_source_fuzzy_saturating_add(
                strong_count, 1, 2
            );
            if (raw_exact) raw_anchor_characters =
                patch_source_fuzzy_saturating_add(
                    raw_anchor_characters, graphemes, 8
                );
        }
    }
    if (raw_mismatch && !(
        (strong_characters >= 24 ||
            (strong_count >= 2 && strong_characters >= 16)) &&
        (raw_anchor_characters >= 8 || strong_characters >= 32)
    )) return false;
    *edits = character_edits;
    return true;
}

static PatchSourceFuzzyMatchResult patch_source_fuzzy_find_hunk(
    PatchSourceFuzzyHunkMatch *result,
    const PatchSourceFuzzyMatchPlan *plan,
    const PatchSourceFuzzyLines *source_comparable,
    const PatchSlice *expected_raw,
    const uint8_t *expected_context,
    const PatchSourceFuzzyLines *expected_comparable,
    size_t expected_offset,
    size_t expected_count,
    size_t minimum_start,
    uint8_t budget
) {
    PatchSourceFuzzyHunkMatch found = {0};
    bool has_match = false;
    size_t last_start;
    size_t start;

    if (minimum_start > plan->source_line_count ||
        expected_count > plan->source_line_count - minimum_start) {
        return PATCH_FUZZY_MATCH_REJECTED;
    }
    last_start = plan->source_line_count - expected_count;
    for (start = minimum_start; start <= last_start; start++) {
        uint8_t edits;
        if (!patch_source_fuzzy_match_at(
            plan, source_comparable, expected_raw, expected_context,
            expected_comparable, expected_offset, expected_count,
            start, budget, &edits
        )) continue;
        if (has_match) {
            return PATCH_FUZZY_MATCH_REJECTED;
        }
        found.source_start = start;
        found.character_edits = edits;
        has_match = true;
    }
    if (!has_match) return PATCH_FUZZY_MATCH_REJECTED;
    *result = found;
    return PATCH_FUZZY_MATCH_SUCCEEDED;
}

PatchSourceFuzzyMatchResult patch_source_fuzzy_match_file(
    PatchSourceFuzzyMatchPlan *plan,
    const PatchFile *file,
    const uint8_t *source,
    size_t source_length,
    uint8_t character_edit_budget
) {
    PatchSourceFuzzyMatchPlan pending = {0};
    PatchSourceFuzzyLines source_comparable = {0};
    PatchSourceFuzzyLines expected_comparable = {0};
    PatchSlice *expected_raw = NULL;
    uint8_t *expected_context = NULL;
    size_t expected_count;
    PatchSourceFuzzyMatchResult expected_result;

    if (!patch_source_fuzzy_plan_init(
        &pending, file, source, source_length
    )) return PATCH_FUZZY_MATCH_FAILED;
    expected_result = patch_source_fuzzy_expected_build(
        &expected_raw, &expected_context, &expected_count, file
    );
    if (expected_result != PATCH_FUZZY_MATCH_SUCCEEDED) {
        patch_source_fuzzy_match_plan_release(&pending);
        return expected_result;
    }
    if (!patch_source_fuzzy_lines_build(
        &source_comparable, pending.source_lines, pending.source_line_count
    ) || !patch_source_fuzzy_lines_build(
        &expected_comparable, expected_raw, expected_count
    )) {
        patch_source_fuzzy_lines_release(&source_comparable);
        free(expected_raw);
        patch_source_fuzzy_match_plan_release(&pending);
        return PATCH_FUZZY_MATCH_FAILED;
    }
    {
        size_t expected_offset = 0;
        size_t minimum_start = 0;
        uint8_t used_edits = 0;
        size_t hunk_index;

        for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            PatchSourceFuzzyMatchResult match_result = patch_source_fuzzy_find_hunk(
                &pending.hunks[hunk_index], &pending,
                &source_comparable, expected_raw, expected_context,
                &expected_comparable, expected_offset,
                file->hunks[hunk_index].old_length, minimum_start,
                (uint8_t)(character_edit_budget - used_edits)
            );
            if (match_result != PATCH_FUZZY_MATCH_SUCCEEDED) {
                patch_source_fuzzy_lines_release(&expected_comparable);
                patch_source_fuzzy_lines_release(&source_comparable);
                free(expected_raw);
                patch_source_fuzzy_match_plan_release(&pending);
                return match_result;
            }
            used_edits = (uint8_t)(used_edits +
                pending.hunks[hunk_index].character_edits);
            minimum_start = pending.hunks[hunk_index].source_start +
                file->hunks[hunk_index].old_length;
            expected_offset += file->hunks[hunk_index].old_length;
        }
        if (expected_offset != expected_count) {
            patch_source_fuzzy_lines_release(&expected_comparable);
            patch_source_fuzzy_lines_release(&source_comparable);
            free(expected_raw);
            patch_source_fuzzy_match_plan_release(&pending);
            return PATCH_FUZZY_MATCH_FAILED;
        }
    }
    patch_source_fuzzy_lines_release(&expected_comparable);
    patch_source_fuzzy_lines_release(&source_comparable);
    free(expected_raw);
    *plan = pending;
    return PATCH_FUZZY_MATCH_SUCCEEDED;
}

void patch_source_fuzzy_match_plan_release(PatchSourceFuzzyMatchPlan *plan) {
    free(plan->hunks);
    memset(plan, 0, sizeof(*plan));
}
