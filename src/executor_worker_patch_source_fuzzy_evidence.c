#include "executor_worker_patch_source_fuzzy_evidence.h"
#include "executor_worker_patch_source_fuzzy_raw.h"
#include "executor_worker_patch_source_fuzzy_semantic.h"
#include <string.h>
#include <utf8proc.h>
static bool patch_evidence_slice_contains(PatchSlice value, PatchSlice part) {
    size_t cursor;
    if (part.length == 0) return true;
    if (part.length > value.length) return false;
    for (cursor = 0; cursor <= value.length - part.length; cursor++) {
        if (memcmp(value.bytes + cursor, part.bytes, part.length) == 0) return true;
    }
    return false;
}

static int patch_evidence_has_token(PatchSlice value, PatchSlice expected) {
    PatchSourceFuzzySemanticToken token;
    size_t cursor = 0;
    PatchSourceFuzzySemanticResult result;
    while ((result = patch_source_fuzzy_semantic_next(
        value, &cursor, &token)) == PATCH_SEMANTIC_TOKEN) {
        PatchSlice candidate = {value.bytes + token.start, token.end - token.start};
        if (candidate.length == expected.length &&
            (candidate.length == 0 || memcmp(candidate.bytes,
                expected.bytes, candidate.length) == 0)) return 1;
    }
    return result == PATCH_SEMANTIC_FAILED ? -1 : 0;
}

static bool patch_evidence_append(
    PatchSlice *entries, size_t capacity, size_t *count, PatchSlice value
) {
    if (*count == SIZE_MAX || (entries != NULL && *count >= capacity)) return false;
    if (entries != NULL) entries[*count] = value;
    (*count)++;
    return true;
}

bool patch_source_fuzzy_evidence_collect(
    PatchSlice *entries, size_t capacity, size_t *count,
    PatchSlice expected, PatchSlice actual
) {
    PatchSourceFuzzySemanticToken token;
    PatchSourceFuzzySemanticToken previous;
    size_t cursor = 0;
    size_t used = 0;
    PatchSourceFuzzySemanticResult result;

    if (!patch_evidence_append(entries, capacity, &used, expected)) return false;
    while ((result = patch_source_fuzzy_semantic_next(
        expected, &cursor, &token)) == PATCH_SEMANTIC_TOKEN) {
        PatchSlice value = {expected.bytes + token.start, token.end - token.start};
        int present = patch_evidence_has_token(actual, value);
        if (present < 0 || (present == 0 && !patch_evidence_append(
            entries, capacity, &used, value))) return false;
    }
    if (result == PATCH_SEMANTIC_FAILED) return false;
    cursor = 0;
    result = patch_source_fuzzy_semantic_next(expected, &cursor, &previous);
    while (result == PATCH_SEMANTIC_TOKEN &&
        (result = patch_source_fuzzy_semantic_next(
            expected, &cursor, &token)) == PATCH_SEMANTIC_TOKEN) {
        PatchSlice fragment = {expected.bytes + previous.start,
            token.end - previous.start};
        if (previous.end != token.start &&
            !patch_evidence_slice_contains(actual, fragment) &&
            !patch_evidence_append(entries, capacity, &used, fragment)) return false;
        previous = token;
    }
    if (result == PATCH_SEMANTIC_FAILED) return false;
    *count = used;
    return true;
}
bool patch_source_fuzzy_evidence_contains(
    const PatchSlice *entries, size_t count, PatchSlice addition
) {
    size_t index;
    for (index = 0; index < count; index++) {
        if (entries[index].length != 0 &&
            patch_evidence_slice_contains(addition, entries[index])) return true;
    }
    return false;
}

static int patch_evidence_deletions_have_token(
    const PatchHunk *hunk, size_t start, size_t end, PatchSlice expected
) {
    while (start < end) {
        if (hunk->lines[start].operation == '-') {
            int present = patch_evidence_has_token(
                hunk->lines[start].content, expected);
            if (present != 0) return present;
        }
        start++;
    }
    return 0;
}

