/* port.h - portability defines */

#if defined(__FreeBSD__)
# define OS_FreeBSD
# define ARCH "FreeBSD"
#elif defined(__OpenBSD__)
# define OS_OpenBSD
# define ARCH "OpenBSD"
#elif defined(__NetBSD__)
# define OS_NetBSD
# define ARCH "NetBSD"
#elif defined(linux)
# define OS_Linux
# define ARCH "Linux"
#elif defined(sun)
# define OS_Solaris
# define ARCH "Solaris"
#elif defined(__osf__)
# define OS_DigitalUnix
# define ARCH "DigitalUnix"
#elif defined(__svr4__)
# define OS_SysV
# define ARCH "SysV"
#else
# define OS_UNKNOWN
# define ARCH "UNKNOWN"
#endif

#ifdef OS_FreeBSD
# include <osreldate.h>
# define HAVE_DAEMON
# define HAVE_SETSID
# define HAVE_SETLOGIN
# define HAVE_WAITPID
# define HAVE_HSTRERROR
# define HAVE_TM_GMTOFF
# define HAVE_SENDFILE
# define HAVE_SCANDIR
# define HAVE_INT64T
# define HAVE_SRANDOMDEV
# ifdef SO_ACCEPTFILTER
#  define HAVE_ACCEPT_FILTERS
#  if ( __FreeBSD_version >= 411000 )
#   define ACCEPT_FILTER_NAME "httpready"
#  else
#   define ACCEPT_FILTER_NAME "dataready"
#  endif
# endif /* SO_ACCEPTFILTER */
#endif /* OS_FreeBSD */

#ifdef OS_OpenBSD
# define HAVE_DAEMON
# define HAVE_SETSID
# define HAVE_SETLOGIN
# define HAVE_WAITPID
# define HAVE_HSTRERROR
# define HAVE_TM_GMTOFF
# define HAVE_SCANDIR
# define HAVE_INT64T
#endif /* OS_OpenBSD */

#ifdef OS_NetBSD
# define HAVE_DAEMON
# define HAVE_SETSID
# define HAVE_SETLOGIN
# define HAVE_WAITPID
# define HAVE_HSTRERROR
# define HAVE_TM_GMTOFF
# define HAVE_SCANDIR
# define HAVE_INT64T
#endif /* OS_NetBSD */

#ifdef OS_Linux
# define HAVE_DAEMON
# define HAVE_SETSID
# define HAVE_WAITPID
# define HAVE_TM_GMTOFF
# define HAVE_SENDFILE
# define HAVE_LINUX_SENDFILE
# define HAVE_SCANDIR
# define HAVE_INT64T
#endif /* OS_Linux */

#ifdef OS_Solaris
# define HAVE_SETSID
# define HAVE_WAITPID
# define HAVE_MEMORY_H
# define HAVE_SIGSET
# define HAVE_INT64T
#endif /* OS_Solaris */

#ifdef OS_DigitalUnix
# define HAVE_SETSID
# define HAVE_SETLOGIN
# define HAVE_WAITPID
# define HAVE_SCANDIR
# define HAVE_TM_GMTOFF
# define NO_SNPRINTF
/* # define HAVE_INT64T */	/* Digital Unix 4.0d doesn't have int64_t */
#endif /* OS_DigitalUnix */

#ifdef OS_SysV
# define HAVE_SETSID
# define HAVE_WAITPID
# define HAVE_MEMORY_H
# define HAVE_SIGSET
#endif /* OS_Solaris */
