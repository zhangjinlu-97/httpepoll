#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    void *(*fn)(void *);  // 需要执行的函数指针
    void *args;  // 参数
} Item;

Item NewItem(void *(*fn)(void *), void *args);

typedef struct {
    Item *data;
    int front;
    int rear;
    int cap;
} BlockingQueue;

BlockingQueue *NewBlockingQueue(int cap);

void Put(BlockingQueue *q, Item item);

Item Take(BlockingQueue *q);

void Destroy(BlockingQueue *q);