bool patch_source_fuzzy_evidence_sensitive_tokens_preserved(
    bool *preserved, const PatchHunk *original, const PatchHunk *corrected,
    size_t start, size_t end
) {
    size_t line_index;
    for (line_index = start; line_index < end; line_index++) {
        PatchSlice content;
        PatchSourceFuzzySemanticToken token;
        size_t cursor = 0;
        PatchSourceFuzzySemanticResult result;

        if (original->lines[line_index].operation != '-') continue;
        content = original->lines[line_index].content;
        while ((result = patch_source_fuzzy_semantic_next(
            content, &cursor, &token)) == PATCH_SEMANTIC_TOKEN) {
            PatchSlice value = {content.bytes + token.start,
                token.end - token.start};
            size_t graphemes;
            int present;
            if (!patch_source_fuzzy_raw_graphemes(
                value.bytes, value.length, NULL, &graphemes)) return false;
            if (graphemes < 2) continue;
            present = patch_evidence_deletions_have_token(
                corrected, start, end, value);
            if (present < 0) return false;
            if (present == 0) { *preserved = false; return true; }
        }
        if (result == PATCH_SEMANTIC_FAILED) return false;
    }
    *preserved = true;
    return true;
}

static bool patch_evidence_blank(PatchSlice value) {
    size_t cursor = 0;
    while (cursor < value.length) {
        int32_t codepoint;
        utf8proc_ssize_t consumed = utf8proc_iterate(
            value.bytes + cursor, (utf8proc_ssize_t)(value.length - cursor), &codepoint);
        if (consumed < 1 || !patch_source_fuzzy_raw_is_space(codepoint)) return false;
        cursor += (size_t)consumed;
    }
    return true;
}

PatchSourceFuzzyAlignedEvidenceResult patch_source_fuzzy_evidence_collect_aligned(
    PatchSlice *entries, size_t capacity, size_t *count,
    const PatchHunk *original, const PatchHunk *corrected,
    size_t start, size_t end) {
    size_t line_index, addition_cursor = start, used = 0;
    for (line_index = start; line_index < end; line_index++) {
        PatchSlice addition = {0};
        bool has_addition = false;
        size_t added;
        if (original->lines[line_index].operation != '-') continue;
        /* Filtered deletion and addition arrays align by ordinal, so advance
         * the addition cursor even when this deletion needs no evidence. */
        while (addition_cursor < end &&
            original->lines[addition_cursor].operation != '+') addition_cursor++;
        if (addition_cursor < end) {
            addition = original->lines[addition_cursor++].content;
            has_addition = true;
        }
        if (patch_evidence_slice_contains(
            corrected->lines[line_index].content, original->lines[line_index].content) &&
            original->lines[line_index].content.length ==
                corrected->lines[line_index].content.length) continue;
        if (patch_evidence_blank(original->lines[line_index].content) &&
            patch_evidence_blank(corrected->lines[line_index].content)) {
            if (has_addition && addition.length == original->lines[line_index].content.length &&
                (addition.length == 0 || memcmp(addition.bytes,
                    original->lines[line_index].content.bytes, addition.length) == 0)) {
                return PATCH_ALIGNED_EVIDENCE_REJECTED;
            }
            continue;
        }
        if (entries != NULL && used > capacity) return PATCH_ALIGNED_EVIDENCE_FAILED;
        if (!patch_source_fuzzy_evidence_collect(
            entries == NULL ? NULL : entries + used,
            entries == NULL ? 0 : capacity - used, &added,
            original->lines[line_index].content,
            corrected->lines[line_index].content) || added > SIZE_MAX - used) {
            return PATCH_ALIGNED_EVIDENCE_FAILED;
        }
        used += added;
    }
    *count = used;
    return PATCH_ALIGNED_EVIDENCE_OK;
}
