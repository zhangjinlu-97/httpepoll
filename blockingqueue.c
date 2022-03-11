#include "blockingqueue.h"
#include <stdio.h>

static pthread_mutex_t bq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;

static void lock() {
    if (pthread_mutex_lock(&bq_mutex) != 0) {
        perror("pthread_mutex_lock");
        exit(1);
    }
}

static void unlock() {
    if (pthread_mutex_unlock(&bq_mutex) != 0) {
        perror("pthread_mutex_unlock");
        exit(1);
    }
}

static void wait(pthread_cond_t *cond_ptr) {
    if (pthread_cond_wait(cond_ptr, &bq_mutex) != 0) {
        perror("pthread_cond_wait");
        exit(1);
    }
}

static void notify(pthread_cond_t *cond_ptr) {
    if (pthread_cond_signal(cond_ptr) != 0) {
        perror("pthread_cond_signal");
        exit(1);
    }
}

static bool is_empty(BlockingQueue *q) {
    return (q->rear + 1) % q->cap == q->front;
}

static bool is_full(BlockingQueue *q) {
    return (q->rear + 2) % q->cap == q->front;
}

Item NewItem(void *(*fn)(void *), void *args) {
    Item item = {fn, args};
    return item;
}

BlockingQueue *NewBlockingQueue(int cap) {
    BlockingQueue *queue = (BlockingQueue *) malloc(sizeof(BlockingQueue));
    queue->data = (Item *) malloc((cap + 1) * sizeof(int));
    queue->front = 1;
    queue->rear = 0;
    queue->cap = cap + 1;
    return queue;
}

void Put(BlockingQueue *q, Item item) {
    lock();
    while (is_full(q))
        wait(&not_full);  // 当队列满时，阻塞等待
    q->rear = (q->rear + 1) % q->cap;
    q->data[q->rear] = item;
    notify(&not_empty);  // 唤醒等待的消费线程
    unlock();
}

Item Take(BlockingQueue *q) {
    Item item;
    lock();
    while (is_empty(q))
        wait(&not_empty);  // 当队列为空时，阻塞等待
    item = q->data[q->front];
    q->front = (q->front + 1) % q->cap;
    notify(&not_full);
    unlock();
    return item;
}

void Destroy(BlockingQueue *q) {
    free(q->data);
    free(q);
}