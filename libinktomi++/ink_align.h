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

#define INK_ALIGN_LONG (sizeof(long) - 1)
#define INK_ALIGN_DOUBLE (sizeof(double) - 1)
#define INK_ALIGN_INT (sizeof(int) - 1)
#define INK_ALIGN_SHORT (sizeof(short)-1)

//
// Move a pointer forward until it meets the alignment width
// specified (as a mask).
//

static inline void *
align_pointer_forward(const void *pointer_, int widthmask)
{
  char *pointer = (char *) pointer_;
  //
  // Round up alignment..
  //
  pointer = (char *)
    (((unsigned long) pointer + widthmask) & (~widthmask));

  return (void *) pointer;
}

//
// Move a pointer forward until it meets the alignment width specified
// (as a mask), and zero out the contents of the space you're skipping
// over.
//
static inline void *
align_pointer_forward_and_zero(const void *pointer_, int widthmask)
{
  char *pointer = (char *) pointer_;
  //
  // Round up alignment..
  //
  while ((((unsigned long) pointer) & widthmask) != 0) {
    *pointer = 0;
    pointer++;
  }

  return (void *) pointer;
}

//
// We include two signatures for the same function to avoid error
// messages concerning coercion between void* and unsigned long.
// We could handle this using casts, but that's more prone to
// errors during porting.
//
#if 0
static inline void *
DOUBLE_ALIGN(void *_x)
{
  return align_pointer_forward(_x, INK_ALIGN_DOUBLE);
}

static inline inku64
DOUBLE_ALIGN(inku64 _x)
{
  return (inku64) align_pointer_forward((void *) _x, INK_ALIGN_DOUBLE);
}
#endif

#endif
