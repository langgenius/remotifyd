#include "executor_worker_patch_source_fuzzy_provenance.h"
#include "executor_worker_patch_source_fuzzy_block.h"
#include "executor_worker_patch_source_fuzzy_character_merge.h"
#include "executor_worker_patch_source_fuzzy_evidence.h"
#include "executor_worker_patch_source_fuzzy_permutation.h"
#include "executor_worker_patch_source_fuzzy_presentation.h"

#include <stdlib.h>
#include <string.h>

static bool patch_source_fuzzy_slice_equal(PatchSlice left, PatchSlice right) {
    return left.length == right.length && (left.length == 0 ||
        memcmp(left.bytes, right.bytes, left.length) == 0);
}

static bool patch_provenance_null_path(PatchSlice path) {
    static const char value[] = "/dev/null";
    return path.length == sizeof(value) - 1 &&
        memcmp(path.bytes, value, sizeof(value) - 1) == 0;
}

static bool patch_provenance_whole_file(
    bool *whole, const PatchFile *original, const PatchFile *corrected,
    const PatchSourceSet *sources
) {
    const PatchSourceText *source;
    const PatchHunk *hunk;
    size_t source_lines = 0;
    size_t index;
    bool addition = false;
    bool deletion = false;

    if (patch_provenance_null_path(original->old_path)) source = NULL;
    else {
        source = patch_source_set_find(sources, original->old_path);
        if (source == NULL) return false;
    }
    if (source != NULL && source->length != 0) {
        source_lines = 1;
        for (index = 0; index < source->length; index++) {
            if (source->bytes[index] == '\n' && source_lines != SIZE_MAX)
                source_lines++;
        }
        if (source_lines == SIZE_MAX) return false;
        if (source->bytes[source->length - 1] == '\n') source_lines--;
    }
    if (original->hunk_count != 1) { *whole = false; return true; }
    hunk = &corrected->hunks[0];
    if (hunk->old_start != 1 || hunk->old_length != source_lines) {
        *whole = false;
        return true;
    }
    for (index = 0; index < hunk->line_count; index++) {
        if (hunk->lines[index].operation == ' ') { *whole = false; return true; }
        addition = addition || hunk->lines[index].operation == '+';
        deletion = deletion || hunk->lines[index].operation == '-';
    }
    *whole = addition && deletion;
    return true;
}

typedef struct PatchProvenanceAssignment {
    PatchLine *line;
    PatchSlice content;
} PatchProvenanceAssignment;

typedef struct PatchProvenancePass {
    PatchProvenanceAssignment *records;
    PatchSlice *evidence;
    uint8_t *bytes;
    size_t record_capacity, evidence_capacity, byte_capacity;
    size_t record_count, evidence_count, byte_count;
} PatchProvenancePass;

typedef enum PatchProvenanceRecordResult {
    PATCH_PROVENANCE_RECORD_FAILED,
    PATCH_PROVENANCE_RECORD_REJECTED,
    PATCH_PROVENANCE_RECORD_OK
} PatchProvenanceRecordResult;

