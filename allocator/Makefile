alloc: alloc.c debug.c unity.c
	gcc alloc.c debug.c unity.c -g -o alloc

memcheck: alloc
	valgrind --leak-check=full --track-origins=yes ./alloc

clean:
	rm -f alloc



