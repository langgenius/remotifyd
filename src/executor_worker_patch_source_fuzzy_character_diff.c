#include "executor_worker_patch_source_fuzzy_character_diff.h"

#include <string.h>

#define PATCH_CHARACTER_DIFF_MAX_GRAPHEMES 100000

static bool patch_character_grapheme_equal(
    const PatchSourceFuzzyCharacterText *left,
    size_t left_index,
    const PatchSourceFuzzyCharacterText *right,
    size_t right_index,
    bool reversed
) {
    size_t left_at = reversed ? left->grapheme_count - left_index - 1 : left_index;
    size_t right_at = reversed ? right->grapheme_count - right_index - 1 : right_index;
    size_t left_start = left->boundaries[left_at];
    size_t right_start = right->boundaries[right_at];
    size_t left_length = left->boundaries[left_at + 1] - left_start;
    size_t right_length = right->boundaries[right_at + 1] - right_start;
    return left_length == right_length && (left_length == 0 || memcmp(
        left->bytes.bytes + left_start,
        right->bytes.bytes + right_start,
        left_length
    ) == 0);
}

static size_t patch_character_trace_read(
    const size_t *trace,
    size_t distance,
    ptrdiff_t diagonal
) {
    return trace[distance * distance + (size_t)(diagonal + (ptrdiff_t)distance)];
}

static bool patch_character_previous(
    const size_t *trace,
    size_t distance,
    ptrdiff_t diagonal,
    size_t base_count,
    size_t variant_count,
    size_t *old_position,
    ptrdiff_t *previous_diagonal,
    bool *removed
) {
    size_t remove_predecessor = SIZE_MAX;
    size_t add_predecessor = SIZE_MAX;
    bool can_remove = false;
    bool can_add = false;

    if (diagonal != -(ptrdiff_t)distance) {
        remove_predecessor = patch_character_trace_read(
            trace, distance - 1, diagonal - 1
        );
        can_remove = remove_predecessor != SIZE_MAX &&
            remove_predecessor < base_count;
    }
    if (diagonal != (ptrdiff_t)distance) {
        ptrdiff_t prior_new_position;
        add_predecessor = patch_character_trace_read(
            trace, distance - 1, diagonal + 1
        );
        if (add_predecessor != SIZE_MAX) {
            prior_new_position = (ptrdiff_t)add_predecessor - diagonal - 1;
            can_add = prior_new_position >= 0 &&
                (size_t)prior_new_position < variant_count;
        }
    }
    if (!can_add && !can_remove) return false;
    if (!can_remove || (can_add && remove_predecessor < add_predecessor)) {
        *old_position = add_predecessor;
        *previous_diagonal = diagonal + 1;
        *removed = false;
    } else {
        *old_position = remove_predecessor;
        *previous_diagonal = diagonal - 1;
        *removed = true;
    }
    return true;
}

