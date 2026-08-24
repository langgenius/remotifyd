#include "executor_worker_patch_source_fuzzy_character_merge.h"
#include "executor_worker_patch_source_fuzzy_character_diff.h"
#include "executor_worker_patch_source_fuzzy_raw.h"
#include "executor_worker_patch_unicode.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include <utf8proc.h>

typedef struct PatchCharacterMergedEdit {
    PatchSourceFuzzyCharacterEdit edit;
    bool intended;
} PatchCharacterMergedEdit;

typedef struct PatchCharacterMergeBacking {
    size_t deletion_capacity, variant_capacity, byte_capacity, boundary_capacity;
    PatchSlice *base_segments, *variant_segments;
    uint8_t *base_bytes, *actual_bytes, *added_bytes;
    uint8_t *merged_bytes;
    size_t *base_boundaries, *actual_boundaries, *added_boundaries;
    PatchCharacterMergedEdit *merged_edits;
    size_t merged_capacity;
    PatchSourceFuzzyCharacterDiffScratch source_diff, intended_diff;
} PatchCharacterMergeBacking;

static bool patch_merge_section(size_t *total, size_t count, size_t width,
    size_t *offset) {
    size_t padding = (alignof(max_align_t) - *total % alignof(max_align_t)) %
        alignof(max_align_t), bytes;
    if (count != 0 && width > SIZE_MAX / count) return false;
    bytes = count * width;
    if (padding > SIZE_MAX - *total || bytes > SIZE_MAX - *total - padding) return false;
    *offset = *total + padding;
    *total += padding + bytes;
    return true;
}

