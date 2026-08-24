#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_PARSER_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PatchSlice {
    const uint8_t *bytes;
    size_t length;
} PatchSlice;

typedef struct PatchLine {
    PatchSlice content;
    uint8_t operation;
    bool no_final_newline;
    bool crlf;
} PatchLine;

typedef struct PatchHunk {
    PatchLine *lines;
    size_t line_count;
    size_t old_start;
    size_t old_length;
    size_t new_start;
    size_t new_length;
} PatchHunk;

typedef struct PatchFile {
    PatchSlice old_path;
    PatchSlice new_path;
    PatchSlice old_header;
    PatchSlice new_header;
    PatchHunk *hunks;
    size_t hunk_count;
} PatchFile;

typedef struct PatchDocument {
    PatchFile *files;
    size_t file_count;
} PatchDocument;

/* Paths belong to the document; headers and line content borrow patch bytes. */
bool patch_document_parse(PatchDocument *document, const uint8_t *patch, size_t patch_length);
void patch_document_release(PatchDocument *document);

#endif
