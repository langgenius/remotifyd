#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_FORMAT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_FORMAT_H

#include "executor_worker_patch_parser.h"

typedef struct PatchFormattedText {
    uint8_t *bytes;
    size_t length;
} PatchFormattedText;

/* Result must be zero on entry; success owns one allocation, failure stays zero. */
bool patch_file_format(PatchFormattedText *result, const PatchFile *file);
bool patch_document_format(PatchFormattedText *result, const PatchDocument *document);
void patch_formatted_text_release(PatchFormattedText *result);

#endif
