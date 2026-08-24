#include "executor_worker_patch_model_escape.h"

#include "executor_worker_patch_apply_exact.h"
#include "executor_worker_patch_json_unicode.h"
#include "executor_worker_patch_model_escape_document.h"
#include "executor_worker_patch_source_fuzzy.h"
#include "executor_worker_patch_unicode.h"

#include <stdlib.h>
#include <string.h>

typedef struct PatchModelLegacyScratch {
    uint8_t *legacy;
    uint8_t *decoded;
    PatchSlice *segments;
    size_t byte_capacity;
    size_t segment_capacity;
} PatchModelLegacyScratch;

typedef enum PatchModelLegacyResult {
    PATCH_MODEL_LEGACY_FAILED,
    PATCH_MODEL_LEGACY_REJECTED,
    PATCH_MODEL_LEGACY_UNCHANGED,
    PATCH_MODEL_LEGACY_CHANGED
} PatchModelLegacyResult;

typedef struct PatchModelSourceLines {
    PatchSlice *lines;
    size_t count;
} PatchModelSourceLines;

typedef struct PatchModelOccurrences {
    size_t count;
    size_t first;
} PatchModelOccurrences;

static void patch_model_legacy_scratch_release(PatchModelLegacyScratch *scratch) {
    free(scratch->legacy);
    free(scratch->decoded);
    free(scratch->segments);
    memset(scratch, 0, sizeof(*scratch));
}

static bool patch_model_eligible_line(const PatchLine *line) {
    return line->operation == ' ' || line->operation == '-';
}

static bool patch_model_null_path(PatchSlice path) {
    static const uint8_t null_path[] = "/dev/null";

    return path.length == sizeof(null_path) - 1 &&
        memcmp(path.bytes, null_path, sizeof(null_path) - 1) == 0;
}

static bool patch_model_slice_equal(PatchSlice left, PatchSlice right) {
    return left.length == right.length &&
        (left.length == 0 || memcmp(left.bytes, right.bytes, left.length) == 0);
}

static void patch_model_source_lines_release(PatchModelSourceLines *lines) {
    free(lines->lines);
    memset(lines, 0, sizeof(*lines));
}

static bool patch_model_source_lines_build(
    PatchModelSourceLines *result,
    const PatchSourceText *source
) {
    PatchModelSourceLines lines = {0};
    size_t count = 1;
    size_t start = 0;
    size_t cursor;
    size_t index = 0;

    /* Model escape deliberately treats an empty source as one empty line;
     * source-fuzzy has a different split contract and must not reuse this. */
    for (cursor = 0; cursor < source->length; cursor++) {
        if (source->bytes[cursor] == '\n') count++;
    }
    if (source->length != 0 && source->bytes[source->length - 1] == '\n') count--;
    if (count > SIZE_MAX / sizeof(*lines.lines)) return false;
    lines.lines = malloc(count * sizeof(*lines.lines));
    if (lines.lines == NULL) return false;
    lines.count = count;

    for (cursor = 0; cursor < source->length; cursor++) {
        PatchSlice line;

        if (source->bytes[cursor] != '\n') continue;
        if (index == count) {
            start = cursor + 1;
            continue;
        }
        line.bytes = source->bytes + start;
        line.length = cursor - start;
        if (line.length != 0 && line.bytes[line.length - 1] == '\r') line.length--;
        lines.lines[index++] = line;
        start = cursor + 1;
    }
    if (index < count) {
        PatchSlice line = {
            .bytes = source->length == 0 ? source->bytes : source->bytes + start,
            .length = source->length - start
        };

        if (line.length != 0 && line.bytes[line.length - 1] == '\r') line.length--;
        lines.lines[index++] = line;
    }
    if (index != count) {
        patch_model_source_lines_release(&lines);
        return false;
    }
    *result = lines;
    return true;
}

