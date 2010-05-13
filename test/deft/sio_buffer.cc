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

/****************************************************************************

   sio_buffer.cc

   Description:
      A simple single reader io buffer which keeps data continguous
         by copying it


 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "sio_buffer.h"
#include "ink_assert.h"

sio_buffer::sio_buffer()
{
  init_buffer(DEFAULT_SIO_SIZE);
}

sio_buffer::sio_buffer(int init_size)
{
  init_buffer(init_size);
}

void
sio_buffer::init_buffer(int size)
{
  ink_debug_assert(size > 0);

  if (size <= 0) {
    size = DEFAULT_SIO_SIZE;
  }

  raw_start = (char *) malloc(size);
  raw_end = raw_start + size;

  data_start = data_end = raw_start;
}

sio_buffer::~sio_buffer()
{
  free(raw_start);
  raw_start = NULL;
}

int
sio_buffer::expand_to(int size)
{

  // Check to see if we've already got enough space
  int wavail = write_avail();
  if (wavail >= size) {
    return wavail;
  }

  int ravail = read_avail();
  int raw_size = raw_end - raw_start;

  // Check to see if we just need to move the data to the head of
  //   the buffer
  if (raw_size - ravail >= size) {
    ::memcpy(raw_start, data_start, ravail);
    data_end = raw_start + ravail;
    data_start = raw_start;
  } else {
    while (raw_size - ravail < size) {
      raw_size = raw_size * 2;
    }

    char *new_buf = (char *) malloc(raw_size);
    ::memcpy(new_buf, data_start, ravail);
    raw_end = new_buf + raw_size;
    data_end = new_buf + ravail;
    data_start = new_buf;

    free(raw_start);
    raw_start = new_buf;
  }

  return write_avail();
}

char *
sio_buffer::start()
{
  return data_start;
}

char *
sio_buffer::end()
{
  return data_end;
}

int
sio_buffer::fill(const char *data, int data_len)
{

  ink_debug_assert(data_len >= 0);

  if (data_len <= 0) {
    return 0;
  }

  expand_to(data_len);
  ink_debug_assert(write_avail() >= data_len);

  ::memcpy(data_end, data, data_len);
  data_end += data_len;

  return data_len;
}

int
sio_buffer::fill(int n)
{
  int wavail = write_avail();
  ink_debug_assert(n <= wavail);
  data_end += n;
  return n;
}

int
sio_buffer::read_avail()
{
  int r = data_end - data_start;
  ink_debug_assert(r >= 0);
  return r;
}

int
sio_buffer::write_avail()
{
  int r = raw_end - data_end;
  ink_debug_assert(r >= 0);
  return r;
}

void
sio_buffer::reset()
{
  data_start = data_end = raw_start;
}

void
sio_buffer::consume(int n)
{
  int ravail = read_avail();
  ink_debug_assert(n <= ravail);

  if (n > ravail) {
    n = ravail;
  }

  data_start += n;
  ink_debug_assert(data_start <= data_end);
}

char *
sio_buffer::memchr(int c, int len, int offset)
{

  int ravail = read_avail();
  ink_debug_assert((len == INT_MAX && offset <= ravail) || len <= ravail - offset);

  if (offset > ravail) {
    return NULL;
  }

  if (len == INT_MAX) {
    len = ravail - offset;
  } else if (len > ravail - offset) {
    len = ravail - offset;
  }

  return (char *)::memchr(data_start + offset, c, len);
}

int
sio_buffer::read(char *buf, int len)
{

  int to_read = read_avail();
  if (len < to_read) {
    to_read = len;
  }

  ::memcpy(buf, data_start, to_read);
  consume(to_read);
  return to_read;
}

char *
sio_buffer::memcpy(char *buf, int len, int offset)
{
  int ravail = offset;
  ink_debug_assert(offset <= ravail);

  if (len == INT_MAX) {
    len = ravail - offset;
  } else if (len > ravail - offset) {
    len = ravail - offset;
  }

  ::memcpy(buf + offset, data_start, len);
  return buf;
}
