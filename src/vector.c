#include "vector.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

Vector* vector_init(size_t initialCapacity, size_t elemSize) {
    if(initialCapacity < 1) return NULL;
    Vector* vec = (Vector*) malloc(sizeof(Vector));
    vec->data = (void*) malloc(initialCapacity*elemSize);
    vec->size = 0;
    vec->capacity = initialCapacity;
    vec->elemSize = elemSize;
    vec->mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(vec->mutex,NULL);
    vec->isEmpty = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(vec->isEmpty, NULL);
    return vec;
}

void vector_destroy(Vector* vec) {
    free(vec->data);
    vec->size = 0;
    vec->capacity = 0;
    vec->elemSize = 0;
    vec->data = NULL;
    pthread_mutex_destroy(vec->mutex);
    pthread_cond_destroy(vec->isEmpty);
    free(vec->mutex);
    free(vec->isEmpty);
    free(vec);
    vec = NULL;
}

void vector_clear(Vector *vec){
    vec->size = 0;
    memset(vec->data,0,vec->capacity*sizeof(vec->elemSize));
}

bool vector_push_back(Vector *vec, void* value) {
    vec->size++;
    if(vec->size >= vec->capacity){
        //reallocate memory
        vec->capacity = 2*vec->size;
        //printf("normal reallocation\n");
        void* newarray = realloc(vec->data, vec->capacity*vec->elemSize);
        if(newarray == NULL) return false; //memory allocation failed
        else{
            vec->data = newarray;
        }
    }
    memcpy((char*) vec->data+(vec->size-1)*vec->elemSize,value, vec->elemSize);
    return true;
}

bool vector_pop(Vector* vec, void* out) {
    if(vec->size > 0){
        memcpy(out,(char*) vec->data,vec->elemSize);
        memmove((char*) vec->data, (char *) vec->data + vec->elemSize, (vec->size-1)*vec->elemSize);
        vec->size--;
        return true;
    }
    else return false;
}

bool vector_peek_front(Vector *vec, void *out){
    if(vec->size > 0){
        memcpy(out,(char*) vec->data, vec->elemSize);
        return true;
    }
    else return false;
}

bool vector_peek_back(Vector *vec, void *out){
    if(vec->size > 0){
        memcpy(out,(char*) vec->data + (vec->size -1)*vec->elemSize, vec->elemSize);
        return true;
    }
    else return false;
}
