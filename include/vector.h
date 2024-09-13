#ifndef VECTOR_H_
#define VECTOR_H_
#include "constants.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    void* data;
    size_t size;
    size_t capacity;
    size_t elemSize;
    pthread_cond_t* isEmpty;
    pthread_mutex_t* mutex;
} Vector;

Vector* vector_init(size_t initialCapacity, size_t elemSize);
void vector_destroy(Vector *vec);
void vector_clear(Vector *vec);
bool vector_push_back(Vector *vec, void* value);
bool vector_pop(Vector* vec, void* out);
bool vector_peek_front(Vector *vec, void* out);
bool vector_peek_back(Vector* vec, void*out);
#endif // VECTOR_H_
