#include "executor_worker_patch_core_test.h"

#include "executor_worker_patch_format.h"
#include "executor_worker_patch_normalize.h"
#include "executor_worker_patch_parser.h"
#include "executor_worker_patch_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool slice_equals_base64(PatchSlice actual, const char *encoded) {
    size_t expected_length;
    uint8_t *expected = patch_test_decode_base64(encoded, &expected_length);
    bool same = expected != NULL && actual.length == expected_length &&
        memcmp(actual.bytes, expected, expected_length) == 0;

    free(expected);
    return same;
}

static bool bytes_contain_base64(
    const uint8_t *bytes,
    size_t length,
    const char *encoded
) {
    size_t expected_length;
    uint8_t *expected = patch_test_decode_base64(encoded, &expected_length);
    size_t cursor;
    bool found = false;

    if (expected == NULL) return false;
    for (cursor = 0; cursor <= length && expected_length <= length - cursor; cursor++) {
        if (memcmp(bytes + cursor, expected, expected_length) == 0) {
            found = true;
            break;
        }
    }
    free(expected);
    return found;
}

static bool line_equals_base64(const PatchLine *line, const char *encoded) {
    size_t expected_length;
    uint8_t *expected = patch_test_decode_base64(encoded, &expected_length);
    bool same = expected != NULL && expected_length == line->content.length + 1 &&
        expected[0] == line->operation &&
        memcmp(expected + 1, line->content.bytes, line->content.length) == 0;

    free(expected);
    return same;
}

static bool run_core_case(const EchoUnifiedDiffCase *test) {
    PatchNormalizedText normalized = {0};
    PatchDocument document = {0};
    PatchDocument formatted_document = {0};
    PatchFormattedText formatted = {0};
    PatchFormattedText formatted_again = {0};
    size_t patch_length = 0;
    uint8_t *patch = patch_test_decode_base64(test->patch_base64, &patch_length);
    const uint8_t *parsed_patch = patch;
    size_t parsed_patch_length = patch_length;
    size_t index;
    bool succeed = false;

    if (patch == NULL) goto cleanup;
    if (test->parse_before_normalization_fails) {
        PatchDocument raw = {0};

        if (patch_document_parse(&raw, patch, patch_length)) {
            patch_document_release(&raw);
            goto cleanup;
        }
        if (!patch_normalize(&normalized, patch, patch_length)) goto cleanup;
        parsed_patch = normalized.bytes;
        parsed_patch_length = normalized.length;
    }
    if (!patch_document_parse(&document, parsed_patch, parsed_patch_length) ||
        document.file_count == 0 ||
        (test->path_count != 0 && document.file_count != test->path_count)) goto cleanup;
    if (test->normalized_header_base64[0] != '\0' &&
        ((normalized.changes & PATCH_NORMALIZED_HUNK_COUNTS) == 0 ||
            !bytes_contain_base64(
                normalized.bytes, normalized.length, test->normalized_header_base64
            ))) goto cleanup;

    for (index = 0; index < test->path_count; index++) {
        if (!slice_equals_base64(
            document.files[index].old_path, test->paths[index].old_path_base64
        ) || !slice_equals_base64(
            document.files[index].new_path, test->paths[index].new_path_base64
        )) goto cleanup;
    }
    if (document.files[0].hunk_count == 0) goto cleanup;
    if (test->first_hunk_line_count != 0 &&
        document.files[0].hunks[0].line_count != test->first_hunk_line_count) goto cleanup;
    for (index = 0; index < test->first_hunk_line_count; index++) {
        if (!line_equals_base64(
                &document.files[0].hunks[0].lines[index],
                test->first_hunk_lines_base64[index]
            )) goto cleanup;
    }
    if ((test->check_old_start &&
            document.files[0].hunks[0].old_start != test->expected_old_start) ||
        (test->check_old_lines &&
            document.files[0].hunks[0].old_length != test->expected_old_lines) ||
        (test->check_new_lines &&
            document.files[0].hunks[0].new_length != test->expected_new_lines)) goto cleanup;

    if (!patch_file_format(&formatted, &document.files[0])) goto cleanup;
    for (index = 0; index < test->format_contains_count; index++) {
        if (!bytes_contain_base64(
            formatted.bytes, formatted.length, test->format_contains_base64[index]
        )) goto cleanup;
    }
    if (test->format_twice_equal) {
        if (!patch_file_format(&formatted_again, &document.files[0]) ||
            formatted.length != formatted_again.length ||
            memcmp(formatted.bytes, formatted_again.bytes, formatted.length) != 0) goto cleanup;
    }
    if (test->check_old_start &&
        document.files[0].hunks[0].old_start != test->expected_old_start) goto cleanup;
    if (!patch_document_parse(
        &formatted_document, formatted.bytes, formatted.length
    )) goto cleanup;
    succeed = true;

cleanup:
    patch_document_release(&formatted_document);
    patch_formatted_text_release(&formatted_again);
    patch_formatted_text_release(&formatted);
    patch_document_release(&document);
    patch_normalized_text_release(&normalized);
    free(patch);
    return succeed;
}

bool run_patch_core_tests(void) {
    size_t index;

    for (index = 0; index < echo_unified_diff_case_count; index++) {
        if (!run_core_case(&echo_unified_diff_cases[index])) {
            fprintf(stderr, "unified diff core case %zu failed\n", index + 1);
            return false;
        }
    }
    return true;
}
