#include "executor_worker.h"

#include "executor_worker_patch_commit.h"
#include "executor_worker_patch_candidates.h"
#include "executor_worker_patch_git_path.h"
#include "executor_worker_patch_normalize.h"
#include "executor_worker_patch_parser.h"
#include "executor_worker_patch_preview.h"
#include "executor_worker_patch_source.h"

IpcStatus executor_worker_patch_apply(
    uv_loop_t *loop,
    const uint8_t *patch,
    size_t patch_length,
    const uint8_t *cwd,
    size_t cwd_length
) {
    PatchDocument document = {0};
    PatchGitPathText prepared = {0};
    PatchNormalizedText normalized = {0};
    PatchPreview preview = {0};
    PatchCommitResult commit_result;
    PatchPreviewResult preview_result;
    IpcStatus status = IPC_STATUS_FAILED;

    if (!patch_normalize(&normalized, patch, patch_length)) return IPC_STATUS_FAILED;
    if (!patch_git_paths_prepare(&prepared, normalized.bytes, normalized.length)) {
        patch_normalized_text_release(&normalized);
        return IPC_STATUS_FAILED;
    }
    if (!patch_document_parse(&document, prepared.bytes, prepared.length)) {
        patch_git_path_text_release(&prepared);
        patch_normalized_text_release(&normalized);
        return IPC_STATUS_FAILED;
    }
    preview_result = patch_preview_build(
        &preview, loop, cwd, cwd_length, &document
    );
    if (preview_result == PATCH_PREVIEW_FAILED) {
        patch_document_release(&document);
        patch_git_path_text_release(&prepared);
        patch_normalized_text_release(&normalized);
        return IPC_STATUS_FAILED;
    }
    if (preview_result == PATCH_PREVIEW_MISMATCH) {
        PatchCandidatePlan plan = {0};
        PatchSourceSet sources = {0};
        PatchSlice original_text = {prepared.bytes, prepared.length};
        size_t candidate_index;
        bool preview_succeeded = false;

        if (!patch_source_set_build(
            &sources, loop, cwd, cwd_length, &document
        )) {
            patch_document_release(&document);
            patch_git_path_text_release(&prepared);
            patch_normalized_text_release(&normalized);
            return IPC_STATUS_FAILED;
        }
        if (!patch_candidate_plan_build(
            &plan, original_text, &document, &sources)) {
            patch_source_set_release(&sources);
            patch_document_release(&document);
            patch_git_path_text_release(&prepared);
            patch_normalized_text_release(&normalized);
            return IPC_STATUS_FAILED;
        }
        patch_source_set_release(&sources);
        patch_document_release(&document);
        patch_git_path_text_release(&prepared);
        patch_normalized_text_release(&normalized);

        for (candidate_index = 0; candidate_index < plan.count; candidate_index++) {
            PatchDocument candidate_document = {0};
            PatchFormattedText *candidate = &plan.texts[candidate_index];
            if (!patch_document_parse(
                &candidate_document, candidate->bytes, candidate->length)) {
                patch_candidate_plan_release(&plan);
                return IPC_STATUS_FAILED;
            }
            preview_result = patch_preview_build(
                &preview, loop, cwd, cwd_length, &candidate_document);
            patch_document_release(&candidate_document);
            if (preview_result == PATCH_PREVIEW_FAILED) {
                patch_candidate_plan_release(&plan);
                return IPC_STATUS_FAILED;
            }
            if (preview_result == PATCH_PREVIEW_SUCCEEDED) {
                preview_succeeded = true;
                break;
            }
        }
        patch_candidate_plan_release(&plan);
        if (!preview_succeeded) return IPC_STATUS_FAILED;
    } else {
        patch_document_release(&document);
        patch_git_path_text_release(&prepared);
        patch_normalized_text_release(&normalized);
    }
    commit_result = patch_preview_commit(loop, &preview);
    patch_preview_release(&preview);

    /* Preserve uncertain filesystem outcomes so callers cannot retry a patch
     * whose requested mutations may already have happened. */
    switch (commit_result) {
        case PATCH_COMMIT_SUCCEEDED: status = IPC_STATUS_OK; break;
        case PATCH_COMMIT_FAILED: status = IPC_STATUS_FAILED; break;
        case PATCH_COMMIT_UNCERTAIN: status = IPC_STATUS_UNCERTAIN; break;
    }
    return status;
}

void executor_worker_handle_patch_request(
    ExecutorWorker *worker,
    const IpcHeader *header,
    const IpcPatchRequestHeader *request
) {
    const uint8_t *patch;
    const uint8_t *cwd;
    size_t remaining;
    IpcHeader response = {
        .request_id = header->request_id,
        .type = IPC_REQUEST_PATCH,
        .response_type = IPC_SINGLE
    };

    if (header->body_length < sizeof(*request)) {
        response.status = IPC_STATUS_FAILED;
        executor_worker_output(worker, &response, NULL);
        return;
    }

    remaining = header->body_length - sizeof(*request);
    if (request->patch_length > remaining ||
        request->cwd_length != remaining - request->patch_length) {
        response.status = IPC_STATUS_FAILED;
        executor_worker_output(worker, &response, NULL);
        return;
    }

    patch = (const uint8_t *)(request + 1);
    cwd = patch + request->patch_length;
    response.status = executor_worker_patch_apply(
        &worker->loop,
        patch,
        request->patch_length,
        cwd,
        request->cwd_length
    );
    executor_worker_output(worker, &response, NULL);
}
