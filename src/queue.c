#include "queue.h"
#include <stdbool.h>
#include <string.h>

Queue* queue_init(size_t capacity,size_t elemSize){
    Queue* queue = malloc(sizeof(Queue));
    queue->data = malloc(capacity*elemSize);
    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
    queue->elemSize = elemSize;
    queue->isEmpty = true;
    queue->isFull = false;
    queue->capacity = capacity;
    return queue;
}

void queue_destroy(Queue *q){
    free(q->data);
    q->data = NULL;
    free(q);
    q = NULL;
}

void queue_insert(Queue *q, void *item){
    if(!q->isFull){
        memcpy((char*) q->data+ q->tail*q->elemSize, item, q->elemSize);
        q->isEmpty = false;
        q->tail++;
        q->size++;
        if(q->tail == q->capacity){
            q->tail = 0;
        }
        if(q->size == q->capacity){
            q->isFull = true;
        }
    }
}

void queue_peek_head(Queue *q, void *out){
    if(!q->isEmpty){
        memcpy(out, (char*) q->data+ q->head*q->elemSize, q->elemSize);
    }
}

void queue_pop(Queue* q, void* out){
    if(!q->isEmpty){
        memcpy(out, (char*) q->data+ q->head*q->elemSize, q->elemSize);
        q->head++;
        q->size--;
        q->isFull = false;
        if(q->head == q->capacity){
            q->head = 0;
        }
        if(q->size == 0){
            q->isEmpty = true;
        }
    }
}
