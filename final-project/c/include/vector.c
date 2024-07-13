#include "vector.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

Vector* vector_init(size_t initialCapacity) {
    if(initialCapacity < 1) return NULL; //return -1 if invalid
    Vector* vec = (Vector*) malloc(sizeof(Vector));
    vec->data = (trade*) malloc(initialCapacity*sizeof(trade));
    vec->size = 0;
    vec->capacity = initialCapacity;
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
    vec->data = NULL;
    pthread_mutex_destroy(vec->mutex);
    pthread_cond_destroy(vec->isEmpty);
    free(vec->mutex);
    free(vec->isEmpty);
    free(vec);
    vec = NULL;
}

int vector_push_back(Vector *vec, trade* value) {
    vec->size++;
    if(vec->size >= vec->capacity){
        //reallocate memory
        vec->capacity = 2*vec->size;
        printf("normal reallocation\n");
        trade* newarray = realloc(vec->data, vec->capacity*sizeof(trade));
        if(newarray == NULL) return -1; //memory allocation failed
        else{
            vec->data = newarray;
        }
    }
    memcpy(vec->data+vec->size-1,value, sizeof(trade));
    return 0;
}

int vector_pop(Vector* vec, trade* out) {
    if(vec->size > 0){
        vec->size--;
        memcpy(out, vec->data,sizeof(trade));
        return 0;
    }
    else return -1;
}
