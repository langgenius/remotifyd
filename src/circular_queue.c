#include "circular_queue.h"

#include <stdlib.h>

static bool circular_queue_grow(CircularQueue *queue) {
    size_t capacity = queue->capacity == 0 ? 8 : queue->capacity * 2;
    void **slots = malloc(capacity * sizeof(*slots));
    size_t index;

    if (slots == NULL) return false;

    /* Put the wrapped queue back into ordinary FIFO order while growing. */
    for (index = 0; index < queue->length; index++) {
        slots[index] = queue->slots[(queue->head + index) % queue->capacity];
    }

    free(queue->slots);
    queue->slots = slots;
    queue->capacity = capacity;
    queue->head = 0;
    return true;
}

bool circular_queue_put(CircularQueue *queue, void *value) {
    size_t tail;

    if (queue->length == queue->capacity && !circular_queue_grow(queue)) {
        return false;
    }

    tail = (queue->head + queue->length) % queue->capacity;
    queue->slots[tail] = value;
    queue->length++;
    return true;
}

void *circular_queue_get(const CircularQueue *queue) {
    if (queue->length == 0) return NULL;
    return queue->slots[queue->head];
}

void *circular_queue_consume(CircularQueue *queue) {
    void *value;

    if (queue->length == 0) return NULL;

    value = queue->slots[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->length--;
    return value;
}
