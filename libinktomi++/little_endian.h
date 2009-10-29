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

#if !defined (_little_endian_h_)
#define	_little_endian_h_

#include "ink_platform.h"
#include "ink_port.h"
#include "ink_assert.h"
#include "ink_string.h"

#if defined(_i386_) || defined(__i386)
#define ALREADY_IN_LITTLE_ENDIAN
#else
#define NEED_TO_CONVERT_TO_LITTLE_ENDIAN_FROM_UNKNOWN_ENDIAN
#endif

/****************************************************************************
  little_endian.h

  Contains implementation of inkuXX_to_le to functions that convert's known host order
  to little endian order. If you don't know the "endianness" of your host, you may 
  want to use the LittleEndianBuffer class defined below.

  Contains implementation of class LittleEndianBuffer, which implements storing/loading
  primitive types like char,int,short to/from a buffer stored in little indian order. 
  That means variables in host order are converted to little indian before storing it on 
  the buffer and variables from the buffer are converted to host order when loading. An 
  example of its use (and a test to check that it works) is at the bottom of the file. 

 ****************************************************************************/

#include "ink_port.h"
#include "ink_assert.h"
// TODO ifdef based on machine type
//   Currently only running on big enidan (sparc)

// There's a duplicate effort, need to merge
// implemenations, each has it's advantages

inline inku64
inku64_to_le(inku64 in)
{
#if defined(ALREADY_IN_LITTLE_ENDIAN)
  return in;
#else
  ink_assert(!"Architecture not supported");
#endif
}

inline inku64
inku64_from_le(inku64 in)
{
#if defined(ALREADY_IN_LITTLE_ENDIAN)
  return in;
#else
  ink_assert(!"Architecture not supported");
#endif
}

inline inku32
inku32_to_le(inku32 in)
{
#if defined(ALREADY_IN_LITTLE_ENDIAN)
  return in;
#else
  ink_assert(!"Architecture not supported");
#endif
  return 0;
}

inline inku32
inku32_from_le(inku32 in)
{
#if defined(ALREADY_IN_LITTLE_ENDIAN)
  return in;
#else
  ink_assert(!"Architecture not supported");
#endif
  return 0;
}

inline inku16
inku16_to_le(inku16 in)
{
#if defined(ALREADY_IN_LITTLE_ENDIAN)
  return in;
#else
  ink_assert(!"Architecture not supported");
#endif
  return 0;
}

inline inku16
inku16_from_le(inku16 in)
{
#if defined(ALREADY_IN_LITTLE_ENDIAN)
  return in;
#else
  ink_assert(!"Architecture not supported");
#endif
  return 0;
}


class LittleEndianBuffer
{
public:
  // Need to pass in the raw stream of bytes to the constructor
  LittleEndianBuffer(inku8 * buf, const size_t size)
  {
    buff = buf;
    buffSize = size;
  }
  inline ink8 load(int i, ink8 & val) const;
  inline ink16 load(int i, ink16 & val) const;
  inline ink32 load(int i, ink32 & val) const;
  inline ink8 *load(int i, ink8 * val) const;
  inline inku8 load(int i, inku8 & val) const;
  inline inku16 load(int i, inku16 & val) const;
  inline inku32 load(int i, inku32 & val) const;

  inline int store(int i, ink8 val) const;
  inline int store(int i, ink16 val) const;
  inline int store(int i, ink32 val) const;
  inline int store(int i, ink8 * val) const;
  inline int store(int i, inku8 val) const;
  inline int store(int i, inku16 val) const;
  inline int store(int i, inku32 val) const;

private:
  inku8 * buff;
  size_t buffSize;
};