static PatchSourceFuzzyCharacterDiffResult patch_character_find_path(
    PatchSourceFuzzyCharacterDiffScratch *scratch,
    const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterText *variant,
    bool reversed,
    size_t *path_length
) {
    size_t total;
    size_t limit;
    size_t required;
    size_t distance;

    if (base->grapheme_count > PATCH_CHARACTER_DIFF_MAX_GRAPHEMES ||
        variant->grapheme_count > PATCH_CHARACTER_DIFF_MAX_GRAPHEMES) {
        return PATCH_CHARACTER_DIFF_REJECTED;
    }
    if (variant->grapheme_count > SIZE_MAX - base->grapheme_count) {
        return PATCH_CHARACTER_DIFF_FAILED;
    }
    total = base->grapheme_count + variant->grapheme_count;
    limit = total < PATCH_SOURCE_FUZZY_CHARACTER_DIFF_MAX_EDITS
        ? total : PATCH_SOURCE_FUZZY_CHARACTER_DIFF_MAX_EDITS;
    if (limit == SIZE_MAX || limit + 1 > SIZE_MAX / (limit + 1)) {
        return PATCH_CHARACTER_DIFF_FAILED;
    }
    required = (limit + 1) * (limit + 1);
    if (required > scratch->trace_capacity) return PATCH_CHARACTER_DIFF_FAILED;

    for (distance = 0; distance <= limit; distance++) {
        ptrdiff_t diagonal;
        for (diagonal = -(ptrdiff_t)distance;
            diagonal <= (ptrdiff_t)distance; diagonal += 2) {
            size_t trace_index = distance * distance +
                (size_t)(diagonal + (ptrdiff_t)distance);
            size_t old_position;
            size_t new_position;

            scratch->trace[trace_index] = SIZE_MAX;
            if (distance == 0) {
                old_position = 0;
            } else {
                ptrdiff_t previous_diagonal;
                bool removed;
                if (!patch_character_previous(
                    scratch->trace, distance, diagonal,
                    base->grapheme_count, variant->grapheme_count,
                    &old_position, &previous_diagonal, &removed
                )) continue;
                (void)previous_diagonal;
                if (removed) old_position++;
            }
            new_position = (size_t)((ptrdiff_t)old_position - diagonal);
            while (old_position < base->grapheme_count &&
                new_position < variant->grapheme_count &&
                patch_character_grapheme_equal(
                    base, old_position, variant, new_position, reversed
                )) {
                old_position++;
                new_position++;
            }
            scratch->trace[trace_index] = old_position;
            if (old_position == base->grapheme_count &&
                new_position == variant->grapheme_count) {
                *path_length = distance;
                return PATCH_CHARACTER_DIFF_SUCCEEDED;
            }
        }
    }
    return PATCH_CHARACTER_DIFF_REJECTED;
}

static PatchSourceFuzzyCharacterDiffResult patch_character_build_edits(
    PatchSourceFuzzyCharacterDiffScratch *scratch,
    const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterText *variant,
    size_t path_length,
    PatchSourceFuzzyCharacterEdit *edits,
    size_t capacity,
    size_t *edit_count
) {
    size_t current_old = base->grapheme_count;
    size_t current_new = variant->grapheme_count;
    size_t count = 0;
    size_t distance;

    for (distance = path_length; distance > 0; distance--) {
        ptrdiff_t diagonal = (ptrdiff_t)current_old - (ptrdiff_t)current_new;
        ptrdiff_t previous_diagonal;
        size_t previous_old;
        size_t previous_new;
        size_t after_old;
        size_t after_new;
        bool removed;
        bool separated;
        PatchSourceFuzzyCharacterEdit *edit;

        if (!patch_character_previous(
            scratch->trace, distance, diagonal,
            base->grapheme_count, variant->grapheme_count,
            &previous_old, &previous_diagonal, &removed
        )) return PATCH_CHARACTER_DIFF_FAILED;
        previous_new = (size_t)((ptrdiff_t)previous_old - previous_diagonal);
        after_old = previous_old + (removed ? 1u : 0u);
        after_new = previous_new + (removed ? 0u : 1u);
        if (after_old > current_old || after_new > current_new ||
            current_old - after_old != current_new - after_new) {
            return PATCH_CHARACTER_DIFF_FAILED;
        }
        separated = current_old != after_old;
        if (count == 0 || separated) {
            if (count == capacity) return PATCH_CHARACTER_DIFF_FAILED;
            edit = &edits[count++];
            edit->start = previous_old;
            edit->end = previous_old + (removed ? 1u : 0u);
            edit->replacement_start = previous_new;
            edit->replacement_count = removed ? 0u : 1u;
        } else {
            edit = &edits[count - 1];
            if (previous_old < edit->start) edit->start = previous_old;
            if (removed && previous_old + 1 > edit->end) {
                edit->end = previous_old + 1;
            }
            if (!removed) {
                edit->replacement_start = previous_new;
                edit->replacement_count++;
            }
        }
        current_old = previous_old;
        current_new = previous_new;
    }
    if (current_old != current_new ||
        current_old != patch_character_trace_read(scratch->trace, 0, 0)) {
        return PATCH_CHARACTER_DIFF_FAILED;
    }

    /* Backtracking discovers change blocks from the end; consumers need source order. */
    for (distance = 0; distance < count / 2; distance++) {
        PatchSourceFuzzyCharacterEdit swap = edits[distance];
        edits[distance] = edits[count - distance - 1];
        edits[count - distance - 1] = swap;
    }
    *edit_count = count;
    return PATCH_CHARACTER_DIFF_SUCCEEDED;
}

