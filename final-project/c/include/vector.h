#ifndef VECTOR_H_
#define VECTOR_H_
#include "constants.h"
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct {
	char symbol[SYMBOL_LENGTH];
	uint64_t timestamp;
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
int vector_push_back(Vector *vec, trade* value);
int vector_pop(Vector* vec, trade* out);
#endif // VECTOR_H_
