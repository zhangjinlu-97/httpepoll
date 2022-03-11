all: httpepoll

blockingqueue: blockingqueue.c
	gcc -o blockingqueue blockingqueue.c -lpthread

threadpool: threadpool.c
	gcc -o threadpool threadpool.c -lpthread

httpepoll: httpepoll.c
	gcc -o httpepoll httpepoll.c threadpool.c blockingqueue.c -lpthread

clean:
	rm httpepoll
