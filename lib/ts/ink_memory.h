/** @file

  Memory allocation routines for libts.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */
#ifndef _ink_memory_h_
#define	_ink_memory_h_

#include <ctype.h>
#include <strings.h>

#include "ink_config.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#if TS_HAS_JEMALLOC
#include <jemalloc/jemalloc.h>
/* TODO: Should this have a value ? */
#define ATS_MMAP_MAX 0
#else
#if HAVE_MALLOC_H
#include <malloc.h>
#define ATS_MMAP_MAX M_MMAP_MAX
#endif // ! HAVE_MALLOC_H
#endif // ! TS_HAS_JEMALLOC

#ifndef MADV_NORMAL
#define MADV_NORMAL 0
#endif

#ifndef MADV_RANDOM
#define MADV_RANDOM 1
#endif

#ifndef MADV_SEQUENTIAL
#define MADV_SEQUENTIAL 2
#endif

#ifndef MADV_WILLNEED
#define MADV_WILLNEED 3
#endif

#ifndef MADV_DONTNEED
#define MADV_DONTNEED 4
#endif

#ifdef __cplusplus
extern "C" {
#endif                          /* __cplusplus */

  void *  ats_malloc(size_t size);
  void *  ats_calloc(size_t nelem, size_t elsize);
  void *  ats_realloc(void *ptr, size_t size);
  void *  ats_memalign(size_t alignment, size_t size);
  void    ats_free(void *ptr);
  void *  ats_free_null(void *ptr);
  void    ats_memalign_free(void *ptr);
  int     ats_mallopt(int param, int value);

  int     ats_msync(caddr_t addr, size_t len, caddr_t end, int flags);
  int     ats_madvise(caddr_t addr, size_t len, int flags);
  int     ats_mlock(caddr_t addr, size_t len);

  static inline size_t __attribute__((const)) ats_pagesize(void)
  {
    static size_t page_size;

    if (page_size)
      return page_size;

#if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
    page_size = (size_t)sysconf(_SC_PAGESIZE);
#elif defined(HAVE_GETPAGESIZE)
    page_size = (size_t)getpagesize()
#else
    page_size = (size_t)8192;
#endif

    return page_size;
  }

/* Some convenience wrappers around strdup() functionality */
char *_xstrdup(const char *str, int length, const char *path);

#define ats_strdup(p)        _xstrdup((p), -1, NULL)
#define ats_strndup(p,n)     _xstrdup((p), n, NULL)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/** Set data to zero.

    Calls @c memset on @a t with a value of zero and a length of @c
    sizeof(t). This can be used on ordinary and array variables. While
    this can be used on variables of intrinsic type it's inefficient.

    @note Because this uses templates it cannot be used on unnamed or
    locally scoped structures / classes. This is an inherent
    limitation of templates.

    Examples:
    @code
    foo bar; // value.
    ink_zero(bar); // zero bar.

    foo *bar; // pointer.
    ink_zero(bar); // WRONG - makes the pointer @a bar zero.
    ink_zero(*bar); // zero what bar points at.

    foo bar[ZOMG]; // Array of structs.
    ink_zero(bar); // Zero all structs in array.

    foo *bar[ZOMG]; // array of pointers.
    ink_zero(bar); // zero all pointers in the array.
    @endcode
    
 */
template < typename T > inline void
ink_zero(T& t) {
  memset(&t, 0, sizeof(t));
}

/** Locally scoped holder for a chunk of memory allocated via these functions.
    If this pointer is assigned the current memory (if any) is freed.
    The memory is also freed when the object is destructed. This makes
    handling temporary memory in a function more robust.

    @internal A poor substitute for a real shared pointer copy on write
    class but one step at a time. It's better than doing this by
    hand every time.
*/
template <typename T> class xptr {
 public:
  typedef xptr self;

 xptr()
   : m_ptr(0)
    { }

  /// Construct from allocated memory.
  /// @note @a ptr must refer to memory allocated @c ats_malloc.
  explicit xptr(T* ptr)
    : m_ptr(ptr)
  { }

  /// Construct and initialized with memory for @a n instances of @a T.
  explicit xptr(size_t n)
    : m_ptr((T *)ats_malloc(sizeof(T) * n))
    { }

  /// Destructor - free memory held by this instance.
  ~xptr()
    {
      ats_free(m_ptr);
    }

  /// Assign memory.
  /// @note @a ptr must be allocated via @c ats_malloc.
  self& operator = (T* ptr) {
    ats_free(m_ptr);
    m_ptr = ptr;
    return *this;
  }

  /// Auto convert to a raw pointer.
  operator T* () { return m_ptr; }

  /// Auto convert to raw pointer.
  operator T const* () const { return m_ptr; }

  /// Boolean operator. Returns true if we are pointing to valid memory.
  operator bool() const { return m_ptr != 0; }

  /** Release memory from control of this instance.

      @note Although direct assignment is forbidden due to the
      non-obvious semantics, a pointer can be moved (@b not copied) from
      one instance to another using this method.
      @code
      new_ptr = old_ptr.release();
      @endcode
      This is by design so any such transfer is always explicit.
  */
  T* release() {
    T* zret = m_ptr;
    m_ptr = 0;
    return zret;
  }

 private:
  T* m_ptr; ///< Pointer to allocated memory.

  /// Copy constructor - forbidden.
  xptr(self const& that);

  /// Self assignment - forbidden.
  self& operator = (self const& that);
};


// Special operators for xptr<char>
/** Combine two strings as file paths.
    Trailing and leading separators for @a lhs and @a rhs respectively
    are handled to yield exactly one separator.
    @return A newly @x ats_malloc string of the combined paths.
*/
inline char*
path_join (xptr<char> const& lhs, xptr<char> const& rhs)
{
  size_t ln = strlen(lhs);
  size_t rn = strlen(rhs);
  char const* rptr = rhs; // May need to be modified.

  if (ln && lhs[ln-1] == '/') --ln; // drop trailing separator.
  if (rn && *rptr == '/') --rn, ++rptr; // drop leading separator.

  char* x = static_cast<char*>(ats_malloc(ln + rn + 2));

  memcpy(x, lhs, ln);
  x[ln] = '/';
  memcpy(x + ln + 1,  rptr, rn);
  x[ln+rn+1] = 0; // terminate string.

  return x;
}

#endif  /* __cplusplus */

#endif