static bool patch_model_find_occurrences(
    PatchModelOccurrences *result,
    const PatchModelSourceLines *source,
    const PatchSlice *sequence,
    size_t sequence_count,
    size_t limit,
    size_t *remaining_comparisons
) {
    PatchModelOccurrences occurrences = {0};
    size_t start;

    for (start = 0; start < source->count; start++) {
        size_t index;
        bool matches = true;

        /* Go indexes candidate starts by the first source line, so this
         * prefilter is free while the inner comparison includes line zero. */
        if (!patch_model_slice_equal(source->lines[start], sequence[0])) continue;
        /* An indexed start that cannot fit the sequence is an invariant
         * rejection in Echo, not an ordinary source no-match. */
        if (sequence_count > source->count - start) return false;
        for (index = 0; index < sequence_count; index++) {
            if (*remaining_comparisons == 0) return false;
            (*remaining_comparisons)--;
            if (!patch_model_slice_equal(
                source->lines[start + index], sequence[index]
            )) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        if (occurrences.count == 0) occurrences.first = start;
        occurrences.count++;
        if (occurrences.count == limit) break;
    }
    *result = occurrences;
    return true;
}

static bool patch_model_legacy_scratch_init(
    PatchModelLegacyScratch *scratch,
    const PatchDocument *document
) {
    size_t file_index;

    scratch->segment_capacity = 1;
    for (file_index = 0; file_index < document->file_count; file_index++) {
        const PatchFile *file = &document->files[file_index];
        size_t hunk_index;

        for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            const PatchHunk *hunk = &file->hunks[hunk_index];
            size_t line_index;

            for (line_index = 0; line_index < hunk->line_count; line_index++) {
                const PatchLine *line = &hunk->lines[line_index];
                size_t segments = 1;
                size_t cursor;

                if (!patch_model_eligible_line(line)) continue;
                if (line->content.length > scratch->byte_capacity) {
                    scratch->byte_capacity = line->content.length;
                }
                for (cursor = 0; cursor + 1 < line->content.length;) {
                    if (line->content.bytes[cursor] == '\\' &&
                        line->content.bytes[cursor + 1] == 'n') {
                        segments++;
                        cursor += 2;
                    } else cursor++;
                }
                if (segments > scratch->segment_capacity) {
                    scratch->segment_capacity = segments;
                }
            }
        }
    }
    if (scratch->segment_capacity > SIZE_MAX / sizeof(*scratch->segments)) {
        return false;
    }
    scratch->segments = malloc(
        scratch->segment_capacity * sizeof(*scratch->segments)
    );
    if (scratch->segments == NULL) return false;
    if (scratch->byte_capacity != 0) {
        scratch->legacy = malloc(scratch->byte_capacity);
        scratch->decoded = malloc(scratch->byte_capacity);
        if (scratch->legacy == NULL || scratch->decoded == NULL) {
            patch_model_legacy_scratch_release(scratch);
            return false;
        }
    }
    return true;
}

/* Returned segment slices borrow decoded and remain valid only until the next
 * line transform reuses this request-scoped scratch. */
static PatchModelLegacyResult patch_model_legacy_line(
    PatchModelLegacyScratch *scratch,
    PatchSlice payload,
    size_t *segment_count
) {
    size_t input = 0;
    size_t legacy_length = 0;
    size_t segment_start = 0;
    size_t count = 0;
    size_t decoded_length = 0;
    bool changed = false;
    size_t index;

    /* The two-byte \" and literal \n patterns cannot overlap each other;
     * replacing the former cannot create the latter, so one left-to-right
     * scan is equivalent to Go's ReplaceAll followed by Split. */
    while (input < payload.length) {
        if (input + 1 < payload.length && payload.bytes[input] == '\\') {
            if (payload.bytes[input + 1] == '"') {
                scratch->legacy[legacy_length++] = '"';
                input += 2;
                changed = true;
                continue;
            }
            if (payload.bytes[input + 1] == 'n') {
                scratch->segments[count].bytes = scratch->legacy + segment_start;
                scratch->segments[count++].length = legacy_length - segment_start;
                segment_start = legacy_length;
                input += 2;
                changed = true;
                continue;
            }
        }
        scratch->legacy[legacy_length++] = payload.bytes[input++];
    }
    scratch->segments[count].bytes = legacy_length == 0
        ? scratch->legacy : scratch->legacy + segment_start;
    scratch->segments[count++].length = legacy_length - segment_start;

    for (index = 0; index < count; index++) {
        PatchSlice segment = scratch->segments[index];
        size_t written;
        PatchJsonUnicodeResult result = patch_json_unicode_decode_once_into(
            scratch->decoded == NULL ? NULL : scratch->decoded + decoded_length,
            scratch->byte_capacity - decoded_length,
            &written,
            segment.bytes,
            segment.length
        );

        if (result == PATCH_JSON_UNICODE_FAILED) return PATCH_MODEL_LEGACY_FAILED;
        if (result == PATCH_JSON_UNICODE_REJECTED) return PATCH_MODEL_LEGACY_REJECTED;
        scratch->segments[index].bytes = scratch->decoded == NULL
            ? NULL : scratch->decoded + decoded_length;
        scratch->segments[index].length = written;
        decoded_length += written;
        if (result == PATCH_JSON_UNICODE_DECODED) changed = true;
    }
    *segment_count = count;
    return changed ? PATCH_MODEL_LEGACY_CHANGED : PATCH_MODEL_LEGACY_UNCHANGED;
}

