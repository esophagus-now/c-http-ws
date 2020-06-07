main:	main.c implement.c http_parse.h mm_err.h websock.h
	gcc -Wall -Wno-cpp -fsanitize=address -fno-omit-frame-pointer -fno-diagnostics-show-caret -g -o main main.c implement.c -lcrypto

clean: 
	rm -rf main
