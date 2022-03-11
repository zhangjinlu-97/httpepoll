#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

static ThreadPool *thread_pool;

static void *thread_main(void *argv) {
//    printf("thread %d is running\n", (int) argv);
    for (;;) {
        Item item = Take(thread_pool->wait_queue);
        item.fn(item.args);
    }
}

ThreadPool *CreateThreadPool(int size) {
    ThreadPool *tp = (ThreadPool *) malloc(sizeof(ThreadPool));
    tp->threads = (pthread_t *) malloc(size * sizeof(pthread_t));
    tp->wait_queue = NewBlockingQueue(size * 2);
    tp->size = size;
    thread_pool = tp;
    return tp;
}

void AddTask(ThreadPool *tp, void *(*fn)(void *), void *args) {
    Item item = NewItem(fn, args);
    Put(tp->wait_queue, item);
}

void Run(ThreadPool *tp) {
    for (int i = 0; i < tp->size; ++i) {
        pthread_create(&tp->threads[i], NULL, thread_main, (void *) i);
    }
}

void Exit(ThreadPool *tp) {
    for (int i = 0; i < tp->size; ++i) {
        pthread_cancel(tp->threads[i]);  // 取消所有线程
    }
    free(tp->threads);
    Destroy(tp->wait_queue);
    free(tp);
}