static PatchModelEscapeResult patch_model_check_file(
    PatchModelEscapeDocument *corrected_document,
    const PatchFile *file,
    PatchFile *corrected,
    const PatchSourceText *source,
    const PatchSourceSet *sources,
    PatchModelLegacyScratch *scratch,
    size_t *remaining_comparisons
) {
    PatchAppliedText applied = {0};
    PatchModelSourceLines source_lines = {0};
    PatchExactResult exact = patch_file_apply_exact(
        &applied, source->bytes, source->length, file
    );
    size_t minimum_proof_start = 0;
    size_t hunk_index;
    bool changed = false;
    bool requires_source_fuzzy_proof = false;

    if (exact == PATCH_EXACT_FAILED) return PATCH_MODEL_ESCAPE_FAILED;
    if (exact == PATCH_EXACT_SUCCEEDED) {
        patch_applied_text_release(&applied);
    }
    if (exact == PATCH_EXACT_MISMATCH &&
        !patch_model_source_lines_build(&source_lines, source)) {
        return PATCH_MODEL_ESCAPE_FAILED;
    }
    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        PatchHunk *corrected_hunk = &corrected->hunks[hunk_index];
        size_t line_index;

        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            const PatchLine *line = &hunk->lines[line_index];
            PatchModelOccurrences raw = {0};
            PatchModelOccurrences normalized = {0};
            PatchModelLegacyResult legacy;
            size_t segment_count;

            if (exact == PATCH_EXACT_SUCCEEDED || !patch_model_eligible_line(line)) {
                if (!patch_model_escape_document_append_original(
                    corrected_document, corrected_hunk, line
                )) {
                    patch_model_source_lines_release(&source_lines);
                    return PATCH_MODEL_ESCAPE_FAILED;
                }
                continue;
            }
            legacy = patch_model_legacy_line(scratch, line->content, &segment_count);
            if (legacy == PATCH_MODEL_LEGACY_FAILED) {
                patch_model_source_lines_release(&source_lines);
                return PATCH_MODEL_ESCAPE_FAILED;
            }
            if (legacy == PATCH_MODEL_LEGACY_UNCHANGED) {
                if (!patch_model_escape_document_append_original(
                    corrected_document, corrected_hunk, line
                )) {
                    patch_model_source_lines_release(&source_lines);
                    return PATCH_MODEL_ESCAPE_FAILED;
                }
                continue;
            }
            if (!patch_model_find_occurrences(
                &raw, &source_lines, &line->content, 1, 1, remaining_comparisons
            )) {
                patch_model_source_lines_release(&source_lines);
                return PATCH_MODEL_ESCAPE_REJECTED;
            }
            if (legacy == PATCH_MODEL_LEGACY_REJECTED) {
                if (raw.count == 0) {
                    patch_model_source_lines_release(&source_lines);
                    return PATCH_MODEL_ESCAPE_REJECTED;
                }
                if (!patch_model_escape_document_append_original(
                    corrected_document, corrected_hunk, line
                )) {
                    patch_model_source_lines_release(&source_lines);
                    return PATCH_MODEL_ESCAPE_FAILED;
                }
                continue;
            }
            if (raw.count != 0) {
                if (!patch_model_find_occurrences(
                    &normalized, &source_lines, scratch->segments,
                    segment_count, 1, remaining_comparisons
                ) || normalized.count != 0) {
                    patch_model_source_lines_release(&source_lines);
                    return PATCH_MODEL_ESCAPE_REJECTED;
                }
                if (!patch_model_escape_document_append_original(
                    corrected_document, corrected_hunk, line
                )) {
                    patch_model_source_lines_release(&source_lines);
                    return PATCH_MODEL_ESCAPE_FAILED;
                }
                continue;
            }
            if (!patch_unicode_strong_escape_proof(
                scratch->segments, segment_count
            ) || !patch_model_find_occurrences(
                &normalized, &source_lines, scratch->segments,
                segment_count, 2, remaining_comparisons
            )) {
                patch_model_source_lines_release(&source_lines);
                return PATCH_MODEL_ESCAPE_REJECTED;
            }
            if (normalized.count == 0) {
                requires_source_fuzzy_proof = true;
            } else if (normalized.count > 1 ||
                normalized.first < minimum_proof_start) {
                patch_model_source_lines_release(&source_lines);
                return PATCH_MODEL_ESCAPE_REJECTED;
            } else {
                /* The occurrence fit check proves this addition cannot overflow. */
                minimum_proof_start = normalized.first + segment_count;
            }
            if (!patch_model_escape_document_append_decoded(
                corrected_document, corrected_hunk,
                line, scratch->segments, segment_count
            )) {
                patch_model_source_lines_release(&source_lines);
                return PATCH_MODEL_ESCAPE_FAILED;
            }
            changed = true;
        }
    }
    patch_model_source_lines_release(&source_lines);
    if (requires_source_fuzzy_proof) {
        PatchDocument proof_document = {.files = corrected, .file_count = 1};
        PatchFormattedText proof = {0};
        PatchSourceFuzzyResult proof_result;

        /* A zero exact occurrence authorizes conversion only when the complete
         * corrected file passes source-fuzzy provenance. The document is a
         * non-owning call-only view and must not be released; text is proof-only. */
        proof_result = patch_source_fuzzy_build(&proof, &proof_document, sources);
        patch_formatted_text_release(&proof);
        if (proof_result == PATCH_SOURCE_FUZZY_FAILED) return PATCH_MODEL_ESCAPE_FAILED;
        if (proof_result == PATCH_SOURCE_FUZZY_REJECTED) return PATCH_MODEL_ESCAPE_REJECTED;
    }
    return changed ? PATCH_MODEL_ESCAPE_CANDIDATE : PATCH_MODEL_ESCAPE_UNCHANGED;
}

