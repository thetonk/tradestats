#ifndef PROD_CONS_H_
#define PROD_CONS_H_
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define QUEUESIZE 5
#define LOOP 10

void *producer (void *args);
void *consumer (void *args);

typedef struct {
  void* (*work)(void*);
  void *arg;

} workFunction;

typedef struct {
  workFunction buf[QUEUESIZE];
  struct timeval insertionTime[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
} queue;

typedef struct {
  size_t threadID;
  queue* queue;
} ProducerData;

typedef struct{
  double parameter;
  double* result;
} myparams;

queue *queueInit (void);
void queueDelete (queue *q);
void queueAdd (queue *q, workFunction in);
void queueDel (queue *q, workFunction *out);

#endif // PROD-CONS_H_
