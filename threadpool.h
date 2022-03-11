#include <pthread.h>
#include "blockingqueue.h"

typedef struct {
    pthread_t *threads;
    BlockingQueue *wait_queue;
    int size;
} ThreadPool;

ThreadPool *CreateThreadPool(int size);

void AddTask(ThreadPool *tp, void *(*fn)(void *), void *args);

void Run(ThreadPool *tp);

void Exit(ThreadPool *tp);
