all: mylib.so server

server: server.c
	gcc  -I../include -L../lib -g -pthread server.c -o ./server

mylib.o: mylib.c
	gcc -I../include -L../lib -Wall -fPIC -DPIC -c mylib.c

mylib.so: mylib.o
	ld -shared -o mylib.so mylib.o -ldl

clean:
	rm -f *.o *.so



