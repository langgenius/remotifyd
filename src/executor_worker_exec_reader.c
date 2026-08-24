#include "executor_worker_exec_reader.h"

#include "executor_worker_exec.h"
#include "logger.h"

#include <errno.h>
#include <iconv.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <langinfo.h>
#include <locale.h>
#endif

enum {
    EXECUTOR_WORKER_EXEC_READ_BYTES = 128 * 1024,
    EXECUTOR_WORKER_EXEC_INPUT_BYTES = EXECUTOR_WORKER_EXEC_PENDING_BYTES + EXECUTOR_WORKER_EXEC_READ_BYTES,
    EXECUTOR_WORKER_EXEC_UTF8_BYTES = EXECUTOR_WORKER_EXEC_INPUT_BYTES * 4,
    EXECUTOR_WORKER_EXEC_ALLOC_BYTES = EXECUTOR_WORKER_EXEC_INPUT_BYTES + sizeof(IpcExecReportHeader) + EXECUTOR_WORKER_EXEC_UTF8_BYTES
};

static const char executor_worker_exec_stdout_hex[] = "[remotify stdout_hex] ";
static const char executor_worker_exec_stderr_hex[] = "[remotify stderr_hex] ";

static bool executor_worker_exec_utf8(const uint8_t *input, size_t input_length) {
    iconv_t converter = iconv_open("UTF-8", "UTF-8");
    char *input_cursor = (char *)input;
    size_t input_remaining = input_length;
    char output[256];

    if (converter == (iconv_t)-1) return false;

    while (input_remaining != 0) {
        char *output_cursor = output;
        size_t output_remaining = sizeof(output);
        size_t result = iconv(converter, &input_cursor, &input_remaining, &output_cursor, &output_remaining);

        if (result != (size_t)-1) break;
        if (errno == E2BIG) continue;

        int conversion_error = errno;
        iconv_close(converter);
        return conversion_error == EINVAL;
    }

    iconv_close(converter);
    return true;
}

static const char *executor_worker_exec_native_encoding(char encoding[32]) {
#ifdef _WIN32
    snprintf(encoding, 32, "CP%u", (unsigned int)GetOEMCP());
    return encoding;
#else
    (void)encoding;
    setlocale(LC_CTYPE, "");
    return nl_langinfo(CODESET);
#endif
}

static iconv_t executor_worker_exec_iconv_detect(
    const uint8_t *input,
    size_t input_length,
    bool allow_bom,
    size_t *bom_length
) {
    const char *source_encoding;
    char native_encoding[32];

    *bom_length = 0;

    if (allow_bom && input_length >= 3 && input[0] == 0xef && input[1] == 0xbb && input[2] == 0xbf) {
        source_encoding = "UTF-8";
        *bom_length = 3;
    } else if (allow_bom && input_length >= 2 && input[0] == 0xff && input[1] == 0xfe) {
        source_encoding = "UTF-16LE";
        *bom_length = 2;
    } else if (allow_bom && input_length >= 2 && input[0] == 0xfe && input[1] == 0xff) {
        source_encoding = "UTF-16BE";
        *bom_length = 2;
    } else if (executor_worker_exec_utf8(input, input_length)) {
        source_encoding = "UTF-8";
    } else {
        source_encoding = executor_worker_exec_native_encoding(native_encoding);
    }

    /* This converter remains attached to one concrete output pipe until close. */
    return iconv_open("UTF-8", source_encoding);
}

static size_t executor_worker_exec_hex(
    uint8_t *output,
    const char *label,
    const uint8_t *input,
    size_t input_length
) {
    static const uint8_t digits[] = "0123456789abcdef";
    size_t label_length = strlen(label);
    size_t cursor = label_length;

    memcpy(output, label, label_length);
    for (size_t index = 0; index < input_length; index++) {
        output[cursor++] = digits[input[index] >> 4];
        output[cursor++] = digits[input[index] & 0x0f];
    }
    output[cursor++] = '\n';
    return cursor;
}

static bool executor_worker_exec_iconv(
    iconv_t converter,
    uint8_t pending[EXECUTOR_WORKER_EXEC_PENDING_BYTES],
    size_t *pending_length,
    uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length
) {
    char *input_cursor = (char *)input;
    size_t input_remaining = input_length;
    char *output_cursor = (char *)output;
    size_t output_remaining = output_capacity;

    *output_length = 0;

    while (input_remaining != 0) {
        size_t result = iconv(converter, &input_cursor, &input_remaining, &output_cursor, &output_remaining);

        if (result != (size_t)-1) break;

        if (errno == EINVAL) {
            /*
             * iconv leaves an incomplete trailing character unconsumed. The
             * caller keeps only that suffix and prepends it to the next read.
             */
            if (input_remaining > EXECUTOR_WORKER_EXEC_PENDING_BYTES) return false;
            memcpy(pending, input_cursor, input_remaining);
            *pending_length = input_remaining;
            break;
        }

        /* E2BIG and EILSEQ are conversion failures reported to the caller. */
        return false;
    }

    *output_length = (size_t)(output_cursor - (char *)output);
    return true;
}

