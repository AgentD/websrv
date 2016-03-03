OPTFLAGS:=-O3 -Os
CFLAGS:=-ansi -pedantic -Wall -Wextra -D_GNU_SOURCE $(OPTFLAGS)

.PHONY: all
all: rdb server

server: main.o file.o http.o conf.o json.o sock.o rest.o str.o error.o
	$(CC) $(OPTFLAGS) $^ -lz -o $@

rdb: rdb.o sock.o
	$(CC) $(OPTFLAGS) $^ -lsqlite3 -o $@

str.o: str.c str.h
rdb.o: rdb.c rdb.h
main.o: main.c http.h file.h conf.h sock.h rest.h error.h
file.o: file.c file.h http.h sock.h error.h
http.o: http.c http.h str.h
conf.o: conf.c conf.h
json.o: json.c json.h str.h
sock.o: sock.c sock.h
rest.o: rest.c rest.h http.h sock.h rdb.h str.h json.h error.h conf.h
error.o: error.c error.h http.h str.h

stunnel.pem:
	openssl req -new -x509 -days 365 -nodes -out $@ -keyout $@
	openssl gendh 2048 >> $@

.PHONY: strip
strip: server rdb
	strip -s --discard-all --strip-unneeded $^
	strip -R .note.gnu.build-id $^
	strip -R .note -R .comment $^
	strip -R .eh_frame -R .eh_frame_hdr -R .jcr $^

.PHONY: clean
clean:
	$(RM) server rdb *.o

.PHONY: cert
cert: stunnel.pem

