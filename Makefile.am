ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -D_GNU_SOURCE -I$(top_srcdir)/include
AM_CPPFLAGS += -std=c99 -pedantic -Wall -Wextra
AM_CPPFLAGS += -O3 -Os


server_SOURCES = http/main.c http/file.c http/http.c http/conf.c \
	common/json.c common/sock.c http/rest.c common/str.c common/log.c \
	http/user.c common/ini.c
server_SOURCES += http/conf.h http/file.h http/http.h http/rest.h http/user.h
server_CPPFLAGS = $(AM_CPPFLAGS) $(ZLIB_CFLAGS)
server_LDADD = $(ZLIB_LIBS)


rdb_SOURCES = db/rdb.c db/session.c db/cl_session.c common/sock.c common/log.c
rdb_SOURCES += db/cl_session.h db/session.h
rdb_CPPFLAGS = $(AM_CPPFLAGS) $(SQLITE3_CFLAGS)
rdb_LDADD = $(SQLITE3_LIBS)


GLOBAL_HDR = \
	include/ini.h include/json.h include/log.h include/rdb.h \
	include/sock.h include/str.h


EXTRA_DIST = data templates $(GLOBAL_HDR) README LICENSE


bin_PROGRAMS = server rdb


.PHONY: strip
strip: server$(EXEEXT) rdb$(EXEEXT)
	strip -s --discard-all --strip-unneeded $^
	strip -R .note.gnu.build-id -R .note -R .comment $^
	strip -R .eh_frame -R .eh_frame_hdr -R .jcr $^

.PHONY: cert
cert: stunnel.pem

stunnel.pem:
	openssl req -new -x509 -days 365 -nodes -out $@ -keyout $@
	openssl gendh 2048 >> $@