static void executor_worker_exec_convert(
    iconv_t *converter,
    uint8_t pending[EXECUTOR_WORKER_EXEC_PENDING_BYTES],
    size_t *pending_length,
    bool *started,
    bool *hex,
    const char *hex_label,
    uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t *output_length
) {
    size_t ascii_length = 0;
    size_t bom_length = 0;
    size_t converted_length = 0;

    if (*hex) {
        *output_length = executor_worker_exec_hex(output, hex_label, input, input_length);
        return;
    }

    if (*converter == (iconv_t)-1) {
        while (ascii_length < input_length && input[ascii_length] < 0x80) {
            ascii_length++;
        }
        memcpy(output, input, ascii_length);

        if (ascii_length == input_length) {
            *started = *started || ascii_length != 0;
            *output_length = ascii_length;
            return;
        }

        *converter = executor_worker_exec_iconv_detect(
            input + ascii_length,
            input_length - ascii_length,
            !*started && ascii_length == 0,
            &bom_length
        );
        if (*converter == (iconv_t)-1) goto hexadecimal;

        if (!executor_worker_exec_iconv(
                *converter,
                pending,
                pending_length,
                input + ascii_length + bom_length,
                input_length - ascii_length - bom_length,
                output + ascii_length,
                EXECUTOR_WORKER_EXEC_UTF8_BYTES - ascii_length,
                &converted_length
            )) goto hexadecimal;

        *started = true;
        *output_length = ascii_length + converted_length;
        return;
    }

    if (executor_worker_exec_iconv(
            *converter,
            pending,
            pending_length,
            input,
            input_length,
            output,
            EXECUTOR_WORKER_EXEC_UTF8_BYTES,
            output_length
        )) {
        return;
    }

hexadecimal:
    *hex = true;
    *pending_length = 0;
    *output_length = executor_worker_exec_hex(output, hex_label, input, input_length);
}

static void executor_worker_exec_pending_hex(
    ExecutorCommand *command,
    uint8_t pending[EXECUTOR_WORKER_EXEC_PENDING_BYTES],
    size_t *pending_length,
    const char *label,
    bool stdout_stream
) {
    struct ExecutorWorkerExecPendingReport {
        IpcExecReportHeader report;
        uint8_t bytes[64];
    } payload = {0};

    if (*pending_length == 0) return;

    if (stdout_stream) {
        payload.report.stdout_length = (uint32_t)executor_worker_exec_hex(
            payload.bytes, label, pending, *pending_length
        );
    } else {
        payload.report.stderr_length = (uint32_t)executor_worker_exec_hex(
            payload.bytes, label, pending, *pending_length
        );
    }
    *pending_length = 0;

    if (!executor_worker_exec_report(command, IPC_STREAM, &payload.report)) {
        logf_error("failed to report incomplete command output");
    }
}

void executor_worker_exec_stdout_end(ExecutorCommand *command) {
    executor_worker_exec_pending_hex(
        command,
        command->output_pending,
        &command->output_pending_length,
        executor_worker_exec_stdout_hex,
        true
    );
    uv_read_stop((uv_stream_t *)&command->output);
    uv_close((uv_handle_t *)&command->output, executor_worker_exec_handle_closed);
    executor_worker_exec_terminal_event(command);
}

void executor_worker_exec_stderr_end(ExecutorCommand *command) {
    executor_worker_exec_pending_hex(
        command,
        command->error_pending,
        &command->error_pending_length,
        executor_worker_exec_stderr_hex,
        false
    );
    uv_read_stop((uv_stream_t *)&command->error);
    uv_close((uv_handle_t *)&command->error, executor_worker_exec_handle_closed);
    executor_worker_exec_terminal_event(command);
}

void executor_worker_exec_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer) {
    uint8_t *allocation;

    (void)handle;
    (void)suggested_size;

    allocation = malloc(EXECUTOR_WORKER_EXEC_ALLOC_BYTES);

    if (allocation == NULL) {
        buffer->base = NULL;
        buffer->len = 0;
        return;
    }

    /* The prefix is reserved for an incomplete character from the prior read. */
    buffer->base = (char *)allocation + EXECUTOR_WORKER_EXEC_PENDING_BYTES;
    buffer->len = EXECUTOR_WORKER_EXEC_READ_BYTES;
}

