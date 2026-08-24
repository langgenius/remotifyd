#include "executor_worker_patch_source_fuzzy_permutation.h"
#include "executor_worker_patch_source_fuzzy_raw.h"
#include "executor_worker_patch_source_fuzzy_semantic.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

typedef struct PatchPermutationBacking {
    uint8_t *candidate;
    size_t *target_boundaries, *candidate_boundaries;
    PatchSlice *keys;
    int64_t *balances;
    size_t byte_capacity, item_capacity;
} PatchPermutationBacking;

static bool patch_permutation_section(size_t *total, size_t count, size_t width,
    size_t *offset) {
    size_t align = alignof(max_align_t), padding = (align - *total % align) % align;
    size_t bytes;
    if (count != 0 && width > SIZE_MAX / count) return false;
    bytes = count * width;
    if (padding > SIZE_MAX - *total || bytes > SIZE_MAX - *total - padding) return false;
    *offset = *total + padding; *total += padding + bytes;
    return true;
}

bool patch_source_fuzzy_permutation_scratch_init(
    PatchSourceFuzzyPermutationScratch *result,
    const PatchDocument *original, const PatchSourceFuzzyDocument *corrected
) {
    size_t payload = 0, lines = 0, bytes, items, total = sizeof(PatchPermutationBacking);
    size_t offsets[5], file_index;
    PatchPermutationBacking *backing;
    for (file_index = 0; file_index < original->file_count; file_index++) {
        size_t hunk_index;
        for (hunk_index = 0; hunk_index < original->files[file_index].hunk_count;
            hunk_index++) {
            const PatchHunk *left = &original->files[file_index].hunks[hunk_index];
            const PatchHunk *right = &corrected->document.files[file_index].hunks[hunk_index];
            size_t line_index;
            for (line_index = 0; line_index < left->line_count; line_index++) {
                size_t left_length = left->lines[line_index].content.length;
                size_t right_length = right->lines[line_index].content.length;
                if (left_length > SIZE_MAX - payload ||
                    right_length > SIZE_MAX - payload - left_length ||
                    lines == SIZE_MAX) return false;
                payload += left_length + right_length; lines++;
            }
        }
    }
    if (lines > SIZE_MAX / 2 || payload > (SIZE_MAX - lines * 2) / 3) return false;
    /* Three bytes cover each invalid UTF-8 byte becoming U+FFFD. Two line
     * slots cover original/corrected separators and any later merged text. */
    bytes = payload * 3 + lines * 2;
    if (bytes == SIZE_MAX || bytes > (size_t)INT64_MAX) return false;
    items = bytes + 1;
#define PATCH_PERM_SECTION(i, count, type) \
    if (!patch_permutation_section(&total, (count), sizeof(type), &offsets[i])) return false
    PATCH_PERM_SECTION(0, bytes, uint8_t); PATCH_PERM_SECTION(1, items, size_t);
    PATCH_PERM_SECTION(2, items, size_t); PATCH_PERM_SECTION(3, items, PatchSlice);
    PATCH_PERM_SECTION(4, items, int64_t);
#undef PATCH_PERM_SECTION
    backing = malloc(total);
    if (backing == NULL) return false;
    backing->candidate = (uint8_t *)backing + offsets[0];
    backing->target_boundaries = (size_t *)((uint8_t *)backing + offsets[1]);
    backing->candidate_boundaries = (size_t *)((uint8_t *)backing + offsets[2]);
    backing->keys = (PatchSlice *)((uint8_t *)backing + offsets[3]);
    backing->balances = (int64_t *)((uint8_t *)backing + offsets[4]);
    backing->byte_capacity = bytes; backing->item_capacity = items;
    result->backing = (uint8_t *)backing;
    return true;
}

static int patch_permutation_compare(const void *left, const void *right) {
    const PatchSlice *a = left, *b = right;
    size_t length = a->length < b->length ? a->length : b->length;
    int compared = length == 0 ? 0 : memcmp(a->bytes, b->bytes, length);
    if (compared != 0) return compared;
    return a->length < b->length ? -1 : a->length != b->length;
}

