#ifndef LOG_H
#define LOG_H

#define LEVEL_DEBUG 3
#define LEVEL_INFO 2
#define LEVEL_WARNING 1
#define LEVEL_CRITICAL 0

#define CRITICAL( ... ) log_printf( LEVEL_CRITICAL, __VA_ARGS__ )
#define WARN( ... ) log_printf( LEVEL_WARNING, __VA_ARGS__ )
#define INFO( ... ) log_printf( LEVEL_INFO, __VA_ARGS__ )
#define DBG( ... ) log_printf( LEVEL_DEBUG, __VA_ARGS__ )

int log_init( const char* file, int level );

void log_printf( int level, const char* fmt, ... );

void print_stacktrace( void );

#endif /* LOG_H */