static bool patch_character_map_reverse(
    PatchSourceFuzzyCharacterEdit *edits,
    size_t count,
    size_t base_count,
    size_t variant_count
) {
    size_t index;
    for (index = 0; index < count; index++) {
        PatchSourceFuzzyCharacterEdit *edit = &edits[index];
        size_t start = edit->start;
        size_t end = edit->end;
        if (end > base_count || start > end ||
            edit->replacement_start > variant_count ||
            edit->replacement_count > variant_count - edit->replacement_start) {
            return false;
        }
        edit->start = base_count - end;
        edit->end = base_count - start;
        edit->replacement_start = variant_count -
            edit->replacement_start - edit->replacement_count;
    }
    for (index = 0; index < count / 2; index++) {
        PatchSourceFuzzyCharacterEdit swap = edits[index];
        edits[index] = edits[count - index - 1];
        edits[count - index - 1] = swap;
    }
    return true;
}

static bool patch_character_edit_lists_equal(
    const PatchSourceFuzzyCharacterEdit *left,
    size_t left_count,
    const PatchSourceFuzzyCharacterEdit *right,
    size_t right_count,
    const PatchSourceFuzzyCharacterText *variant
) {
    size_t index;
    if (left_count != right_count) return false;
    for (index = 0; index < left_count; index++) {
        size_t left_start = variant->boundaries[left[index].replacement_start];
        size_t left_end = variant->boundaries[
            left[index].replacement_start + left[index].replacement_count
        ];
        size_t right_start = variant->boundaries[right[index].replacement_start];
        size_t right_end = variant->boundaries[
            right[index].replacement_start + right[index].replacement_count
        ];
        size_t left_length = left_end - left_start;
        size_t right_length = right_end - right_start;
        if (left[index].start != right[index].start ||
            left[index].end != right[index].end ||
            left_length != right_length ||
            (left_length != 0 && memcmp(
                variant->bytes.bytes + left_start,
                variant->bytes.bytes + right_start,
                left_length
            ) != 0)) return false;
    }
    return true;
}

PatchSourceFuzzyCharacterDiffResult patch_source_fuzzy_character_diff(
    PatchSourceFuzzyCharacterDiff *result,
    PatchSourceFuzzyCharacterDiffScratch *scratch,
    const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterText *variant
) {
    size_t path_length;
    size_t forward_count;
    size_t reverse_count;
    PatchSourceFuzzyCharacterDiff diff = {0};
    PatchSourceFuzzyCharacterDiffResult status = patch_character_find_path(
        scratch, base, variant, false, &path_length
    );
    if (status == PATCH_CHARACTER_DIFF_SUCCEEDED) {
        status = patch_character_build_edits(
            scratch, base, variant, path_length,
            scratch->forward_edits, scratch->forward_capacity, &forward_count
        );
    }
    if (status != PATCH_CHARACTER_DIFF_SUCCEEDED) return status;
    status = patch_character_find_path(scratch, base, variant, true, &path_length);
    if (status != PATCH_CHARACTER_DIFF_SUCCEEDED) return status;
    status = patch_character_build_edits(
        scratch, base, variant, path_length,
        scratch->reverse_edits, scratch->reverse_capacity, &reverse_count
    );
    if (status != PATCH_CHARACTER_DIFF_SUCCEEDED || !patch_character_map_reverse(
        scratch->reverse_edits, reverse_count,
        base->grapheme_count, variant->grapheme_count
    )) return PATCH_CHARACTER_DIFF_FAILED;

    diff.variants[0] = scratch->forward_edits;
    diff.edit_counts[0] = forward_count;
    diff.variant_count = 1;
    if (!patch_character_edit_lists_equal(
        scratch->forward_edits, forward_count,
        scratch->reverse_edits, reverse_count, variant
    )) {
        diff.variants[1] = scratch->reverse_edits;
        diff.edit_counts[1] = reverse_count;
        diff.variant_count = 2;
    }
    *result = diff;
    return PATCH_CHARACTER_DIFF_SUCCEEDED;
}
