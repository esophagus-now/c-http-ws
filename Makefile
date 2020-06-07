main:	main.c	implement.c http_parse.h	mm_err.h
	gcc -Wall -fno-diagnostics-show-caret -g -o main main.c implement.c

clean: 
	rm -rf main
