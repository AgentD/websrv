#include <linux/random.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include "session.h"
#include "config.h"
#include "log.h"

#ifdef HAVE_SESSION
static int lockfd = -1;
static void* buffer = NULL;
static size_t bufsize = 0;

#ifndef HAVE_GETRANDOM
int getrandom(void *buf, size_t buflen, unsigned int flags)
{
    return syscall(SYS_getrandom, buf, buflen, flags);
}
#endif

int sesion_init( void )
{
    bufsize = 4096;
    buffer = mmap(NULL, bufsize, PROT_READ|PROT_WRITE,
                  MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if( !buffer )
        goto fail;

    lockfd = eventfd(1, EFD_CLOEXEC|EFD_SEMAPHORE);
    if( lockfd < 0 )
        goto fail;

    return 1;
fail:
    session_cleanup( );
    return 0;
}

void session_cleanup( void )
{
    if( buffer && bufsize )
        munmap(buffer, bufsize);
    if( lockfd >= 0 )
        close(lockfd);
    bufsize = 0;
    buffer = NULL;
    lockfd = -1;
}

void session_lock( void )
{
    uint64_t val;
    read(lockfd, &val, 8);
}

void session_unlock( void )
{
    uint64_t val = 1;
    write(lockfd, &val, 8);
}

size_t sessions_get_count( void )
{
    return *((size_t*)buffer);
}

struct session* sessions_get( size_t index )
{
    size_t offset = sizeof(size_t) + index * sizeof(struct session);

    if( offset >= bufsize )
        return NULL;

    return (struct session*)((uint8_t*)buffer + offset);
}

struct session* sessions_get_by_id( uint32_t id )
{
    struct session* s = (struct session*)((size_t*)buffer + 1);
    size_t i, count = *((size_t*)buffer);

    for( i=0; i<count; ++i, ++s )
    {
        if( s->sid == id )
            return s;
    }
    return NULL;
}

void sessions_check_expire( void )
{
    struct session* s = (struct session*)((size_t*)buffer + 1);
    size_t i, count = *((size_t*)buffer);
    time_t current = time(NULL);

    for( i=0; i<count; ++i )
    {
        if( (current - s[i].atime) > SESSION_EXPIRE )
        {
            INFO("session (SID=%u, UID=%u) expired",
                (unsigned int)s[i].sid, (unsigned int)s[i].uid);
            s[i] = s[count - 1];
            --i;
            --count;
        }
    }

    *((size_t*)buffer) = count;
}

struct session* session_create( void )
{
    size_t tries = 0, count = *((size_t*)buffer);
    struct session* s = sessions_get( count );

    if( !s )
        return NULL;

    memset( s, 0, sizeof(*s) );

    while( 1 )
    {
        getrandom(&(s->sid), sizeof(s->sid), 0);
        if( s->sid!=0 && sessions_get_by_id(s->sid)==NULL )
            break;
        if( tries++ > 10 )      /* should be very unlikely */
        {
            CRITICAL("failed to generarte unique session ID (last try=%u)",
                    (unsigned int)s->sid);
            return NULL;
        }
    }

    s->atime = time(NULL);
    *((size_t*)buffer) += 1;
    return s;
}

void session_remove_by_id( uint32_t id )
{
    struct session* s = (struct session*)((size_t*)buffer + 1);
    size_t i, count = *((size_t*)buffer);

    for( i=0; i<count; ++i )
    {
        if( s[i].sid == id )
        {
            s[i] = s[count - 1];
            --i;
            --count;
        }
    }

    *((size_t*)buffer) = count;
}
#endif