inline ink8
LittleEndianBuffer::load(int i, ink8 & val) const
{
  val = (ink8) buff[i];
  return val;
}
inline ink8 *
LittleEndianBuffer::load(int i, ink8 * val) const
{
  if (val == NULL)
    return (ink8 *) NULL;
  const size_t valSize = strlen(val);
  ink_release_assert((valSize) < (buffSize - i));

  ink_strncpy((ink8 *) & buff[i], val, buffSize - i);
  return val;
}
inline int
LittleEndianBuffer::store(int i, ink8 * val) const
{
  if (val == NULL)
    return -1;
  const size_t valSize = strlen(val);
  ink_release_assert((valSize) < (buffSize - i));

  ink_strncpy((ink8 *) & buff[i], val, buffSize - i);
  return (i + valSize);
}
inline int
LittleEndianBuffer::store(int i, ink8 val) const
{
  buff[i] = val;
  return (i + sizeof(ink8));
}
inline ink16
LittleEndianBuffer::load(int i, ink16 & val) const
{
  val = (inku8) buff[i] | ((inku8) buff[i + 1]) << 8;
  return (ink16) val;
}
inline int
LittleEndianBuffer::store(int i, ink16 val) const
{
  buff[i] = val & 0xFF;
  buff[i + 1] = val >> 8;
  return (i + sizeof(ink16));
}
inline ink32
LittleEndianBuffer::load(int i, ink32 & val) const
{
  inku16 first = load(i, first);
  inku16 second = load(i + 2, second);
  val = (ink32) (first | (second << 16));
  return val;
}
inline int
LittleEndianBuffer::store(int i, ink32 val) const
{
  store(i, (ink16) (val & 0xFFFF));
  store(i + 2, (ink16) (val >> 16));
  return i + sizeof(ink32);
}

//Unsigned versions

inline inku8
LittleEndianBuffer::load(int i, inku8 & val) const
{
  val = (inku8) buff[i];
  return val;
}
inline int
LittleEndianBuffer::store(int i, inku8 val) const
{
  buff[i] = val;
  return i + sizeof(inku8);
}
inline inku16
LittleEndianBuffer::load(int i, inku16 & val) const
{
  inku16 first = (inku8) buff[i];
  inku16 second = (inku8) buff[i + 1];
  val = (first | second << 8);
  return val;
}
inline int
LittleEndianBuffer::store(int i, inku16 val) const
{
  buff[i] = val & 0xFF;
  buff[i + 1] = val >> 8;
  return i + sizeof(inku16);
}
inline inku32
LittleEndianBuffer::load(int i, inku32 & val) const
{
  inku16 first, second;

  load(i, first);
  load(i + 2, second);
  val = first | (second << 16);
  return val;
}
inline int
LittleEndianBuffer::store(int i, inku32 val) const
{
  store(i, (inku16) (val & 0xFFFF));
  store(i + 2, (inku16) (val >> 16));
  return i + sizeof(inku32);
}

#endif


/*
   main () {
     char buffer[1000];
     LittleEndianBuffer Buff(buffer);
     ink16 shortX=199;
     inku16 shortuX=107;
     ink16  shortY = shortX;
     inku16 shortuY = shortuX;

     ink32 intX=29986;
     inku32 intuX=28378;

     ink32 intY = intX;
     inku32 intuY = intuX;

     int i;
     
     i =0;
     i = Buff.store(i,shortX);
     i = Buff.store(i,intX);
     i = Buff.store(i,shortuX);
     i = Buff.store(i,intuX);

     i=0;
     shortX=0;
     intX=0;
     shortuX=0;
     intuX=0;

     Buff.load(i,shortX);
     if (shortY != shortX)
       printf("You are hosed, short!\n");
     i+=sizeof(ink16);
     Buff.load(i,intX);
     if (intY != intX)
       printf("You are hosed, int!\n");
     i+=sizeof(ink32);
     Buff.load(i,shortuX);
     if (shortuY != shortuX)
       printf("You are hosed, shortu!\n");
     i+=sizeof(inku16);
     Buff.load(i,intuX);
     if (intuY != intuX) 
       printf("You are hosed, intu!\n");

     if (shortY != shortX || shortuY != shortuX || intY != intX || intuY != intuX) 
       printf("You are hosed!\n");
     else
       printf("You may be still be hosed, but I have no proof\n");
   }
*/
