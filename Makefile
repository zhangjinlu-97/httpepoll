all: httpepoll

httpepoll: httpepoll.c
	gcc -o httpepoll httpepoll.c

clean:
	rm httpepoll
