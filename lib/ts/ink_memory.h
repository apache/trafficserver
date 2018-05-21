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
#pragma once

#include <cctype>
#include <cstring>
#include <strings.h>
#include <cinttypes>
#include <string>
#include <string_view>

#include "ts/ink_config.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#if TS_HAS_JEMALLOC
#include <jemalloc/jemalloc.h>
#else
#if HAVE_MALLOC_H
#include <malloc.h>
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
#endif /* __cplusplus */

struct IOVec : public iovec {
  IOVec()
  {
    iov_base = nullptr;
    iov_len  = 0;
  }
  IOVec(void *base, size_t len)
  {
    iov_base = base;
    iov_len  = len;
  }
};

void *ats_malloc(size_t size);
void *ats_calloc(size_t nelem, size_t elsize);
void *ats_realloc(void *ptr, size_t size);
void *ats_memalign(size_t alignment, size_t size);
void ats_free(void *ptr);
void *ats_free_null(void *ptr);
void ats_memalign_free(void *ptr);
int ats_mallopt(int param, int value);

int ats_msync(caddr_t addr, size_t len, caddr_t end, int flags);
int ats_madvise(caddr_t addr, size_t len, int flags);
int ats_mlock(caddr_t addr, size_t len);

void *ats_track_malloc(size_t size, uint64_t *stat);
void *ats_track_realloc(void *ptr, size_t size, uint64_t *alloc_stat, uint64_t *free_stat);
void ats_track_free(void *ptr, uint64_t *stat);

static inline size_t __attribute__((const)) ats_pagesize(void)
{
  static size_t page_size;

  if (page_size)
    return page_size;

#if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
  long ret  = sysconf(_SC_PAGESIZE);
  page_size = (size_t)((ret > -1) ? ret : 8192);
#elif defined(HAVE_GETPAGESIZE)
  page_size = (size_t)getpagesize()
#else
  page_size = (size_t)8192;
#endif

  return page_size;
}

/* Some convenience wrappers around strdup() functionality */
char *_xstrdup(const char *str, int length, const char *path);

#define ats_strdup(p) _xstrdup((p), -1, nullptr)

#define ats_strndup(p, n) _xstrdup((p), n, nullptr)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

// this is to help with migration to a std::string issue with older code that
// expects char* being copied. As more code moves to std::string, this can be
// removed to avoid these extra copies.
inline char *
ats_stringdup(std::string const &p)
{
  return p.empty() ? nullptr : _xstrdup(p.data(), p.size(), nullptr);
}

inline char *
ats_stringdup(std::string_view const &p)
{
  return p.empty() ? nullptr : _xstrdup(p.data(), p.size(), nullptr);
}

template <typename PtrType, typename SizeType>
static inline IOVec
make_iovec(PtrType ptr, SizeType sz)
{
  IOVec iov = {ptr, static_cast<size_t>(sz)};
  return iov;
}

template <typename PtrType, unsigned N> static inline IOVec make_iovec(PtrType (&array)[N])
{
  IOVec iov = {&array[0], static_cast<size_t>(sizeof(array))};
  return iov;
}

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
template <typename T>
inline void
ink_zero(T &t)
{
  memset(static_cast<void *>(&t), 0, sizeof(t));
}

/** Scoped resources.

    An instance of this class is used to hold a contingent resource. When this object goes out of scope
    the resource is destroyed. If the resource needs to be kept valid it can be released from this container.
    The standard usage pattern is
    - Allocate resource.
    - Perform various other checks or resource allocations which, if they fail, require this resource to be destroyed.
    - Release the resource.

    This serves as a base implementation, actual use is usually through specialized subclasses.

    @see ats_scoped_fd
    @see ats_scoped_mem
    @see ats_scoped_obj

    For example, if you open a file descriptor and have to do other checks which result in having to call
    @c close in each @c if clause.

    @code
    int fd = open(...);
    if (X) { Log(...); close(fd); return -1; }
    if (Y) { Log(...); close(fd); return -1; }
    ...
    return fd;
    @endcode

    Change this to
    @code
    ats_scoped_fd fd(open(...);
    if (X) { Log(...) return; } // fd is closed upon return.
    if (Y) { Log(...) return; } // fd is closed upon return.
    fd.release(); // fd will not be automatically closed after this.
    return fd;
    @endcode

    The @a TRAITS class must have the following members.

    @code
    value_type; // Declaration type of the resource.
    RT initValue(); // Return canonical initialization value for RT.
    bool isValid(RT); // Check for validity. Can take a reference or const reference.
    void destroy(RT); // Cleanup. Can take a reference.
    @endcode

    @c isValid should return @c true if the resource instance is valid and @c false if it is not valid.

    @c initValue must be a constant value of @a RT such that @c isValid(INVALID) is @c false. This
    is used to initialize the object when the container is empty.

    @c destroy should perform cleanup on the object.

    @internal One might think the initialization value should be a constant but you can't initialize
    non-integral class constants (such as pointers) in C++ (you can in C++ eleventy but we can't
    require that). We can only hope the compiler is smart enough to optimize out functions returning
    constants.

    @internal For subclasses, you need to override the default constructor, value constructor, and
    assignment operator. This will be easier with C++ eleventy.

*/

