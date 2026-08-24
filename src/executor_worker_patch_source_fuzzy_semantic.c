#include "executor_worker_patch_source_fuzzy_semantic.h"

#include "executor_worker_patch_unicode.h"

#include <grapheme.h>
#include <utf8proc.h>

typedef struct PatchHanRange {
    int32_t first;
    int32_t last;
    int32_t stride;
} PatchHanRange;

/* utf8proc has no script predicate; mirror Go 1.25's Unicode-15 Han table. */
static const PatchHanRange PATCH_HAN_RANGES[] = {
    {0x2e80, 0x2e99, 1}, {0x2e9b, 0x2ef3, 1}, {0x2f00, 0x2fd5, 1},
    {0x3005, 0x3007, 2}, {0x3021, 0x3029, 1}, {0x3038, 0x303b, 1},
    {0x3400, 0x4dbf, 1}, {0x4e00, 0x9fff, 1}, {0xf900, 0xfa6d, 1},
    {0xfa70, 0xfad9, 1}, {0x16fe2, 0x16fe3, 1}, {0x16ff0, 0x16ff1, 1},
    {0x20000, 0x2a6df, 1}, {0x2a700, 0x2b739, 1},
    {0x2b740, 0x2b81d, 1}, {0x2b820, 0x2cea1, 1},
    {0x2ceb0, 0x2ebe0, 1}, {0x2f800, 0x2fa1d, 1},
    {0x30000, 0x3134a, 1}, {0x31350, 0x323af, 1}
};

static bool patch_semantic_is_han(int32_t codepoint) {
    size_t index;
    for (index = 0; index < sizeof(PATCH_HAN_RANGES) /
        sizeof(PATCH_HAN_RANGES[0]); index++) {
        PatchHanRange range = PATCH_HAN_RANGES[index];
        if (codepoint >= range.first && codepoint <= range.last &&
            (codepoint - range.first) % range.stride == 0) return true;
    }
    return false;
}

static int patch_semantic_next_segment(
    PatchSlice input, size_t *cursor, PatchSourceFuzzySemanticToken *segment
) {
    size_t start = *cursor;
    size_t valid_end;
    size_t length;
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t consumed;

    if (start == input.length) return 0;
    consumed = utf8proc_iterate(input.bytes + start,
        (utf8proc_ssize_t)(input.length - start), &codepoint);
    if (consumed < 0) length = 1;
    else {
        valid_end = start + (size_t)consumed;
        while (valid_end < input.length) {
            consumed = utf8proc_iterate(input.bytes + valid_end,
                (utf8proc_ssize_t)(input.length - valid_end), &codepoint);
            if (consumed < 0) break;
            valid_end += (size_t)consumed;
        }
        length = grapheme_next_word_break_utf8(
            (const char *)input.bytes + start, valid_end - start);
        if (length == 0 || length > valid_end - start) return -1;
    }
    segment->start = start;
    segment->end = start + length;
    *cursor = segment->end;
    return 1;
}

static void patch_semantic_segment_kind(
    PatchSlice input, PatchSourceFuzzySemanticToken segment,
    bool *word_like, bool *only_han
) {
    size_t cursor = segment.start;
    *word_like = false;
    *only_han = true;
    while (cursor < segment.end) {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t consumed = utf8proc_iterate(input.bytes + cursor,
            (utf8proc_ssize_t)(segment.end - cursor), &codepoint);
        if (consumed < 0) { *only_han = false; return; }
        cursor += (size_t)consumed;
        if (codepoint == '_' || patch_unicode_is_letter_number(codepoint))
            *word_like = true;
        if (!patch_semantic_is_han(codepoint)) *only_han = false;
    }
}

PatchSourceFuzzySemanticResult patch_source_fuzzy_semantic_next(
    PatchSlice input, size_t *cursor, PatchSourceFuzzySemanticToken *token
) {
    PatchSourceFuzzySemanticToken segment;
    int result;
    while ((result = patch_semantic_next_segment(input, cursor, &segment)) > 0) {
        bool word_like;
        bool only_han;
        size_t lookahead;
        patch_semantic_segment_kind(input, segment, &word_like, &only_han);
        if (!word_like) continue;
        *token = segment;
        if (!only_han) return PATCH_SEMANTIC_TOKEN;
        lookahead = *cursor;
        while ((result = patch_semantic_next_segment(
            input, &lookahead, &segment)) > 0) {
            patch_semantic_segment_kind(input, segment, &word_like, &only_han);
            if (!word_like || !only_han) break;
            token->end = segment.end;
            *cursor = lookahead;
        }
        return result < 0 ? PATCH_SEMANTIC_FAILED : PATCH_SEMANTIC_TOKEN;
    }
    return result < 0 ? PATCH_SEMANTIC_FAILED : PATCH_SEMANTIC_END;
}
