/** @file

  A brief file description

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

#ifndef __INK_RESOURCE_H__
#define __INK_RESOURCE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ink_apidefs.h"
#include "ink_port.h"
#include "ink_memory.h"

extern volatile int res_track_memory;   /* set this to zero to disable resource tracking */

#define __RES_PATH(x)   #x
#define _RES_PATH(x)    __RES_PATH (x)
#define RES_PATH(x)     x __FILE__ ":" _RES_PATH (__LINE__)

struct Resource
{
  void *magic;
  struct Resource *next;
  const char *path;
  int64_t value;
  int64_t snapshot;
  int64_t baseline;
};

// TODO: TS-567 Support turning this off in the case of "memory debugging" being
// enabled in the ./configure phase. Also, figure out if / how this could integrate
// with jemalloc / tcmalloc's features of enabling memory debugging.
#define RES_TRACK_MEMORY_DEFAULT 0      /* default value for res_track_memory variable */

char *_xstrdup(const char *str, int length, const char *path);
void xdump(void);

#if defined(__cplusplus)
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
    : m_ptr(ats_malloc(sizeof(T) * n))
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

  /// Auto conver to raw pointer.
  operator T const* () const { return m_ptr; }

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

#endif // c++

#endif /* __INK_RESOURCE_H__ */
