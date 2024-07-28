#ifndef QUEUE_H_
#define QUEUE_H_
#include <stdlib.h>
#include <stdbool.h>
typedef struct {
    void *data;
    size_t elemSize, head, tail,size,capacity;
    bool isFull, isEmpty;
} Queue;

Queue* queue_init(size_t capacity, size_t elemSize);
void queue_insert(Queue* q,void* item);
void queue_peek_head(Queue* q, void* out);
void queue_pop(Queue* q, void* out);
void queue_destroy(Queue* q);
#endif // QUEUE_H_