bool patch_source_fuzzy_character_merge_scratch_init(
    PatchSourceFuzzyCharacterMergeScratch *result, const PatchDocument *original,
    const PatchSourceFuzzyDocument *corrected) {
    size_t deletions = 0, additions = 0, payload = 0, lines, bytes, bounds;
    size_t limit, trace_count, total = sizeof(PatchCharacterMergeBacking), offsets[15];
    size_t file_index;
    PatchCharacterMergeBacking *backing;
    for (file_index = 0; file_index < original->file_count; file_index++) {
        size_t hunk_index;
        for (hunk_index = 0; hunk_index < original->files[file_index].hunk_count;
            hunk_index++) {
            const PatchHunk *left = &original->files[file_index].hunks[hunk_index];
            const PatchHunk *right = &corrected->document.files[file_index].hunks[hunk_index];
            size_t line_index;
            for (line_index = 0; line_index < left->line_count; line_index++) {
                size_t added = left->lines[line_index].content.length;
                if (left->lines[line_index].operation == '-') {
                    if (deletions == SIZE_MAX || added > SIZE_MAX - payload) return false;
                    deletions++; payload += added;
                    added = right->lines[line_index].content.length;
                } else if (left->lines[line_index].operation == '+') {
                    if (additions == SIZE_MAX) return false;
                    additions++;
                } else continue;
                if (added > SIZE_MAX - payload) return false;
                payload += added;
            }
        }
    }
    if (deletions > (SIZE_MAX - additions) / 2) return false;
    lines = deletions * 2 + additions;
    if (payload > (SIZE_MAX - lines) / 3) return false;
    bytes = payload * 3 + lines;
    if (bytes == SIZE_MAX) return false;
    bounds = bytes + 1;
    limit = bytes > PATCH_SOURCE_FUZZY_CHARACTER_DIFF_MAX_EDITS / 2
        ? PATCH_SOURCE_FUZZY_CHARACTER_DIFF_MAX_EDITS : bytes * 2;
    trace_count = (limit + 1) * (limit + 1);
    backing = NULL;
#define PATCH_MERGE_SECTION(i, count, type) \
    if (!patch_merge_section(&total, (count), sizeof(type), &offsets[i])) return false
    PATCH_MERGE_SECTION(0, deletions, PatchSlice);
    PATCH_MERGE_SECTION(1, deletions > additions ? deletions : additions, PatchSlice);
    PATCH_MERGE_SECTION(2, bytes, uint8_t); PATCH_MERGE_SECTION(3, bytes, uint8_t);
    PATCH_MERGE_SECTION(4, bytes, uint8_t);
    PATCH_MERGE_SECTION(5, bounds, size_t); PATCH_MERGE_SECTION(6, bounds, size_t);
    PATCH_MERGE_SECTION(7, bounds, size_t); PATCH_MERGE_SECTION(8, trace_count, size_t);
    PATCH_MERGE_SECTION(9, limit, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_SECTION(10, limit, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_SECTION(11, limit, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_SECTION(12, limit, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_SECTION(13, bytes, uint8_t);
    PATCH_MERGE_SECTION(14, limit * 2, PatchCharacterMergedEdit);
#undef PATCH_MERGE_SECTION
    backing = malloc(total);
    if (backing == NULL) return false;
#define PATCH_MERGE_POINTER(field, i, type) backing->field = (type *)((uint8_t *)backing + offsets[i])
    PATCH_MERGE_POINTER(base_segments, 0, PatchSlice);
    PATCH_MERGE_POINTER(variant_segments, 1, PatchSlice);
    PATCH_MERGE_POINTER(base_bytes, 2, uint8_t); PATCH_MERGE_POINTER(actual_bytes, 3, uint8_t);
    PATCH_MERGE_POINTER(added_bytes, 4, uint8_t);
    PATCH_MERGE_POINTER(base_boundaries, 5, size_t);
    PATCH_MERGE_POINTER(actual_boundaries, 6, size_t);
    PATCH_MERGE_POINTER(added_boundaries, 7, size_t);
    PATCH_MERGE_POINTER(source_diff.trace, 8, size_t);
    PATCH_MERGE_POINTER(source_diff.forward_edits, 9, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_POINTER(source_diff.reverse_edits, 10, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_POINTER(intended_diff.forward_edits, 11, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_POINTER(intended_diff.reverse_edits, 12, PatchSourceFuzzyCharacterEdit);
    PATCH_MERGE_POINTER(merged_bytes, 13, uint8_t);
    PATCH_MERGE_POINTER(merged_edits, 14, PatchCharacterMergedEdit);
#undef PATCH_MERGE_POINTER
    backing->deletion_capacity = deletions;
    backing->variant_capacity = deletions > additions ? deletions : additions;
    backing->byte_capacity = bytes; backing->boundary_capacity = bounds;
    backing->source_diff.trace_capacity = trace_count;
    backing->source_diff.forward_capacity = limit;
    backing->source_diff.reverse_capacity = limit;
    backing->intended_diff.trace = backing->source_diff.trace;
    backing->intended_diff.trace_capacity = trace_count;
    backing->intended_diff.forward_capacity = limit;
    backing->intended_diff.reverse_capacity = limit;
    backing->merged_capacity = limit * 2;
    result->backing = (uint8_t *)backing;
    return true;
}

static PatchSlice patch_merge_replacement(const PatchSourceFuzzyCharacterEdit *edit,
    const PatchSourceFuzzyCharacterText *text) {
    size_t start = text->boundaries[edit->replacement_start];
    size_t end = text->boundaries[edit->replacement_start + edit->replacement_count];
    PatchSlice result = {text->bytes.bytes + start, end - start};
    return result;
}

static bool patch_merge_edit_equal(const PatchSourceFuzzyCharacterEdit *left,
    const PatchSourceFuzzyCharacterText *left_text,
    const PatchSourceFuzzyCharacterEdit *right,
    const PatchSourceFuzzyCharacterText *right_text) {
    PatchSlice a = patch_merge_replacement(left, left_text);
    PatchSlice b = patch_merge_replacement(right, right_text);
    return left->start == right->start && left->end == right->end &&
        a.length == b.length && (a.length == 0 ||
        memcmp(a.bytes, b.bytes, a.length) == 0);
}

static bool patch_merge_edits_conflict(const PatchSourceFuzzyCharacterEdit *left,
    const PatchSourceFuzzyCharacterEdit *right) {
    bool left_insert = left->start == left->end;
    bool right_insert = right->start == right->end;
    if (left_insert && right_insert) return left->start == right->start;
    if (left_insert) return left->start > right->start && left->start < right->end;
    if (right_insert) return right->start > left->start && right->start < left->end;
    return left->start < right->end && right->start < left->end;
}

static bool patch_merge_fully_replaced(
    const PatchSourceFuzzyCharacterDiff *source,
    const PatchSourceFuzzyCharacterDiff *intended) {
    size_t source_variant, intended_variant;
    for (source_variant = 0; source_variant < source->variant_count; source_variant++) {
        size_t source_index;
        if (source->edit_counts[source_variant] == 0) return false;
        for (intended_variant = 0; intended_variant < intended->variant_count;
            intended_variant++) for (source_index = 0;
            source_index < source->edit_counts[source_variant]; source_index++) {
            const PatchSourceFuzzyCharacterEdit *from =
                &source->variants[source_variant][source_index];
            size_t intended_index;
            bool covered = false;
            for (intended_index = 0;
                intended_index < intended->edit_counts[intended_variant]; intended_index++) {
                const PatchSourceFuzzyCharacterEdit *by =
                    &intended->variants[intended_variant][intended_index];
                covered = from->start == from->end
                    ? from->start > by->start && from->start < by->end
                    : from->start >= by->start && from->end <= by->end;
                if (covered) break;
            }
            if (!covered) return false;
        }
    }
    return true;
}

static bool patch_merge_range_has_letter_number(
    const PatchSourceFuzzyCharacterText *text, size_t start, size_t count) {
    size_t cursor = text->boundaries[start], end = text->boundaries[start + count];
    while (cursor < end) {
        int32_t codepoint;
        utf8proc_ssize_t consumed = utf8proc_iterate(
            text->bytes.bytes + cursor, (utf8proc_ssize_t)(end - cursor), &codepoint);
        if (consumed < 1 || patch_unicode_is_letter_number(codepoint)) return true;
        cursor += (size_t)consumed;
    }
    return false;
}

static bool patch_merge_grapheme_is_js_space(
    const PatchSourceFuzzyCharacterText *text, size_t index) {
    size_t start = text->boundaries[index], end = text->boundaries[index + 1];
    int32_t codepoint;
    utf8proc_ssize_t consumed = utf8proc_iterate(
        text->bytes.bytes + start, (utf8proc_ssize_t)(end - start), &codepoint);
    if (consumed < 1 || (size_t)consumed != end - start) return false;
    /* ECMAScript \s includes FEFF and deliberately excludes U+0085. */
    return (codepoint >= 0x09 && codepoint <= 0x0d) || codepoint == 0x20 ||
        codepoint == 0xa0 || codepoint == 0x1680 ||
        (codepoint >= 0x2000 && codepoint <= 0x200a) || codepoint == 0x2028 ||
        codepoint == 0x2029 || codepoint == 0x202f || codepoint == 0x205f ||
        codepoint == 0x3000 || codepoint == 0xfeff;
}

static bool patch_merge_replacement_contains(
    const PatchSourceFuzzyCharacterEdit *edit,
    const PatchSourceFuzzyCharacterText *text,
    const PatchSourceFuzzyCharacterText *base, size_t base_index) {
    size_t index, base_start = base->boundaries[base_index];
    size_t base_length = base->boundaries[base_index + 1] - base_start;
    for (index = 0; index < edit->replacement_count; index++) {
        size_t at = edit->replacement_start + index;
        size_t start = text->boundaries[at];
        size_t length = text->boundaries[at + 1] - start;
        if (length == base_length && (length == 0 || memcmp(
            text->bytes.bytes + start, base->bytes.bytes + base_start, length) == 0)) return true;
    }
    return false;
}

static bool patch_merge_can_resolve(const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterEdit *source,
    const PatchSourceFuzzyCharacterText *actual,
    const PatchSourceFuzzyCharacterEdit *intended,
    const PatchSourceFuzzyCharacterText *added) {
    PatchSlice source_bytes = patch_merge_replacement(source, actual);
    PatchSlice intended_bytes = patch_merge_replacement(intended, added);
    bool same_range = source->start == intended->start && source->end == intended->end;
    bool prefix_suffix = source_bytes.length != 0 && source_bytes.length <= intended_bytes.length &&
        (memcmp(intended_bytes.bytes, source_bytes.bytes, source_bytes.length) == 0 ||
        memcmp(intended_bytes.bytes + intended_bytes.length - source_bytes.length,
            source_bytes.bytes, source_bytes.length) == 0);
    size_t index;
    if (patch_merge_edit_equal(source, actual, intended, added)) return true;
    if (same_range) return prefix_suffix || (source->start != source->end &&
        !patch_merge_range_has_letter_number(base, source->start, source->end - source->start) &&
        !patch_merge_range_has_letter_number(added, intended->replacement_start,
            intended->replacement_count));
    if (intended->replacement_count == 0 || (source->start == source->end
        ? source->start <= intended->start || source->start >= intended->end
        : source->start < intended->start || source->end > intended->end)) return false;
    if (prefix_suffix) return true;
    for (index = intended->start; index < intended->end; index++) if (
        !patch_merge_grapheme_is_js_space(base, index) &&
        patch_merge_replacement_contains(intended, added, base, index)) return false;
    return true;
}

typedef enum PatchMergePairResult {
    PATCH_MERGE_PAIR_FAILED,
    PATCH_MERGE_PAIR_REJECTED,
    PATCH_MERGE_PAIR_READY
} PatchMergePairResult;

static int patch_merge_edit_compare(const void *left, const void *right) {
    const PatchCharacterMergedEdit *a = left, *b = right;
    if (a->edit.start != b->edit.start) return a->edit.start < b->edit.start ? -1 : 1;
    if (a->edit.end != b->edit.end) return a->edit.end < b->edit.end ? -1 : 1;
    return 0;
}

static PatchMergePairResult patch_merge_pair_build(PatchCharacterMergeBacking *backing,
    const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterEdit *source, size_t source_count,
    const PatchSourceFuzzyCharacterText *actual,
    const PatchSourceFuzzyCharacterEdit *intended, size_t intended_count,
    const PatchSourceFuzzyCharacterText *added, size_t *merged_count) {
    size_t count = 0;
    size_t source_index;
    if (intended_count > backing->merged_capacity) return PATCH_MERGE_PAIR_FAILED;
    while (count < intended_count) {
        backing->merged_edits[count].edit = intended[count];
        backing->merged_edits[count++].intended = true;
    }
    for (source_index = 0; source_index < source_count; source_index++) {
        size_t intended_index, conflicts = 0, conflict = 0;
        for (intended_index = 0; intended_index < intended_count; intended_index++) if (
            patch_merge_edits_conflict(&source[source_index], &intended[intended_index])) {
            conflict = intended_index; conflicts++;
        }
        if (conflicts == 0) {
            if (count == backing->merged_capacity) return PATCH_MERGE_PAIR_FAILED;
            backing->merged_edits[count].edit = source[source_index];
            backing->merged_edits[count++].intended = false;
            continue;
        }
        if (conflicts != 1 || !patch_merge_can_resolve(
            base, &source[source_index], actual, &intended[conflict], added)) {
            return PATCH_MERGE_PAIR_REJECTED;
        }
    }
    qsort(backing->merged_edits, count, sizeof(*backing->merged_edits), patch_merge_edit_compare);
    for (source_index = 0; source_index < count; source_index++) {
        PatchSourceFuzzyCharacterEdit *edit = &backing->merged_edits[source_index].edit;
        if (edit->start > edit->end || edit->end > base->grapheme_count ||
            (source_index != 0 && patch_merge_edits_conflict(
                &backing->merged_edits[source_index - 1].edit, edit))) {
            return PATCH_MERGE_PAIR_FAILED;
        }
    }
    *merged_count = count;
    return PATCH_MERGE_PAIR_READY;
}

typedef struct PatchMergePass {
    uint8_t *output;
    const uint8_t *expected;
    size_t expected_length, capacity, length;
    bool same;
} PatchMergePass;

static bool patch_merge_append(PatchMergePass *pass, const uint8_t *bytes, size_t length) {
    if (length > SIZE_MAX - pass->length) return false;
    if (pass->output != NULL) {
        if (length > pass->capacity - pass->length) return false;
        if (length != 0) memcpy(pass->output + pass->length, bytes, length);
    } else if (pass->same) {
        if (pass->length > pass->expected_length ||
            length > pass->expected_length - pass->length ||
            (length != 0 && memcmp(pass->expected + pass->length, bytes, length) != 0)) {
            pass->same = false;
        }
    }
    pass->length += length;
    return true;
}

static bool patch_merge_pair_apply(PatchMergePass *pass,
    const PatchCharacterMergeBacking *backing, size_t count,
    const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterText *actual,
    const PatchSourceFuzzyCharacterText *added) {
    size_t cursor = 0, index;
    for (index = 0; index < count; index++) {
        const PatchCharacterMergedEdit *tagged = &backing->merged_edits[index];
        const PatchSourceFuzzyCharacterEdit *edit = &tagged->edit;
        const PatchSourceFuzzyCharacterText *text = tagged->intended ? added : actual;
        PatchSlice replacement = patch_merge_replacement(edit, text);
        size_t start;
        if (edit->start < cursor) return false;
        start = base->boundaries[cursor];
        if (!patch_merge_append(pass, base->bytes.bytes + start,
            base->boundaries[edit->start] - start) ||
            !patch_merge_append(pass, replacement.bytes, replacement.length)) return false;
        cursor = edit->end;
    }
    return patch_merge_append(pass, base->bytes.bytes + base->boundaries[cursor],
        base->bytes.length - base->boundaries[cursor]);
}

static bool patch_merge_text(PatchSourceFuzzyCharacterText *text,
    uint8_t *bytes, size_t byte_capacity, size_t *boundaries, size_t boundary_capacity,
    const PatchSlice *segments, size_t count) {
    size_t length, graphemes;
    if (!patch_source_fuzzy_raw_join(NULL, &length, segments, count) ||
        length > byte_capacity || !patch_source_fuzzy_raw_join(bytes, &length, segments, count) ||
        !patch_source_fuzzy_raw_graphemes(bytes, length, NULL, &graphemes) ||
        graphemes == SIZE_MAX || graphemes + 1 > boundary_capacity ||
        !patch_source_fuzzy_raw_graphemes(bytes, length, boundaries, &graphemes)) return false;
    text->bytes.bytes = bytes; text->bytes.length = length;
    text->boundaries = boundaries; text->grapheme_count = graphemes;
    return true;
}

PatchSourceFuzzyCharacterMergeResult patch_source_fuzzy_character_merge(
    PatchSlice *merged, PatchSourceFuzzyCharacterMergeScratch *scratch,
    const PatchHunk *original, const PatchHunk *corrected, size_t start, size_t end,
    PatchSourceFuzzyCharacterMergeMode mode) {
    PatchCharacterMergeBacking *b = (PatchCharacterMergeBacking *)scratch->backing;
    PatchSourceFuzzyCharacterText base, actual, added;
    PatchSourceFuzzyCharacterDiff source_diff, intended_diff;
    size_t base_count = 0, variant_count = 0, index;
    PatchSourceFuzzyCharacterDiffResult status;
    for (index = start; index < end; index++) if (original->lines[index].operation == '-') {
        if (base_count == b->deletion_capacity) return PATCH_CHARACTER_MERGE_FAILED;
        b->base_segments[base_count] = original->lines[index].content;
        b->variant_segments[base_count++] = corrected->lines[index].content;
    }
    if (!patch_merge_text(&base, b->base_bytes, b->byte_capacity, b->base_boundaries,
        b->boundary_capacity, b->base_segments, base_count) ||
        !patch_merge_text(&actual, b->actual_bytes, b->byte_capacity,
            b->actual_boundaries, b->boundary_capacity, b->variant_segments, base_count)) {
        return PATCH_CHARACTER_MERGE_FAILED;
    }
    status = patch_source_fuzzy_character_diff(
        &source_diff, &b->source_diff, &base, &actual);
    if (status != PATCH_CHARACTER_DIFF_SUCCEEDED) return status == PATCH_CHARACTER_DIFF_REJECTED
        ? PATCH_CHARACTER_MERGE_REJECTED : PATCH_CHARACTER_MERGE_FAILED;
    for (index = start; index < end; index++) if (original->lines[index].operation == '+') {
        if (variant_count == b->variant_capacity) return PATCH_CHARACTER_MERGE_FAILED;
        b->variant_segments[variant_count++] = original->lines[index].content;
    }
    if (!patch_merge_text(&added, b->added_bytes, b->byte_capacity,
        b->added_boundaries, b->boundary_capacity, b->variant_segments, variant_count)) {
        return PATCH_CHARACTER_MERGE_FAILED;
    }
    status = patch_source_fuzzy_character_diff(
        &intended_diff, &b->intended_diff, &base, &added);
    if (status == PATCH_CHARACTER_DIFF_REJECTED) return PATCH_CHARACTER_MERGE_REJECTED;
    if (status != PATCH_CHARACTER_DIFF_SUCCEEDED) return PATCH_CHARACTER_MERGE_FAILED;
    {
        size_t first_length = 0;
        bool first = true;
        bool fully_replaced = mode != PATCH_CHARACTER_MERGE_NORMAL &&
            patch_merge_fully_replaced(&source_diff, &intended_diff);
        if (mode == PATCH_CHARACTER_MERGE_FULLY_REPLACED_ONLY) {
            return fully_replaced ? PATCH_CHARACTER_MERGE_FULLY_REPLACED
                : PATCH_CHARACTER_MERGE_REJECTED;
        }
        for (index = 0; index < source_diff.variant_count; index++) {
            size_t intended_index;
            for (intended_index = 0; intended_index < intended_diff.variant_count;
                intended_index++) {
                size_t merged_count;
                PatchMergePass pass = {0};
                PatchMergePairResult pair = patch_merge_pair_build(b, &base,
                    source_diff.variants[index], source_diff.edit_counts[index], &actual,
                    intended_diff.variants[intended_index],
                    intended_diff.edit_counts[intended_index], &added, &merged_count);
                if (pair == PATCH_MERGE_PAIR_REJECTED) {
                    return fully_replaced ? PATCH_CHARACTER_MERGE_FULLY_REPLACED
                        : PATCH_CHARACTER_MERGE_REJECTED;
                }
                if (pair != PATCH_MERGE_PAIR_READY) return PATCH_CHARACTER_MERGE_FAILED;
                pass.output = first ? b->merged_bytes : NULL;
                pass.expected = b->merged_bytes;
                pass.expected_length = first_length;
                pass.capacity = b->byte_capacity;
                pass.same = true;
                if (!patch_merge_pair_apply(
                    &pass, b, merged_count, &base, &actual, &added)) {
                    return PATCH_CHARACTER_MERGE_FAILED;
                }
                if (first) { first_length = pass.length; first = false; }
                else if (!pass.same || pass.length != first_length) {
                    return fully_replaced ? PATCH_CHARACTER_MERGE_FULLY_REPLACED
                        : PATCH_CHARACTER_MERGE_REJECTED;
                }
            }
        }
        if (first_length == added.bytes.length && (first_length == 0 ||
            memcmp(b->merged_bytes, added.bytes.bytes, first_length) == 0)) {
            return PATCH_CHARACTER_MERGE_UNCHANGED;
        }
        merged->bytes = b->merged_bytes;
        merged->length = first_length;
        return PATCH_CHARACTER_MERGE_MERGED;
    }
}

void patch_source_fuzzy_character_merge_scratch_release(
    PatchSourceFuzzyCharacterMergeScratch *scratch) {
    free(scratch->backing); memset(scratch, 0, sizeof(*scratch));
}
