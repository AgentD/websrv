include Config

OPTFLAGS:=-O3 -Os
INCFLAGS:=-Iinclude
CFLAGS:=-std=c99 -pedantic -Wall -Wextra -D_GNU_SOURCE $(OPTFLAGS) $(INCFLAGS)

.PHONY: all
all: rdb server

server: http/main.o http/file.o http/http.o http/conf.o common/json.o \
	common/sock.o http/rest.o common/str.o common/log.o \
	http/user.o common/ini.o
	$(CC) $(OPTFLAGS) $^ -lz -o $@

rdb: db/rdb.o db/session.o db/cl_session.o common/sock.o common/log.o
	$(CC) $(OPTFLAGS) $^ -lsqlite3 -o $@

common/ini.o: common/ini.c include/ini.h include/log.h
common/str.o: common/str.c include/str.h
common/log.o: common/log.c include/log.h
common/json.o: common/json.c include/json.h include/str.h
common/sock.o: common/sock.c include/sock.h include/log.h

db/rdb.o: db/rdb.c include/rdb.h include/log.h db/session.h db/cl_session.h \
		include/sock.h
db/session.o: db/session.c db/session.h include/log.h
db/cl_session.o: db/cl_session.c db/cl_session.h db/session.h include/log.h \
			include/rdb.h

http/main.o: http/main.c http/http.h http/file.h http/conf.h \
			include/sock.h http/rest.h include/log.h include/str.h
http/file.o: http/file.c http/file.h http/http.h include/sock.h include/str.h
http/http.o: http/http.c http/http.h include/str.h
http/conf.o: http/conf.c http/conf.h include/ini.h include/log.h
http/rest.o: http/rest.c http/rest.h http/http.h include/sock.h \
			include/rdb.h include/str.h include/json.h \
			http/conf.h http/user.h
http/user.o: http/user.c http/user.h http/http.h include/rdb.h include/str.h

stunnel.pem:
	openssl req -new -x509 -days 365 -nodes -out $@ -keyout $@
	openssl gendh 2048 >> $@

.PHONY: strip
strip: server rdb
	strip -s --discard-all --strip-unneeded $^
	strip -R .note.gnu.build-id -R .note -R .comment $^
	strip -R .eh_frame -R .eh_frame_hdr -R .jcr $^

Config:
	sh configure

.PHONY: clean
clean:
	$(RM) server rdb *.o db/*.o http/*.o common/*.o

.PHONY: cert
cert: stunnel.pem

