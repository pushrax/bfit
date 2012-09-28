
compile:
	gcc -Wall -m32 -std=gnu99 -g -o bfit bfit.c cli.c

run: compile
	./bfit
