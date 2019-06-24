all:
			 gcc -c parser.c color.c process_control.c
	   	 gcc shell.c -o shell parser.o color.o process_control.o
		gcc -Wall test_pipe.c -o test_pipe
		./shell


debug:
			 gcc -c parser.c color.c process_control.c
	     gcc shell.c -o shell parser.o color.o process_control.o -DDEBUG

clean:
			 rm -rf *.o shell
