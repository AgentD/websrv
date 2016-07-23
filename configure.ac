AC_PREREQ([2.60])

AC_INIT([websrv], 0.1, [david.oberhollenzer@tele2.at], websrv)
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects dist-bzip2])
AM_SILENT_RULES([yes])
AC_PROG_LIBTOOL
AC_DISABLE_STATIC
AC_PROG_CC
AC_PROG_INSTALL


AC_ARG_ENABLE([json-parser],
	[AS_HELP_STRING([--disable-json-parser], [Compile without json parser])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([JSON_PARSER], [true]) ;;
	no) AM_CONDITIONAL([JSON_PARSER], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --disable-json-parser]) ;;
	esac],
	[AM_CONDITIONAL([JSON_PARSER], [true])])

AM_COND_IF([JSON_PARSER],
	[AC_DEFINE([JSON_PARSER], [1], ["Compile with JSON parser"])])



AC_ARG_ENABLE([json-serializer],
	[AS_HELP_STRING([--disable-json-serializer],
		[Compile without json serializer])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([JSON_SERIALIZER], [true]) ;;
	no) AM_CONDITIONAL([JSON_SERIALIZER], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --disable-json-serializer]) ;;
	esac],
	[AM_CONDITIONAL([JSON_SERIALIZER], [true])])

AM_COND_IF([JSON_SERIALIZER],
	[AC_DEFINE([JSON_SERIALIZER], [1], ["Compile with JSON serializer"])])



AC_ARG_ENABLE([session],
	[AS_HELP_STRING([--disable-session],
		[Compile without session management support])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([HAVE_SESSION], [true]) ;;
	no) AM_CONDITIONAL([HAVE_SESSION], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --disable-session]) ;;
	esac],
	[AM_CONDITIONAL([HAVE_SESSION], [true])])

AM_COND_IF([HAVE_SESSION],
	[AC_DEFINE([HAVE_SESSION], [1],
		["Compile with session management code"])])



AC_ARG_ENABLE([rest],
	[AS_HELP_STRING([--disable-rest], [Compile without REST backend])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([HAVE_REST], [true]) ;;
	no) AM_CONDITIONAL([HAVE_REST], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --disable-rest]) ;;
	esac],
	[AM_CONDITIONAL([HAVE_REST], [true])])

AM_COND_IF([HAVE_REST],
	[AC_DEFINE([HAVE_REST], [1], ["Compile with REST backend"])])



AC_ARG_ENABLE([static],
	[AS_HELP_STRING([--disable-static],
		[Compile without static file backend])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([HAVE_STATIC], [true]) ;;
	no) AM_CONDITIONAL([HAVE_STATIC], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --disable-static]) ;;
	esac],
	[AM_CONDITIONAL([HAVE_STATIC], [true])])

AM_COND_IF([HAVE_STATIC],
	[AC_DEFINE([HAVE_STATIC], [1], ["Compile with static file backend"])])



keepalive_timeout=2000
AC_ARG_WITH([client-timeout],
	[AS_HELP_STRING([--with-client-timeout=<timeout-ms>],
		[Timeout in milli seconds for receiving a HTTP request])],
	[keepalive_timeout=${withval}])

AC_DEFINE_UNQUOTED([KEEPALIVE_TIMEOUT_MS], [$keepalive_timeout],
	[Timeout for HTTP keepalive])



request_secs=5
AC_ARG_WITH([request-timeout],
	[AS_HELP_STRING([--with-request-timeout=<timeout-secs>],
		[Timeout in seconds for handling a HTTP request])],
	[request_secs=${withval}])

AC_DEFINE_UNQUOTED([MAX_REQUEST_SECONDS], [$request_secs],
	[Timeout for handling a request])



file_timeout=7200
AC_ARG_WITH([download-timeout],
	[AS_HELP_STRING([--with-download-timeout=<timeout-ms>],
		[Maximum number of milli seconds a file transfer may take])],
	[file_timeout=${withval}])

AC_DEFINE_UNQUOTED([MAX_FILEXFER_TIMEOUT], [$file_timeout],
	[Maximum time a file transfer can take])



con_requests=1000
AC_ARG_WITH([requests],
	[AS_HELP_STRING([--with-requests=<count>],
		[Maximum number of requests per connection for HTTP keep-alive])],
	[con_requests=${withval}])

AC_DEFINE_UNQUOTED([MAX_REQUESTS], [$con_requests],
	[Maximum requests per connection])



PKG_CHECK_MODULES(ZLIB, [zlib])
PKG_CHECK_MODULES(SQLITE3, [sqlite3])

AC_CONFIG_HEADERS([include/config.h:include/config.h.in])
AC_OUTPUT([Makefile])
