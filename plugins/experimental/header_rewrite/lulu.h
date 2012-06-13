//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the classes for the various types of hash keys we support.
//
#ifndef __LULU_H__
#define __LULU_H__ 1

// Define UNUSED properly.
#if ((__GNUC__ >= 3) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 7)))
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif /* #if ((__GNUC__ >= 3) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 7))) */

static char UNUSED rcsId__lulu_h[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <sys/types.h>

// Memory barriers on i386 / linux / gcc
#if defined(__i386__)
#define mb()  __asm__ __volatile__ ( "lock; addl $0,0(%%esp)" : : : "memory" )
#define rmb() __asm__ __volatile__ ( "lock; addl $0,0(%%esp)" : : : "memory" )
#define wmb() __asm__ __volatile__ ( "" : : : "memory")
#elif defined(__x86_64__)
#define mb()  __asm__ __volatile__ ( "mfence" : : : "memory")
#define rmb() __asm__ __volatile__ ( "lfence" : : : "memory")
#define wmb() __asm__ __volatile__ ( "" : : : "memory")
#else
#error "Define barriers"
#endif

static const char* PLUGIN_NAME UNUSED = "header_rewrite";
static const char* PLUGIN_NAME_DBG UNUSED = "header_rewrite_dbg";


// From google styleguide: http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml
#define DISALLOW_COPY_AND_ASSIGN(TypeName)      \
  TypeName(const TypeName&);                    \
  void operator=(const TypeName&)


#endif // __LULU_H__
