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

//-*-c++-*-
#ifndef _ink_align_h_
#define _ink_align_h_

#include "ts/ink_time.h"

union Alias32 {
  uint8_t byte[4];
  int32_t i32;
  uint32_t u32;
};

union Alias64 {
  uint8_t byte[8];
  int32_t i32[2];
  uint32_t u32[2];
  int64_t i64;
  uint64_t u64;
  ink_time_t i_time;
};

/**
 * Alignment macros
 */

#define INK_MIN_ALIGN 8
/* INK_ALIGN() is only to be used to align on a power of 2 boundary */
#define INK_ALIGN(size, boundary) (((size) + ((boundary)-1)) & ~((boundary)-1))

/** Default alignment */
#define INK_ALIGN_DEFAULT(size) INK_ALIGN(size, INK_MIN_ALIGN)

//
// Move a pointer forward until it meets the alignment width.
//
static inline void *
align_pointer_forward(const void *pointer_, size_t alignment)
{
  char *pointer = (char *)pointer_;
  //
  // Round up alignment..
  //
  pointer = (char *)INK_ALIGN((ptrdiff_t)pointer, alignment);

  return (void *)pointer;
}

//
// Move a pointer forward until it meets the alignment width specified,
// and zero out the contents of the space you're skipping over.
//
static inline void *
align_pointer_forward_and_zero(const void *pointer_, size_t alignment)
{
  char *pointer = (char *)pointer_;
  char *aligned = (char *)INK_ALIGN((ptrdiff_t)pointer, alignment);
  //
  // Fill the skippings..
  //
  while (pointer < aligned) {
    *pointer = 0;
    pointer++;
  }

  return (void *)aligned;
}

//
// We include two signatures for the same function to avoid error
// messages concerning coercion between void* and unsigned long.
// We could handle this using casts, but that's more prone to
// errors during porting.
//

#endif