static size_t patch_permutation_key(PatchPermutationBacking *backing,
    size_t key_count, PatchSlice value) {
    size_t low = 0, high = key_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int compared = patch_permutation_compare(&value, &backing->keys[middle]);
        if (compared == 0) return middle;
        if (compared < 0) high = middle; else low = middle + 1;
    }
    return SIZE_MAX;
}

static void patch_permutation_adjust(PatchPermutationBacking *backing,
    size_t key_count, PatchSlice value, int amount, size_t *nonzero,
    size_t *outside) {
    size_t key = patch_permutation_key(backing, key_count, value);
    /* A leaving grapheme belongs to the current fixed-size window, so an
     * unknown key necessarily contributed one existing outside entry. */
    if (key == SIZE_MAX) { *outside = amount < 0 ? *outside + 1 : *outside - 1; return; }
    if (backing->balances[key] == 0) (*nonzero)++;
    backing->balances[key] += amount;
    if (backing->balances[key] == 0) (*nonzero)--;
}

static PatchSourceFuzzyPermutationResult patch_permutation_candidate(PatchPermutationBacking *backing,
    PatchSlice target, size_t target_count, size_t key_count, PatchSlice candidate) {
    size_t candidate_length, candidate_count, index, nonzero = 0, outside = 0;
    if (!patch_source_fuzzy_raw_join(NULL, &candidate_length, &candidate, 1) ||
        candidate_length > backing->byte_capacity ||
        !patch_source_fuzzy_raw_join(backing->candidate, &candidate_length,
            &candidate, 1) ||
        !patch_source_fuzzy_raw_graphemes(backing->candidate, candidate_length,
            backing->candidate_boundaries, &candidate_count)) return PATCH_PERMUTATION_FAILED;
    if (candidate_count < target_count) return PATCH_PERMUTATION_CLEAR;
    memset(backing->balances, 0, key_count * sizeof(*backing->balances));
    for (index = 0; index < target_count; index++) {
        PatchSlice value = {target.bytes + backing->target_boundaries[index],
            backing->target_boundaries[index + 1] - backing->target_boundaries[index]};
        patch_permutation_adjust(backing, key_count, value, 1, &nonzero, &outside);
    }
    for (index = 0; index < target_count; index++) {
        PatchSlice value = {backing->candidate + backing->candidate_boundaries[index],
            backing->candidate_boundaries[index + 1] - backing->candidate_boundaries[index]};
        patch_permutation_adjust(backing, key_count, value, -1, &nonzero, &outside);
    }
    if (nonzero == 0 && outside == 0) return PATCH_PERMUTATION_FOUND;
    for (index = target_count; index < candidate_count; index++) {
        size_t previous = index - target_count;
        PatchSlice leaving = {backing->candidate + backing->candidate_boundaries[previous],
            backing->candidate_boundaries[previous + 1] - backing->candidate_boundaries[previous]};
        PatchSlice entering = {backing->candidate + backing->candidate_boundaries[index],
            backing->candidate_boundaries[index + 1] - backing->candidate_boundaries[index]};
        patch_permutation_adjust(backing, key_count, leaving, 1, &nonzero, &outside);
        patch_permutation_adjust(backing, key_count, entering, -1, &nonzero, &outside);
        if (nonzero == 0 && outside == 0) return PATCH_PERMUTATION_FOUND;
    }
    return PATCH_PERMUTATION_CLEAR;
}

static int patch_permutation_has_token(const PatchHunk *hunk, size_t start,
    size_t end, PatchSlice expected) {
    while (start < end) {
        if (hunk->lines[start].operation == '-') {
            PatchSourceFuzzySemanticToken token; size_t cursor = 0;
            PatchSourceFuzzySemanticResult result;
            while ((result = patch_source_fuzzy_semantic_next(
                hunk->lines[start].content, &cursor, &token)) == PATCH_SEMANTIC_TOKEN) {
                PatchSlice value = {hunk->lines[start].content.bytes + token.start,
                    token.end - token.start};
                if (patch_permutation_compare(&value, &expected) == 0) return 1;
            }
            if (result == PATCH_SEMANTIC_FAILED) return -1;
        }
        start++;
    }
    return 0;
}

