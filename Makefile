OPTFLAGS:=-O3 -Os
CFLAGS:=-ansi -pedantic -Wall -Wextra -D_GNU_SOURCE $(OPTFLAGS)

server: main.o file.o http.o conf.o json.o
	$(CC) $(OPTFLAGS) $^ -o $@
	strip --discard-all $@

main.o: main.c http.h file.h conf.h
file.o: file.c file.h http.h
http.o: http.c http.h
conf.o: conf.c conf.h json.h
json.o: json.c json.h

.PHONY: clean
clean:
	$(RM) server *.o

