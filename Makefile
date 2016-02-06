OPTFLAGS:=-O3 -Os
CFLAGS:=-ansi -pedantic -Wall -Wextra -D_GNU_SOURCE $(OPTFLAGS)

.PHONY: all
all: rdb server

server: main.o file.o http.o conf.o json.o sock.o rest.o html.o
	$(CC) $(OPTFLAGS) $^ -o $@

rdb: rdb.o sock.o
	$(CC) $(OPTFLAGS) $^ -lsqlite3 -o $@

rdb.o: rdb.c rdb.h
main.o: main.c http.h file.h conf.h sock.h rest.h
file.o: file.c file.h http.h sock.h
http.o: http.c http.h
conf.o: conf.c conf.h json.h
json.o: json.c json.h
sock.o: sock.c sock.h
rest.o: rest.c rest.h http.h html.h sock.h rdb.h
html.o: html.c html.h http.h

stunnel.pem:
	openssl req -new -x509 -days 365 -nodes -out $@ -keyout $@
	openssl gendh 2048 >> $@

.PHONY: strip
strip: server rdb
	strip -s --discard-all --strip-unneeded $^
	strip -R .note.gnu.build-id $^
	strip -R .note -R .comment $^
	strip -R .eh_frame server -R .eh_frame_hdr -R .jcr $^

.PHONY: clean
clean:
	$(RM) server rdb *.o

.PHONY: cert
cert: stunnel.pem

