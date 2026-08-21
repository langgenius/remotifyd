#ifndef REMOTIFY_CIRCULAR_QUEUE_H
#define REMOTIFY_CIRCULAR_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * CircularQueue stores opaque pointers only. The module does not know what
 * they point to or how those objects are released.
 */
typedef struct CircularQueue {
    void **slots;
    size_t capacity;
    size_t head;
    size_t length;
} CircularQueue;

bool circular_queue_put(CircularQueue *queue, void *value);
void *circular_queue_get(const CircularQueue *queue);
void *circular_queue_consume(CircularQueue *queue);

#endif
