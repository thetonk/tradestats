#ifndef VECTOR_H_
#define VECTOR_H_
#include "constants.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
	char symbol[SYMBOL_LENGTH];
	time_t timestamp;
	double price, volume;
} trade;

typedef struct {
    trade* data;
    size_t size;
    size_t capacity;
    pthread_cond_t* isEmpty;
    pthread_mutex_t* mutex;
} Vector;

Vector* vector_init(size_t initialCapacity);
void vector_destroy(Vector *vec);
bool vector_push_back(Vector *vec, trade* value);
bool vector_pop(Vector* vec, trade* out);
bool vector_peek_front(Vector *vec, trade* out);
bool vector_peek_back(Vector* vec, trade*out);
#endif // VECTOR_H_
