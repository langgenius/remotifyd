#include "executor_worker_patch_candidates.h"

#include "executor_worker_patch_model_escape.h"

#include <string.h>

static bool patch_candidate_equal(PatchFormattedText text, PatchSlice other) {
    return text.length == other.length && (text.length == 0 ||
        memcmp(text.bytes, other.bytes, text.length) == 0);
}

static bool patch_candidate_duplicate(
    const PatchCandidatePlan *plan, PatchSlice original, PatchFormattedText text
) {
    size_t index;
    if (patch_candidate_equal(text, original)) return true;
    for (index = 0; index < plan->count; index++) {
        PatchSlice prior = {plan->texts[index].bytes, plan->texts[index].length};
        if (patch_candidate_equal(text, prior)) return true;
    }
    return false;
}

static void patch_candidate_append(
    PatchCandidatePlan *plan, PatchSlice original, PatchFormattedText *text
) {
    if (patch_candidate_duplicate(plan, original, *text)) {
        patch_formatted_text_release(text);
        return;
    }
    plan->texts[plan->count++] = *text;
    memset(text, 0, sizeof(*text));
}

void patch_candidate_plan_release(PatchCandidatePlan *plan) {
    size_t index;
    for (index = 0; index < plan->count; index++) {
        patch_formatted_text_release(&plan->texts[index]);
    }
    memset(plan, 0, sizeof(*plan));
}

bool patch_candidate_plan_build(
    PatchCandidatePlan *result, PatchSlice original_text,
    const PatchDocument *original, const PatchSourceSet *sources
) {
    PatchCandidatePlan plan = {0};
    PatchFormattedText model = {0};
    PatchFormattedText fuzzy = {0};
    PatchFormattedText *model_text = NULL;
    PatchDocument model_document = {0};
    PatchModelEscapeResult model_result = patch_model_escape_build(
        &model, original, sources);
    PatchSourceFuzzyResult fuzzy_result;

    if (model_result == PATCH_MODEL_ESCAPE_FAILED) goto failed;
    if (model_result == PATCH_MODEL_ESCAPE_CANDIDATE) {
        if (patch_candidate_duplicate(&plan, original_text, model)) {
            /* Echo derives fuzzy(model) from the model outcome even when the
             * model text itself is deduplicated from the attempt list. */
            model_text = &model;
        } else {
            patch_candidate_append(&plan, original_text, &model);
            model_text = &plan.texts[plan.count - 1];
        }
    }

    fuzzy_result = patch_source_fuzzy_build(&fuzzy, original, sources);
    if (fuzzy_result == PATCH_SOURCE_FUZZY_FAILED) goto failed;
    if (fuzzy_result == PATCH_SOURCE_FUZZY_CANDIDATE) {
        patch_candidate_append(&plan, original_text, &fuzzy);
    }

    if (model_text != NULL) {
        if (!patch_document_parse(
            &model_document, model_text->bytes, model_text->length)) goto failed;
        fuzzy_result = patch_source_fuzzy_build(
            &fuzzy, &model_document, sources);
        patch_document_release(&model_document);
        if (fuzzy_result == PATCH_SOURCE_FUZZY_FAILED) goto failed;
        if (fuzzy_result == PATCH_SOURCE_FUZZY_CANDIDATE) {
            patch_candidate_append(&plan, original_text, &fuzzy);
        }
    }
    patch_formatted_text_release(&model);
    *result = plan;
    return true;

failed:
    patch_document_release(&model_document);
    patch_formatted_text_release(&fuzzy);
    patch_formatted_text_release(&model);
    patch_candidate_plan_release(&plan);
    return false;
}
