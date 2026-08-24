#include "executor_worker_patch_source_fuzzy.h"

#include "executor_worker_patch_apply_exact.h"
#include "executor_worker_patch_source_fuzzy_document.h"
#include "executor_worker_patch_source_fuzzy_provenance.h"

#include <string.h>

#define PATCH_SOURCE_FUZZY_CHARACTER_EDITS 2

static bool patch_source_fuzzy_null_path(PatchSlice path) {
    static const char null_path[] = "/dev/null";
    return path.length == sizeof(null_path) - 1 &&
        memcmp(path.bytes, null_path, sizeof(null_path) - 1) == 0;
}

PatchSourceFuzzyResult patch_source_fuzzy_build(
    PatchFormattedText *candidate,
    const PatchDocument *original,
    const PatchSourceSet *sources
) {
    PatchSourceFuzzyDocument corrected = {0};
    uint8_t remaining_edits = PATCH_SOURCE_FUZZY_CHARACTER_EDITS;
    bool changed = false;
    size_t file_index;

    if (!patch_source_fuzzy_document_init(&corrected, original)) {
        return PATCH_SOURCE_FUZZY_FAILED;
    }

    for (file_index = 0; file_index < original->file_count; file_index++) {
        const PatchFile *file = &original->files[file_index];
        const PatchSourceText empty_source = {0};
        const PatchSourceText *source = patch_source_fuzzy_null_path(file->old_path)
            ? &empty_source
            : patch_source_set_find(sources, file->old_path);
        PatchAppliedText applied = {0};
        PatchExactResult exact;

        if (source == NULL) {
            patch_source_fuzzy_document_release(&corrected);
            return PATCH_SOURCE_FUZZY_REJECTED;
        }
        exact = patch_file_apply_exact(
            &applied, source->bytes, source->length, file
        );
        if (exact == PATCH_EXACT_SUCCEEDED) {
            patch_applied_text_release(&applied);
            continue;
        }
        if (exact == PATCH_EXACT_FAILED) {
            patch_source_fuzzy_document_release(&corrected);
            return PATCH_SOURCE_FUZZY_FAILED;
        }
        if (patch_source_fuzzy_null_path(file->old_path)) {
            patch_source_fuzzy_document_release(&corrected);
            return PATCH_SOURCE_FUZZY_REJECTED;
        }

        {
            PatchSourceFuzzyMatchPlan plan = {0};
            PatchSourceFuzzyMatchResult match = patch_source_fuzzy_match_file(
                &plan, file, source->bytes, source->length,
                remaining_edits
            );
            uint8_t used_edits = 0;
            size_t hunk_index;

            if (match != PATCH_FUZZY_MATCH_SUCCEEDED) {
                patch_source_fuzzy_document_release(&corrected);
                return match == PATCH_FUZZY_MATCH_REJECTED
                    ? PATCH_SOURCE_FUZZY_REJECTED
                    : PATCH_SOURCE_FUZZY_FAILED;
            }
            for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
                uint8_t edits = plan.hunks[hunk_index].character_edits;
                if (edits > remaining_edits - used_edits) {
                    patch_source_fuzzy_match_plan_release(&plan);
                    patch_source_fuzzy_document_release(&corrected);
                    return PATCH_SOURCE_FUZZY_FAILED;
                }
                used_edits += edits;
            }
            if (!patch_source_fuzzy_document_apply_file(
                &corrected, file_index, &plan
            )) {
                patch_source_fuzzy_match_plan_release(&plan);
                patch_source_fuzzy_document_release(&corrected);
                return PATCH_SOURCE_FUZZY_FAILED;
            }
            patch_source_fuzzy_match_plan_release(&plan);
            remaining_edits -= used_edits;
            changed = true;
        }
    }

    if (!changed) {
        patch_source_fuzzy_document_release(&corrected);
        return PATCH_SOURCE_FUZZY_EXACT;
    }
    {
        PatchSourceFuzzyProvenance provenance = {0};
        PatchSourceFuzzyProvenanceResult result =
            patch_source_fuzzy_provenance_reconcile(
                &provenance, original, &corrected, sources
            );
        bool formatted = false;
        if (result == PATCH_SOURCE_FUZZY_PROVENANCE_SUCCEEDED) {
            formatted = patch_document_format(candidate, &corrected.document);
        }
        patch_source_fuzzy_provenance_release(&provenance);
        patch_source_fuzzy_document_release(&corrected);
        if (result == PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED) {
            return PATCH_SOURCE_FUZZY_REJECTED;
        }
        if (result == PATCH_SOURCE_FUZZY_PROVENANCE_FAILED || !formatted) {
            return PATCH_SOURCE_FUZZY_FAILED;
        }
    }
    return PATCH_SOURCE_FUZZY_CANDIDATE;
}