template <typename TRAITS ///< Traits object.
          >
class ats_scoped_resource
{
public:
  typedef TRAITS Traits;                          ///< Make template arg available.
  typedef typename TRAITS::value_type value_type; ///< Import value type.
  typedef ats_scoped_resource self;               ///< Self reference type.

public:
  /// Default constructor - an empty container.
  ats_scoped_resource() : _r(Traits::initValue()) {}
  /// Construct with contained resource.
  explicit ats_scoped_resource(value_type rt) : _r(rt) {}
  /// Destructor.
  ~ats_scoped_resource()
  {
    if (Traits::isValid(_r))
      Traits::destroy(_r);
  }

  /// Automatic conversion to resource type.
  operator value_type() const { return _r; }
  /// Explicit conversion to resource type.
  /// @note Syntactic sugar for @c static_cast<value_type>(instance). Required when passing to var arg function
  /// as automatic conversion won't be done.
  value_type
  get() const
  {
    return _r;
  }

  /** Release resource from this container.
      After this call, the resource will @b not cleaned up when this container is destructed.

      @note Although direct assignment is forbidden due to the non-obvious semantics, a pointer can
      be moved (@b not copied) from one instance to another using this method.
      @code
      new_ptr = old_ptr.release();
      @endcode
      This is by design.

      @return The no longer contained resource.
  */
  value_type
  release()
  {
    value_type zret = _r;
    _r              = Traits::initValue();
    return zret;
  }

  /** Place a new resource @a rt in the container.
      Any resource currently contained is destroyed.
      This object becomes the owner of @a rt.

      @internal This is usually overridden in subclasses to get the return type adjusted.
  */
  self &
  operator=(value_type rt)
  {
    if (Traits::isValid(_r))
      Traits::destroy(_r);
    _r = rt;
    return *this;
  }

  /// Equality.
  bool
  operator==(value_type rt) const
  {
    return _r == rt;
  }

  /// Inequality.
  bool
  operator!=(value_type rt) const
  {
    return _r != rt;
  }

  /// Test if the contained resource is valid.
  bool
  isValid() const
  {
    return Traits::isValid(_r);
  }

protected:
  value_type _r; ///< Resource.
private:
  ats_scoped_resource(self const &) = delete; ///< Copy constructor not permitted.
  self &operator=(self const &) = delete;     ///< Self assignment not permitted.
};

namespace detail
{
/** Traits for @c ats_scoped_resource for file descriptors.
 */
struct SCOPED_FD_TRAITS {
  typedef int value_type;
  static int
  initValue()
  {
    return -1;
  }
  static bool
  isValid(int fd)
  {
    return fd >= 0;
  }
  static void
  destroy(int fd)
  {
    close(fd);
  }
};
} // namespace detail
/** File descriptor as a scoped resource.
 */
class ats_scoped_fd : public ats_scoped_resource<detail::SCOPED_FD_TRAITS>
{
public:
  typedef ats_scoped_resource<detail::SCOPED_FD_TRAITS> super_type; ///< Super type.
  typedef ats_scoped_fd self_type;                                  ///< Self reference type.

  /// Default constructor - an empty container.
  ats_scoped_fd() : super_type() {}
  /// Construct with contained resource.
  explicit ats_scoped_fd(value_type rt) : super_type(rt) {}
  /// Move ownership constructor.
  ats_scoped_fd(self_type &&that) : super_type(that.release()) {}
  /** Place a new resource @a rt in the container.
      Any resource currently contained is destroyed.
      This object becomes the owner of @a rt.
  */
  self_type &
  operator=(value_type rt)
  {
    super_type::operator=(rt);
    return *this;
  }
  /** Move the resource from @a that to @a this.
      If @a this has a resource, that resource is destroyed.
      This object becomes the owner of the resource in @a that.
  */
  self_type &
  operator=(self_type &&that)
  {
    super_type::operator=(that.release());
    return *this;
  }
};

