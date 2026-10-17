vmem: smoketest.c vm.c vm.h
	gcc -g smoketest.c vm.c -o vmem

memcheck: vmem
	valgrind --leak-check=full --track-origins=yes ./vmem

clean:
	rm -f vmem

	