void executor_worker_exec_stdout_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer) {
    /* uv_spawn attached this command to the concrete stdout pipe handle. */
    ExecutorCommand *command = stream->data;
    uint8_t *allocation = NULL;
    uint8_t *input;
    size_t input_length;
    IpcExecReportHeader *report;
    uint8_t *utf8;
    size_t utf8_length;
    bool was_hex;

    if (received < 0) {
        if (buffer->base != NULL) {
            allocation = (uint8_t *)buffer->base - EXECUTOR_WORKER_EXEC_PENDING_BYTES;
        }
        if (received != UV_EOF) logf_error("failed to read command stdout: %s", uv_strerror((int)received));
        executor_worker_exec_stdout_end(command);
        free(allocation);
        return;
    }

    /*
     * executor_worker_exec_alloc returns a pointer after the reserved pending
     * prefix. Moving back by that fixed size recovers the address that must be
     * passed to free(). The complete allocation is laid out as:
     *
     *   [ pending prefix ][ libuv read area ][ report header ][ UTF-8 output ]
     */
    allocation = (uint8_t *)buffer->base - EXECUTOR_WORKER_EXEC_PENDING_BYTES;

    /*
     * libuv wrote the new bytes starting at buffer->base. Starting input just
     * before that address makes the retained suffix and the new read one
     * continuous iconv input without copying the new bytes.
     */
    input = (uint8_t *)buffer->base - command->output_pending_length;
    input_length = command->output_pending_length + (size_t)received;

    if (command->output_pending_length != 0) {
        /* Only the incomplete suffix from the preceding read is copied. */
        memcpy(input, command->output_pending, command->output_pending_length);
    }

    /* iconv may set this again if the new read also ends mid-character. */
    command->output_pending_length = 0;

    /*
     * The output begins after the complete reserved input region, not after
     * received bytes, so input and output can never overlap.
     */
    report = (IpcExecReportHeader *)(allocation + EXECUTOR_WORKER_EXEC_INPUT_BYTES);
    utf8 = (uint8_t *)(report + 1);

    was_hex = command->output_hex;
    executor_worker_exec_convert(
            &command->output_iconv,
            command->output_pending,
            &command->output_pending_length,
            &command->output_started,
            &command->output_hex,
            executor_worker_exec_stdout_hex,
            input,
            input_length,
            utf8,
            &utf8_length
        );
    if (!was_hex && command->output_hex) {
        logf_error("failed to convert command stdout to UTF-8");
    }

    if (utf8_length != 0) {
        report->exit_code = 0;
        report->stdout_length = (uint32_t)utf8_length;
        report->stderr_length = 0;
        executor_worker_exec_report(command, IPC_STREAM, report);
    }

    free(allocation);
}

void executor_worker_exec_stderr_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer) {
    ExecutorCommand *command = stream->data;
    uint8_t *allocation = NULL;
    uint8_t *input;
    size_t input_length;
    IpcExecReportHeader *report;
    uint8_t *utf8;
    size_t utf8_length;
    bool was_hex;

    if (received < 0) {
        if (buffer->base != NULL) {
            allocation = (uint8_t *)buffer->base - EXECUTOR_WORKER_EXEC_PENDING_BYTES;
        }
        if (received != UV_EOF) logf_error("failed to read command stderr: %s", uv_strerror((int)received));
        executor_worker_exec_stderr_end(command);
        free(allocation);
        return;
    }

    /* Recover the start of [pending][read][UTF-8] allocated by the alloc callback. */
    allocation = (uint8_t *)buffer->base - EXECUTOR_WORKER_EXEC_PENDING_BYTES;

    /* Place stderr's retained suffix immediately before the new libuv bytes. */
    input = (uint8_t *)buffer->base - command->error_pending_length;
    input_length = command->error_pending_length + (size_t)received;

    if (command->error_pending_length != 0) {
        memcpy(input, command->error_pending, command->error_pending_length);
    }

    /* iconv may populate this again when the current read ends mid-character. */
    command->error_pending_length = 0;
    report = (IpcExecReportHeader *)(allocation + EXECUTOR_WORKER_EXEC_INPUT_BYTES);
    utf8 = (uint8_t *)(report + 1);

    was_hex = command->error_hex;
    executor_worker_exec_convert(
            &command->error_iconv,
            command->error_pending,
            &command->error_pending_length,
            &command->error_started,
            &command->error_hex,
            executor_worker_exec_stderr_hex,
            input,
            input_length,
            utf8,
            &utf8_length
        );
    if (!was_hex && command->error_hex) {
        logf_error("failed to convert command stderr to UTF-8");
    }

    if (utf8_length != 0) {
        report->exit_code = 0;
        report->stdout_length = 0;
        report->stderr_length = (uint32_t)utf8_length;
        executor_worker_exec_report(command, IPC_STREAM, report);
    }

    free(allocation);
}
