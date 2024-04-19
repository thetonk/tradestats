#include "prod-cons.h"

uint32_t itemCount, itemsConsumed = 0;
double *itemDetentionTime;

void* mysin(void *params){
    myparams *parameters = (myparams*) params;
    *parameters->result = sin(parameters->parameter + rand() % 10);
    return NULL;
}

int main (int argc, char *argv[])
{
  uint32_t p = 5, q = 5;
  if (argc >= 3){
    p = atoi(argv[1]);
    q = atoi(argv[2]);
  }
  else{
    printf("Usage: %s <producers number> <consumers number>\n", argv[0]);
    exit(1);
  }
  uint32_t i = 0, j=0;
  itemCount = LOOP*p;
  itemDetentionTime = (double*) malloc(itemCount*sizeof(double));
  queue *fifo;
  ProducerData producerData[p];
  pthread_t pro[p], con[q];
  fifo = queueInit ();
  if (fifo ==  NULL) {
    fprintf(stderr, "main: Queue Init failed.\n");
    exit(1);
  }
  for(i = 0; i < p; ++i){
    producerData[i].threadID = i;
    producerData[i].queue = fifo;
    pthread_create (&pro[i], NULL, producer, &producerData[i]);
  }
  for(j = 0; j < q; ++j){
    pthread_create (&con[j], NULL, consumer, fifo);
  }
  i = 0, j = 0;
  for(i = 0; i < p; ++i){
    pthread_join (pro[i], NULL);
  }
  for(j = 0; j < q; ++j){
    pthread_join (con[j], NULL);
  }
  queueDelete (fifo);
  FILE *fp = fopen("detentionTimes.txt", "w");
  if (fp == NULL) {
    printf("Error! output file cannot be created!\n");
    exit(2);
  }
  for(uint32_t i = 0; i < itemCount; ++i){
    fprintf(fp, "%f\n", itemDetentionTime[i]);
  }
  fclose(fp);
  free(itemDetentionTime);
  return 0;
}

void *producer (void *q)
{
  ProducerData* data = (ProducerData*) q;
  queue *fifo = data->queue;
  int i;
  for (i = 0; i < LOOP; i++) {
    pthread_mutex_lock (fifo->mut);
    while (fifo->full) {
      //printf ("producer: queue FULL.\n");
      pthread_cond_wait (fifo->notFull, fifo->mut);
    }
    myparams *parameters = (myparams*) malloc(sizeof(myparams));
    parameters->parameter = M_PI_4;
    parameters->result = (double*) malloc(sizeof(double));
    workFunction myfunction = {&mysin, parameters};
    queueAdd (fifo, myfunction);
    pthread_cond_signal (fifo->notEmpty);
    pthread_mutex_unlock (fifo->mut);
  }
  //printf("producer: done!\n");
  return (NULL);
}

void *consumer (void *q)
{
  queue *fifo;
  int i, d;
  workFunction task;
  myparams *parameters;
  fifo = (queue *)q;
  while(true) {
    pthread_mutex_lock (fifo->mut);
    while (itemsConsumed < itemCount && fifo->empty) {
      //printf ("consumer: queue EMPTY.\n");
      pthread_cond_wait (fifo->notEmpty, fifo->mut);
    }
    if(itemsConsumed == itemCount){
      //wake up the remaining threads
      pthread_cond_signal(fifo->notEmpty);
      pthread_mutex_unlock(fifo->mut);
      break;
    }
    if(!fifo->empty){
      queueDel (fifo, &task);
      parameters = (myparams*) task.arg;
      task.work(task.arg);
      itemsConsumed++;
      printf ("consumer: received %f.\n",*parameters->result);
      free(parameters->result);
      free(task.arg);
    }
    pthread_cond_signal (fifo->notFull);
    pthread_mutex_unlock (fifo->mut);
  }
  //printf("consumer: done!\n");
  return (NULL);
}

queue *queueInit (void)
{
  queue *q;

  q = (queue *)malloc (sizeof (queue));
  if (q == NULL) return (NULL);

  q->empty = 1;
  q->full = 0;
  q->head = 0;
  q->tail = 0;
  q->mut = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (q->mut, NULL);
  q->notFull = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notFull, NULL);
  q->notEmpty = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notEmpty, NULL);
  return (q);
}

void queueDelete (queue *q)
{
  pthread_mutex_destroy (q->mut);
  free (q->mut);
  pthread_cond_destroy (q->notFull);
  free (q->notFull);
  pthread_cond_destroy (q->notEmpty);
  free (q->notEmpty);
  free (q);
}

void queueAdd (queue *q, workFunction in)
{
  q->buf[q->tail] = in;
  gettimeofday(&q->insertionTime[q->tail] , NULL);
  q->tail++;
  if (q->tail == QUEUESIZE)
    q->tail = 0;
  if (q->tail == q->head)
    q->full = 1;
  q->empty = 0;

  return;
}

void queueDel (queue *q, workFunction *out)
{
  *out = q->buf[q->head];
  struct timeval currentTime;
  struct timeval insertionTime = q->insertionTime[q->head];
  gettimeofday(&currentTime, NULL);
  size_t secondsElapsed = currentTime.tv_sec - insertionTime.tv_sec;
  int32_t microsecondsElapsed = currentTime.tv_usec - insertionTime.tv_usec;
  itemDetentionTime[itemsConsumed] = secondsElapsed + 1e-6*microsecondsElapsed;
  q->head++;
  if (q->head == QUEUESIZE)
    q->head = 0;
  if (q->head == q->tail)
    q->empty = 1;
  q->full = 0;
  return;
}
