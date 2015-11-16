OPTFLAGS:=-O3 -Os
CFLAGS:=-ansi -pedantic -Wall -Wextra -D_GNU_SOURCE $(OPTFLAGS)

server: main.o file.o http.o conf.o json.o sock.o rest.o
	$(CC) $(OPTFLAGS) $^ -o $@

main.o: main.c http.h file.h conf.h sock.h rest.h
file.o: file.c file.h http.h
http.o: http.c http.h
conf.o: conf.c conf.h json.h
json.o: json.c json.h
sock.o: sock.c sock.h
rest.o: rest.c rest.h http.h

.PHONY: strip
strip: server
	strip -s --discard-all --strip-unneeded server
	strip -R .note.gnu.build-id server
	strip -R .note -R .comment server
	strip -R .eh_frame server -R .eh_frame_hdr -R .jcr  server

.PHONY: clean
clean:
	$(RM) server *.o

