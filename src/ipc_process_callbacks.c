#include "ipc_types.h"

#include <stdlib.h>
#include <string.h>

struct IpcProcessCallbackEntry {
    uint32_t device_request_id;
    uint64_t expires_at;
    uint64_t ttl;
    IpcProcessCallback callback;
    IpcProcessCallbackEntry *next;
};

static IpcProcessCallbackEntry *ipc_process_callbacks_find(
    IpcProcessCallbacks *callbacks,
    uint32_t device_request_id,
    IpcProcessCallbackEntry **previous
) {
    IpcProcessCallbackEntry *entry = callbacks->tail;

    *previous = NULL;

    while (entry != NULL) {
        if (entry->device_request_id == device_request_id) return entry;
        *previous = entry;
        entry = entry->next;
    }

    return NULL;
}

static void ipc_process_callbacks_append(
    IpcProcessCallbacks *callbacks,
    IpcProcessCallbackEntry *entry
) {
    entry->next = NULL;

    if (callbacks->head == NULL) {
        callbacks->tail = entry;
    } else {
        callbacks->head->next = entry;
    }

    callbacks->head = entry;
}

static void ipc_process_callbacks_unlink(
    IpcProcessCallbacks *callbacks,
    IpcProcessCallbackEntry *previous,
    IpcProcessCallbackEntry *entry
) {
    if (previous == NULL) {
        callbacks->tail = entry->next;
    } else {
        previous->next = entry->next;
    }

    if (callbacks->head == entry) callbacks->head = previous;
}

static void ipc_process_callbacks_destroy(IpcProcessCallbackEntry *entry) {
    free(entry);
}

static void ipc_process_callbacks_expire(IpcProcessCallbacks *callbacks, uint64_t now) {
    /*
     * tail is the oldest entry. Insertions and TTL refreshes move entries to
     * head, so expired entries form a removable prefix starting at tail.
     */
    while (callbacks->tail != NULL && callbacks->tail->expires_at <= now) {
        IpcProcessCallbackEntry *expired = callbacks->tail;

        callbacks->tail = expired->next;

        if (callbacks->head == expired) callbacks->head = NULL;

        ipc_process_callbacks_destroy(expired);
    }
}

bool ipc_process_callbacks_put(
    IpcProcessCallbacks *callbacks,
    uint64_t now,
    uint64_t ttl,
    uint32_t device_request_id,
    IpcProcessCallback callback
) {
    IpcProcessCallbackEntry *previous;
    IpcProcessCallbackEntry *entry;

    entry = ipc_process_callbacks_find(callbacks, device_request_id, &previous);

    if (entry != NULL) {
        entry->callback = callback;
        entry->expires_at = now + ttl;
        entry->ttl = ttl;

        if (callbacks->head != entry) {
            ipc_process_callbacks_unlink(callbacks, previous, entry);
            ipc_process_callbacks_append(callbacks, entry);
        }

        /* A successful head update lazily removes the expired tail prefix. */
        ipc_process_callbacks_expire(callbacks, now);
        return true;
    }

    entry = malloc(sizeof(*entry));

    if (entry == NULL) return false;

    entry->device_request_id = device_request_id;
    entry->expires_at = now + ttl;
    entry->ttl = ttl;
    entry->callback = callback;

    /* head is the only insertion point; tail remains the expiration point. */
    ipc_process_callbacks_append(callbacks, entry);

    /*
     * After the new head is visible, scan from the oldest entry and stop at
     * the first live deadline. Everything before it is already expired.
     */
    ipc_process_callbacks_expire(callbacks, now);
    return true;
}

IpcProcessCallback ipc_process_callbacks_get(
    IpcProcessCallbacks *callbacks,
    uint64_t now,
    uint32_t device_request_id
) {
    IpcProcessCallbackEntry *previous;
    IpcProcessCallbackEntry *entry;

    ipc_process_callbacks_expire(callbacks, now);
    entry = ipc_process_callbacks_find(callbacks, device_request_id, &previous);

    if (entry == NULL) return NULL;

    /* A live callback receives a fresh deadline and becomes the new head. */
    entry->expires_at = now + entry->ttl;

    if (callbacks->head != entry) {
        ipc_process_callbacks_unlink(callbacks, previous, entry);
        ipc_process_callbacks_append(callbacks, entry);
    }

    return entry->callback;
}

void ipc_process_callbacks_remove(IpcProcessCallbacks *callbacks, uint32_t device_request_id) {
    IpcProcessCallbackEntry *previous;
    IpcProcessCallbackEntry *entry = ipc_process_callbacks_find(callbacks, device_request_id, &previous);

    if (entry == NULL) return;

    ipc_process_callbacks_unlink(callbacks, previous, entry);
    ipc_process_callbacks_destroy(entry);
}

void ipc_process_callbacks_clear(IpcProcessCallbacks *callbacks) {
    IpcProcessCallbackEntry *entry = callbacks->tail;

    while (entry != NULL) {
        IpcProcessCallbackEntry *next = entry->next;

        ipc_process_callbacks_destroy(entry);
        entry = next;
    }

    memset(callbacks, 0, sizeof(*callbacks));
}