static bool patch_model_copy_file(
    PatchModelEscapeDocument *corrected_document,
    const PatchFile *file,
    PatchFile *corrected
) {
    size_t hunk_index;

    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        size_t line_index;
        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            if (!patch_model_escape_document_append_original(
                corrected_document, &corrected->hunks[hunk_index],
                &hunk->lines[line_index]
            )) return false;
        }
    }
    return true;
}

PatchModelEscapeResult patch_model_escape_build(
    PatchFormattedText *candidate,
    const PatchDocument *document,
    const PatchSourceSet *sources
) {
    PatchModelLegacyScratch scratch = {0};
    PatchModelEscapeDocument corrected = {0};
    size_t remaining_comparisons = 1000000;
    size_t file_index;
    bool changed = false;
    PatchModelEscapeResult outcome = PATCH_MODEL_ESCAPE_UNCHANGED;

    if (!patch_model_legacy_scratch_init(&scratch, document)) {
        return PATCH_MODEL_ESCAPE_FAILED;
    }
    if (!patch_model_escape_document_init(&corrected, document)) {
        patch_model_legacy_scratch_release(&scratch);
        return PATCH_MODEL_ESCAPE_FAILED;
    }
    for (file_index = 0; file_index < document->file_count; file_index++) {
        const PatchFile *file = &document->files[file_index];
        PatchFile *corrected_file = &corrected.document.files[file_index];
        const PatchSourceText *source;
        PatchModelEscapeResult result;

        if (patch_model_null_path(file->old_path)) {
            if (!patch_model_copy_file(&corrected, file, corrected_file)) {
                outcome = PATCH_MODEL_ESCAPE_FAILED;
                break;
            }
            continue;
        }
        source = patch_source_set_find(sources, file->old_path);
        if (source == NULL) {
            outcome = PATCH_MODEL_ESCAPE_REJECTED;
            break;
        }
        result = patch_model_check_file(
            &corrected, file, corrected_file,
            source, sources, &scratch, &remaining_comparisons
        );
        if (result == PATCH_MODEL_ESCAPE_FAILED ||
            result == PATCH_MODEL_ESCAPE_REJECTED) {
            outcome = result;
            break;
        }
        if (result == PATCH_MODEL_ESCAPE_CANDIDATE) changed = true;
    }
    if (outcome == PATCH_MODEL_ESCAPE_UNCHANGED && changed) {
        outcome = patch_document_format(candidate, &corrected.document)
            ? PATCH_MODEL_ESCAPE_CANDIDATE
            : PATCH_MODEL_ESCAPE_FAILED;
    }
    patch_model_escape_document_release(&corrected);
    patch_model_legacy_scratch_release(&scratch);
    return outcome;
}