namespace detail
{
/** Traits for @c ats_scoped_resource for pointers from @c ats_malloc.
 */
template <typename T ///< Underlying type (not the pointer type).
          >
struct SCOPED_MALLOC_TRAITS {
  typedef T *value_type;
  static T *
  initValue()
  {
    return nullptr;
  }
  static bool
  isValid(T *t)
  {
    return nullptr != t;
  }
  static void
  destroy(T *t)
  {
    ats_free(t);
  }
};

/// Traits for @c ats_scoped_resource for objects using @c new and @c delete.
template <typename T ///< Underlying type - not the pointer type.
          >
struct SCOPED_OBJECT_TRAITS {
  typedef T *value_type;
  static T *
  initValue()
  {
    return nullptr;
  }
  static bool
  isValid(T *t)
  {
    return nullptr != t;
  }
  static void
  destroy(T *t)
  {
    delete t;
  }
};
} // namespace detail

/** Specialization of @c ats_scoped_resource for strings.
    This contains an allocated string that is cleaned up if not explicitly released.
*/
class ats_scoped_str : public ats_scoped_resource<detail::SCOPED_MALLOC_TRAITS<char>>
{
public:
  typedef ats_scoped_resource<detail::SCOPED_MALLOC_TRAITS<char>> super; ///< Super type.
  typedef ats_scoped_str self;                                           ///< Self reference type.

  /// Default constructor (no string).
  ats_scoped_str() {}
  /// Construct and allocate @a n bytes for a string.
  explicit ats_scoped_str(size_t n) : super(static_cast<char *>(ats_malloc(n))) {}
  /// Put string @a s in this container for cleanup.
  explicit ats_scoped_str(char *s) : super(s) {}
  // constructor with std::string
  explicit ats_scoped_str(const std::string &s)
  {
    if (s.empty())
      _r = nullptr;
    else
      _r = strdup(s.c_str());
  }
  // constructor with string_view
  explicit ats_scoped_str(const std::string_view &s)
  {
    if (s.empty())
      _r = nullptr;
    else
      _r = strdup(s.data());
  }
  /// Assign a string @a s to this container.
  self &
  operator=(char *s)
  {
    super::operator=(s);
    return *this;
  }
  // std::string case
  self &
  operator=(const std::string &s)
  {
    if (s.empty())
      _r = nullptr;
    else
      _r = strdup(s.c_str());
    return *this;
  }
  // string_view case
  self &
  operator=(const std::string_view &s)
  {
    if (s.empty())
      _r = nullptr;
    else
      _r = strdup(s.data());
    return *this;
  }
};

/** Specialization of @c ats_scoped_resource for pointers allocated with @c ats_malloc.
 */
template <typename T ///< Underlying (not pointer) type.
          >
class ats_scoped_mem : public ats_scoped_resource<detail::SCOPED_MALLOC_TRAITS<T>>
{
public:
  typedef ats_scoped_resource<detail::SCOPED_MALLOC_TRAITS<T>> super; ///< Super type.
  typedef ats_scoped_mem self;                                        ///< Self reference.

  self &
  operator=(T *ptr)
  {
    super::operator=(ptr);
    return *this;
  }
};

/** Specialization of @c ats_scoped_resource for objects.
    This handles a pointer to an object created by @c new and destroyed by @c delete.
*/

template <typename T /// Underlying (not pointer) type.
          >
class ats_scoped_obj : public ats_scoped_resource<detail::SCOPED_OBJECT_TRAITS<T>>
{
public:
  typedef ats_scoped_resource<detail::SCOPED_OBJECT_TRAITS<T>> super; ///< Super type.
  typedef ats_scoped_obj self;                                        ///< Self reference.

  /// Default constructor - an empty container.
  ats_scoped_obj() : super() {}
  /// Construct with contained resource.
  explicit ats_scoped_obj(T *obj) : super(obj) {}
  self &
  operator=(T *obj)
  {
    super::operator=(obj);
    return *this;
  }

  T *operator->() const { return *this; }
};

/** Combine two strings as file paths.
     Trailing and leading separators for @a lhs and @a rhs respectively
     are handled to yield exactly one separator.
     @return A newly @x ats_malloc string of the combined paths.
*/
inline char *
path_join(ats_scoped_str const &lhs, ats_scoped_str const &rhs)
{
  size_t ln        = strlen(lhs);
  size_t rn        = strlen(rhs);
  const char *rptr = rhs; // May need to be modified.

  if (ln && lhs[ln - 1] == '/')
    --ln; // drop trailing separator.
  if (rn && *rptr == '/')
    --rn, ++rptr; // drop leading separator.

  ats_scoped_str x(ln + rn + 2);

  memcpy(x, lhs, ln);
  x[ln] = '/';
  memcpy(x + ln + 1, rptr, rn);
  x[ln + rn + 1] = 0; // terminate string.

  return x.release();
}
#endif /* __cplusplus */