static PatchProvenanceRecordResult patch_provenance_record_merged(PatchProvenancePass *pass,
    PatchHunk *hunk, size_t start, size_t end, PatchSlice merged) {
    size_t additions = patch_source_fuzzy_operation_count(hunk, start, end, '+');
    size_t lines = additions == 0 && merged.length == 0 ? 0 : 1;
    size_t cursor, content_bytes;

    for (cursor = 0; cursor < merged.length; cursor++) if (merged.bytes[cursor] == '\n') {
        if (lines == SIZE_MAX) return PATCH_PROVENANCE_RECORD_FAILED;
        lines++;
    }
    if (lines != additions) return PATCH_PROVENANCE_RECORD_REJECTED;
    content_bytes = merged.length - (additions == 0 ? 0 : additions - 1);
    if (additions > SIZE_MAX - pass->record_count ||
        content_bytes > SIZE_MAX - pass->byte_count) return PATCH_PROVENANCE_RECORD_FAILED;
    if (pass->records == NULL) {
        pass->record_count += additions;
        pass->byte_count += content_bytes;
        return PATCH_PROVENANCE_RECORD_OK;
    }
    if (pass->record_count > pass->record_capacity ||
        pass->byte_count > pass->byte_capacity ||
        additions > pass->record_capacity - pass->record_count ||
        content_bytes > pass->byte_capacity - pass->byte_count) {
        return PATCH_PROVENANCE_RECORD_FAILED;
    }
    cursor = 0;
    while (start < end) {
        size_t line_end, length;
        if (hunk->lines[start].operation != '+') { start++; continue; }
        line_end = cursor;
        while (line_end < merged.length && merged.bytes[line_end] != '\n') line_end++;
        length = line_end - cursor;
        if (length != 0) memcpy(pass->bytes + pass->byte_count, merged.bytes + cursor, length);
        pass->records[pass->record_count].line = &hunk->lines[start];
        pass->records[pass->record_count].content.bytes = pass->bytes + pass->byte_count;
        pass->records[pass->record_count].content.length = length;
        pass->record_count++;
        pass->byte_count += length;
        cursor = line_end < merged.length ? line_end + 1 : line_end;
        start++;
    }
    return cursor == merged.length
        ? PATCH_PROVENANCE_RECORD_OK : PATCH_PROVENANCE_RECORD_FAILED;
}

