all: program1 program2
program1: server.c
	gcc -o server server.c -lpthread
program2: client.c
	gcc -o client client.c -lpthread
