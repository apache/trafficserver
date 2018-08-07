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

#include <cstdarg>
#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/TextBuffer.h"

/****************************************************************************
 *
 *  TextBuffer.cc - A self-expanding buffer, primarily meant for strings
 *
 *
 *
 ****************************************************************************/

TextBuffer::TextBuffer(int size)
{
  bufferStart = nullptr;
  nextAdd     = nullptr;
  currentSize = spaceLeft = 0;
  if (size > 0) {
    // Institute a minimum size
    if (size < 1024) {
      size = 1024;
    }

    bufferStart = (char *)ats_malloc(size);
    nextAdd     = bufferStart;
    currentSize = size;
    spaceLeft   = size - 1; // Leave room for a terminator;
    nextAdd[0]  = '\0';
  }
}

TextBuffer::~TextBuffer()
{
  ats_free(bufferStart);
}

char *
TextBuffer::release()
{
  char *ret = bufferStart;

  bufferStart = nextAdd = nullptr;
  currentSize = spaceLeft = 0;

  return ret;
}

// void TextBuffer::reUse()
//
//   Sets the text buffer for reuse by repositioning the
//     ptrs to beginning of buffer.  The buffer space is
//     reused
void
TextBuffer::reUse()
{
  if (bufferStart != nullptr) {
    nextAdd    = bufferStart;
    spaceLeft  = currentSize - 1;
    nextAdd[0] = '\0';
  }
}

// int TextBuffer::copyFrom(void*,int num_bytes)
//
//
//  Copy N bytes (determined by num_bytes) on to the
//  end of the buffer.
//
//  Returns the number of bytes copies or
//  -1 if there was insufficient memory
int
TextBuffer::copyFrom(const void *source, unsigned num_bytes)
{
  // Get more space if necessary
  if (spaceLeft < num_bytes) {
    if (enlargeBuffer(num_bytes) == -1) {
      return -1;
    }
  }

  memcpy(nextAdd, source, num_bytes);
  spaceLeft -= num_bytes;

  nextAdd += num_bytes;
  nextAdd[0] = '\0';

  return num_bytes;
}

//  TextBuffer::enlargeBuffer(int n)
//
//  Enlarge the buffer so at least at N
//    bytes are free in the buffer.
//
//  Always enlarges by a power of two.
//
//  Returns -1 if insufficient memory,
//    zero otherwise
int
TextBuffer::enlargeBuffer(unsigned N)
{
  unsigned addedSize = 0;
  unsigned newSize   = (currentSize ? currentSize : 1) * 2;
  char *newSpace;

  if (spaceLeft < N) {
    while ((newSize - currentSize) < N) {
      newSize *= 2;
    }

    addedSize = newSize - currentSize;

    newSpace = (char *)ats_realloc(bufferStart, newSize);
    if (newSpace != nullptr) {
      nextAdd     = newSpace + (unsigned)(nextAdd - bufferStart);
      bufferStart = newSpace;
      spaceLeft += addedSize;
      currentSize = newSize;
    } else {
      // Out of Memory, Sigh
      return -1;
    }
  }

  return 0;
}

// int TextBuffer::rawReadFromFile
//
// - Issues a single read command on the file descriptor or handle
//   passed in and reads in raw data (not assumed to be text, no
//   string terminators added).
// - Cannot read from file descriptor on win32 because the win32
//   read() function replaces CR-LF with LF if the file is not
//   opened in binary mode.
int
TextBuffer::rawReadFromFile(int fd)
{
  int readSize;

  // Check to see if we have got a reasonable amount of space left in our
  //   buffer, if not try to get some more
  if (spaceLeft < 4096) {
    if (enlargeBuffer(4096) == -1) {
      return -1;
    }
  }

  readSize = read(fd, nextAdd, spaceLeft - 1);

  if (readSize == 0) { // EOF
    return 0;
  } else if (readSize < 0) {
    // Error on read
    return readSize;
  } else {
    nextAdd = nextAdd + readSize;
    spaceLeft -= readSize;
    return readSize;
  }
}

// Read the entire contents of the given file descriptor.
void
TextBuffer::slurp(int fd)
{
  int nbytes;

  do {
    nbytes = readFromFD(fd);
  } while (nbytes > 0);
}

// int TextBuffer::readFromFD(int fd)
//
// Issues a single read command on the file
// descriptor passed in.  Attempts to read a minimum of
// 512 bytes from file descriptor passed.
int
TextBuffer::readFromFD(int fd)
{
  int readSize;

  // Check to see if we have got a reasonable amount of space left in our
  //   buffer, if not try to get some more
  if (spaceLeft < 512) {
    if (enlargeBuffer(512) == -1) {
      return -1;
    }
  }

  readSize = read(fd, nextAdd, spaceLeft - 1);

  if (readSize == 0) {
    // Socket is empty so we are done
    return 0;
  } else if (readSize < 0) {
    // Error on read
    return readSize;
  } else {
    nextAdd    = nextAdd + readSize;
    nextAdd[0] = '\0';
    spaceLeft -= readSize + 1;
    return readSize;
  }
}

void
TextBuffer::vformat(const char *fmt, va_list ap)
{
  for (bool done = false; !done;) {
    int num;

    // Copy the args in case the buffer isn't big enough and we need to
    // try again. Vsnprintf modifies the va_list on each pass.
    va_list args;
    va_copy(args, ap);

    num = vsnprintf(this->nextAdd, this->spaceLeft, fmt, args);

    va_end(args);

    if ((unsigned)num < this->spaceLeft) {
      // We had enough space to format including the NUL. Since the returned character
      // count does not include the NUL, we can just increment and the next format will
      // overwrite the previous NUL.
      this->spaceLeft -= num;
      this->nextAdd += num;
      done = true;
    } else {
      if (enlargeBuffer(num + 1) == -1) {
        return;
      }
    }
  }
}

void
TextBuffer::format(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vformat(fmt, ap);
  va_end(ap);
}

void
TextBuffer::chomp()
{
  while ((nextAdd > bufferStart) && (nextAdd[-1] == '\n')) {
    --nextAdd;
    ++spaceLeft;
    *nextAdd = '\0';
  }
}