static PatchSourceFuzzyPermutationResult patch_permutation_check(
    PatchSourceFuzzyPermutationScratch *scratch, const PatchHunk *original,
    const PatchHunk *corrected, size_t start, size_t end,
    const PatchSlice *candidate
) {
    PatchPermutationBacking *backing = (PatchPermutationBacking *)scratch->backing;
    size_t line_index;
    for (line_index = start; line_index < end; line_index++) {
        PatchSlice content; PatchSourceFuzzySemanticToken token; size_t cursor = 0;
        PatchSourceFuzzySemanticResult result;
        if (original->lines[line_index].operation != '-') continue;
        content = original->lines[line_index].content;
        while ((result = patch_source_fuzzy_semantic_next(
            content, &cursor, &token)) == PATCH_SEMANTIC_TOKEN) {
            PatchSlice target = {content.bytes + token.start, token.end - token.start};
            size_t target_count, key_count = 0, index;
            int present;
            if (!patch_source_fuzzy_raw_graphemes(target.bytes, target.length,
                backing->target_boundaries, &target_count)) return PATCH_PERMUTATION_FAILED;
            if (target_count < 2) continue;
            present = patch_permutation_has_token(corrected, start, end, target);
            if (present < 0) return PATCH_PERMUTATION_FAILED;
            if (present != 0) continue;
            for (index = 0; index < target_count; index++) {
                backing->keys[index].bytes = target.bytes + backing->target_boundaries[index];
                backing->keys[index].length = backing->target_boundaries[index + 1] -
                    backing->target_boundaries[index];
            }
            qsort(backing->keys, target_count, sizeof(*backing->keys),
                patch_permutation_compare);
            for (index = 0; index < target_count; index++) if (index == 0 ||
                patch_permutation_compare(&backing->keys[index],
                    &backing->keys[index - 1]) != 0) backing->keys[key_count++] = backing->keys[index];
            if (candidate != NULL) {
                PatchSourceFuzzyPermutationResult checked = patch_permutation_candidate(
                    backing, target, target_count, key_count, *candidate);
                if (checked != PATCH_PERMUTATION_CLEAR) return checked;
            } else {
                /* A NULL candidate selects original additions. Semantic targets
                 * contain no LF, so per-line checks equal checking their join. */
                for (index = start; index < end; index++) if (
                    original->lines[index].operation == '+') {
                    PatchSourceFuzzyPermutationResult checked =
                        patch_permutation_candidate(backing, target, target_count,
                            key_count, original->lines[index].content);
                    if (checked != PATCH_PERMUTATION_CLEAR) return checked;
                }
            }
        }
        if (result == PATCH_SEMANTIC_FAILED) return PATCH_PERMUTATION_FAILED;
    }
    return PATCH_PERMUTATION_CLEAR;
}

PatchSourceFuzzyPermutationResult patch_source_fuzzy_permutation_original(
    PatchSourceFuzzyPermutationScratch *scratch, const PatchHunk *original,
    const PatchHunk *corrected, size_t start, size_t end
) {
    return patch_permutation_check(
        scratch, original, corrected, start, end, NULL);
}

PatchSourceFuzzyPermutationResult patch_source_fuzzy_permutation_text(
    PatchSourceFuzzyPermutationScratch *scratch, const PatchHunk *original,
    const PatchHunk *corrected, size_t start, size_t end, PatchSlice candidate
) {
    return patch_permutation_check(
        scratch, original, corrected, start, end, &candidate);
}

void patch_source_fuzzy_permutation_scratch_release(
    PatchSourceFuzzyPermutationScratch *scratch
) {
    free(scratch->backing); memset(scratch, 0, sizeof(*scratch));
}