PatchSourceFuzzyProvenanceResult patch_source_fuzzy_provenance_reconcile(
    PatchSourceFuzzyProvenance *result,
    const PatchDocument *original,
    PatchSourceFuzzyDocument *corrected,
    const PatchSourceSet *sources
) {
    PatchSourceFuzzyProvenance provenance = {0};
    PatchSourceFuzzyPresentation presentation = {0};
    PatchSourceFuzzyCharacterMergeScratch merge = {0};
    PatchSourceFuzzyPermutationScratch permutation = {0};
    PatchSourceFuzzyProvenanceResult outcome = PATCH_SOURCE_FUZZY_PROVENANCE_SUCCEEDED;
    PatchProvenancePass pass = {0};
    uint8_t *backing = NULL;
    bool old_side_changed = false;
    size_t deletion_offset, file_index, phase;

    if (original->file_count != corrected->document.file_count) {
        return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
    }
    for (file_index = 0; file_index < original->file_count; file_index++) {
        const PatchFile *source_file = &original->files[file_index];
        const PatchFile *target_file = &corrected->document.files[file_index];
        size_t hunk_index;

        if (!patch_source_fuzzy_slice_equal(
            source_file->old_path, target_file->old_path
        ) || !patch_source_fuzzy_slice_equal(
            source_file->new_path, target_file->new_path
        ) || source_file->hunk_count != target_file->hunk_count) {
            return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
        }
        for (hunk_index = 0; hunk_index < source_file->hunk_count; hunk_index++) {
            const PatchHunk *source_hunk = &source_file->hunks[hunk_index];
            const PatchHunk *target_hunk = &target_file->hunks[hunk_index];
            size_t line_index;

            if (source_hunk->old_length != target_hunk->old_length ||
                source_hunk->new_length != target_hunk->new_length ||
                source_hunk->line_count != target_hunk->line_count) {
                return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            }
            for (line_index = 0; line_index < source_hunk->line_count; line_index++) {
                const PatchLine *source_line = &source_hunk->lines[line_index];
                const PatchLine *target_line = &target_hunk->lines[line_index];
                bool content_equal = patch_source_fuzzy_slice_equal(
                    source_line->content, target_line->content
                );

                if (source_line->operation != target_line->operation ||
                    source_line->crlf != target_line->crlf ||
                    source_line->no_final_newline != target_line->no_final_newline) {
                    return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                }
                if (source_line->operation == '+') {
                    if (!content_equal) return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                } else if (source_line->operation == ' ' ||
                    source_line->operation == '-') {
                    old_side_changed = old_side_changed || !content_equal;
                } else return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            }
        }
    }
    if (!old_side_changed) {
        *result = provenance;
        return PATCH_SOURCE_FUZZY_PROVENANCE_SUCCEEDED;
    }
    if (!patch_source_fuzzy_presentation_init(
        &presentation, original, corrected
    )) return PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;

    /* Both passes rerun every gate before the final infallible assignment loop,
     * so corrected remains unchanged until one exact allocation is complete. */
    for (phase = 0; phase < 2; phase++) {
      deletion_offset = 0;
      pass.record_count = 0;
      pass.evidence_count = 0;
      pass.byte_count = 0;
      for (file_index = 0; file_index < original->file_count; file_index++) {
        const PatchFile *source_file = &original->files[file_index];
        PatchFile *target_file = &corrected->document.files[file_index];
        bool whole_file;
        size_t evidence_start = pass.evidence_count;
        size_t record_start = pass.record_count;
        size_t hunk_index;

        if (!patch_provenance_whole_file(
            &whole_file, source_file, target_file, sources)) {
            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            goto finished;
        }

        for (hunk_index = 0; hunk_index < source_file->hunk_count; hunk_index++) {
            const PatchHunk *source_hunk = &source_file->hunks[hunk_index];
            PatchHunk *target_hunk = &target_file->hunks[hunk_index];
            size_t line_index;

            for (line_index = 0; line_index < source_hunk->line_count; line_index++) {
                const PatchLine *source_line = &source_hunk->lines[line_index];
                const PatchLine *target_line = &target_hunk->lines[line_index];
                if (source_line->operation == ' ' &&
                    !patch_source_fuzzy_slice_equal(
                        source_line->content, target_line->content
                    )) {
                    PatchSlice *entries = NULL;
                    size_t capacity = 0;
                    size_t added;
                    if (pass.evidence != NULL) {
                        if (pass.evidence_count > pass.evidence_capacity) {
                            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                            goto finished;
                        }
                        entries = pass.evidence + pass.evidence_count;
                        capacity = pass.evidence_capacity - pass.evidence_count;
                    }
                    if (!patch_source_fuzzy_evidence_collect(
                        entries, capacity, &added,
                        source_line->content, target_line->content
                    ) || added > SIZE_MAX - pass.evidence_count) {
                        outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                        goto finished;
                    }
                    pass.evidence_count += added;
                }
            }

            line_index = 0;
            while (line_index < source_hunk->line_count) {
                size_t block_start;
                size_t block_end;
                size_t deletion_count;
                bool deletion_changed = false;
                bool permutation_pending = false;

                if (source_hunk->lines[line_index].operation == ' ') {
                    line_index++;
                    continue;
                }
                block_start = line_index;
                while (line_index < source_hunk->line_count &&
                    source_hunk->lines[line_index].operation != ' ') {
                    if (source_hunk->lines[line_index].operation == '-' &&
                        !patch_source_fuzzy_slice_equal(
                            source_hunk->lines[line_index].content,
                            target_hunk->lines[line_index].content
                        )) deletion_changed = true;
                    line_index++;
                }
                block_end = line_index;
                deletion_count = patch_source_fuzzy_operation_count(
                    source_hunk, block_start, block_end, '-'
                );
                if (deletion_count > SIZE_MAX - deletion_offset) {
                    outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                    goto finished;
                }
                if (deletion_changed && patch_source_fuzzy_joined_change_sides_equal(
                    source_hunk, block_start, block_end
                )) {
                    outcome = PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED;
                    goto finished;
                }
                if (deletion_changed && whole_file) {
                    bool preserved;
                    if (!patch_source_fuzzy_evidence_sensitive_tokens_preserved(
                        &preserved, source_hunk, target_hunk,
                        block_start, block_end)) {
                        outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                        goto finished;
                    }
                    /* With no long expected token missing from actual, neither
                     * original nor merged additions can form its permutation. */
                    if (!preserved) {
                        PatchSourceFuzzyPermutationResult checked;
                        if (permutation.backing == NULL &&
                            !patch_source_fuzzy_permutation_scratch_init(
                                &permutation, original, corrected)) {
                            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                            goto finished;
                        }
                        checked = patch_source_fuzzy_permutation_original(
                            &permutation, source_hunk, target_hunk,
                            block_start, block_end);
                        if (checked != PATCH_PERMUTATION_CLEAR) {
                            outcome = checked == PATCH_PERMUTATION_FOUND
                                ? PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED
                                : PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                            goto finished;
                        }
                        permutation_pending = true;
                    }
                }
                if (deletion_changed) {
                    PatchSourceFuzzyPresentationResult checked =
                        patch_source_fuzzy_presentation_check(
                            &presentation, deletion_offset, deletion_count
                        );
                    if (checked == PATCH_FUZZY_PRESENTATION_FAILED) {
                        outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                        goto finished;
                    }
                    if (checked != PATCH_FUZZY_PRESENTATION_EQUIVALENT ||
                        !patch_source_fuzzy_joined_operation_empty(
                            source_hunk, block_start, block_end, '+')) {
                        PatchSlice merged;
                        PatchSourceFuzzyCharacterMergeResult merge_result;
                        PatchSourceFuzzyCharacterMergeMode merge_mode = PATCH_CHARACTER_MERGE_NORMAL;
                        /* Changed internal whitespace rejects ordinary merge;
                         * only whole-file presentation coverage may recover it. */
                        if (checked == PATCH_FUZZY_PRESENTATION_WHITESPACE_CHANGED) {
                            if (!whole_file) {
                                outcome = PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED;
                                goto finished;
                            }
                            merge_mode = PATCH_CHARACTER_MERGE_FULLY_REPLACED_ONLY;
                        } else if (whole_file && checked == PATCH_FUZZY_PRESENTATION_EQUIVALENT) {
                            merge_mode = PATCH_CHARACTER_MERGE_ALLOW_FULLY_REPLACED;
                        }
                        if (merge.backing == NULL &&
                            !patch_source_fuzzy_character_merge_scratch_init(
                                &merge, original, corrected)) {
                            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                            goto finished;
                        }
                        merge_result = patch_source_fuzzy_character_merge(
                            &merged, &merge, source_hunk, target_hunk,
                            block_start, block_end, merge_mode);
                        if (merge_result == PATCH_CHARACTER_MERGE_MERGED) {
                            /* merged borrows merge scratch until its next use, so
                             * Echo's post-merge permutation gate must run here. */
                            if (permutation_pending) {
                                PatchSourceFuzzyPermutationResult checked =
                                    patch_source_fuzzy_permutation_text(
                                        &permutation, source_hunk, target_hunk,
                                        block_start, block_end, merged);
                                if (checked != PATCH_PERMUTATION_CLEAR) {
                                    outcome = checked == PATCH_PERMUTATION_FOUND
                                        ? PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED
                                        : PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                                    goto finished;
                                }
                            }
                            PatchProvenanceRecordResult recorded =
                                patch_provenance_record_merged(
                                    &pass, target_hunk, block_start, block_end, merged);
                            if (recorded != PATCH_PROVENANCE_RECORD_OK) {
                                outcome = recorded == PATCH_PROVENANCE_RECORD_REJECTED
                                    ? PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED
                                    : PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                                goto finished;
                            }
                        } else if (merge_result == PATCH_CHARACTER_MERGE_FULLY_REPLACED) {
                            PatchSourceFuzzyAlignedEvidenceResult collected;
                            PatchSlice *entries = NULL;
                            size_t capacity = 0, added;
                            if (pass.evidence != NULL) {
                                if (pass.evidence_count > pass.evidence_capacity) {
                                    outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                                    goto finished;
                                }
                                entries = pass.evidence + pass.evidence_count;
                                capacity = pass.evidence_capacity - pass.evidence_count;
                            }
                            collected = patch_source_fuzzy_evidence_collect_aligned(
                                entries, capacity, &added, source_hunk, target_hunk,
                                block_start, block_end);
                            if (collected != PATCH_ALIGNED_EVIDENCE_OK ||
                                added > SIZE_MAX - pass.evidence_count) {
                                outcome = collected == PATCH_ALIGNED_EVIDENCE_REJECTED
                                    ? PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED
                                    : PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                                goto finished;
                            }
                            pass.evidence_count += added;
                        } else if (merge_result != PATCH_CHARACTER_MERGE_UNCHANGED) {
                            outcome = merge_result == PATCH_CHARACTER_MERGE_REJECTED
                                ? PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED
                                : PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                            goto finished;
                        }
                    }
                }
                deletion_offset += deletion_count;
            }
        }
        /* Read pending records as an overlay so rejection cannot mutate corrected.
         * Evidence comes from one line and contains no LF, making per-addition
         * checks equivalent to searching the LF-joined additions. */
        if (pass.records != NULL && pass.evidence_count != evidence_start) {
            size_t record_cursor = record_start;
            for (hunk_index = 0; hunk_index < target_file->hunk_count; hunk_index++) {
                PatchHunk *hunk = &target_file->hunks[hunk_index];
                size_t line_index;
                for (line_index = 0; line_index < hunk->line_count; line_index++) {
                    PatchLine *line = &hunk->lines[line_index];
                    PatchSlice content;
                    if (line->operation != '+') continue;
                    if (record_cursor < pass.record_count &&
                        pass.records[record_cursor].line == line) {
                        content = pass.records[record_cursor++].content;
                    } else content = line->content;
                    if (patch_source_fuzzy_evidence_contains(
                        pass.evidence + evidence_start,
                        pass.evidence_count - evidence_start, content
                    )) {
                        outcome = PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED;
                        goto finished;
                    }
                }
            }
            if (record_cursor != pass.record_count) {
                outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
                goto finished;
            }
        }
      }
      if (phase == 0) {
        size_t record_bytes, evidence_bytes, total;
        if (pass.record_count == 0 && pass.evidence_count == 0) break;
        if (pass.record_count > SIZE_MAX / sizeof(*pass.records)) {
            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            goto finished;
        }
        record_bytes = pass.record_count * sizeof(*pass.records);
        if (pass.evidence_count > SIZE_MAX / sizeof(*pass.evidence)) {
            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            goto finished;
        }
        evidence_bytes = pass.evidence_count * sizeof(*pass.evidence);
        if (evidence_bytes > SIZE_MAX - record_bytes ||
            pass.byte_count > SIZE_MAX - record_bytes - evidence_bytes) {
            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            goto finished;
        }
        total = record_bytes + evidence_bytes + pass.byte_count;
        backing = malloc(total);
        if (backing == NULL) {
            outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
            goto finished;
        }
        pass.records = (PatchProvenanceAssignment *)backing;
        pass.evidence = (PatchSlice *)(backing + record_bytes);
        pass.bytes = backing + record_bytes + evidence_bytes;
        pass.record_capacity = pass.record_count;
        pass.evidence_capacity = pass.evidence_count;
        pass.byte_capacity = pass.byte_count;
      } else if (pass.record_count != pass.record_capacity ||
        pass.evidence_count != pass.evidence_capacity ||
        pass.byte_count != pass.byte_capacity) {
        outcome = PATCH_SOURCE_FUZZY_PROVENANCE_FAILED;
        goto finished;
      }
    }
finished:
    patch_source_fuzzy_permutation_scratch_release(&permutation);
    patch_source_fuzzy_character_merge_scratch_release(&merge);
    patch_source_fuzzy_presentation_release(&presentation);
    if (outcome == PATCH_SOURCE_FUZZY_PROVENANCE_SUCCEEDED) {
        size_t index;
        for (index = 0; index < pass.record_count; index++) {
            pass.records[index].line->content = pass.records[index].content;
        }
        provenance.backing = backing;
        *result = provenance;
    } else free(backing);
    return outcome;
}

void patch_source_fuzzy_provenance_release(
    PatchSourceFuzzyProvenance *provenance
) {
    free(provenance->backing);
    memset(provenance, 0, sizeof(*provenance));
}
