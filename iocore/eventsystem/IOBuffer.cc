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

#include "ink_unused.h"      /* MAGIC_EDITING_TAG */
/**************************************************************************
  UIOBuffer.cc

**************************************************************************/

#include "P_EventSystem.h"

//
// General Buffer Allocator
//
inkcoreapi Allocator ioBufAllocator[DEFAULT_BUFFER_SIZES];
inkcoreapi ClassAllocator<MIOBuffer> ioAllocator("ioAllocator", DEFAULT_BUFFER_NUMBER);
inkcoreapi ClassAllocator<IOBufferData> ioDataAllocator("ioDataAllocator", DEFAULT_BUFFER_NUMBER);
inkcoreapi ClassAllocator<IOBufferBlock> ioBlockAllocator("ioBlockAllocator", DEFAULT_BUFFER_NUMBER);
ink64 default_large_iobuffer_size = DEFAULT_LARGE_BUFFER_SIZE;
ink64 default_small_iobuffer_size = DEFAULT_SMALL_BUFFER_SIZE;
ink64 max_iobuffer_size = DEFAULT_BUFFER_SIZES - 1;

//
// Initialization
//
void
init_buffer_allocators()
{
  char *name;

  for (int i = 0; i < DEFAULT_BUFFER_SIZES; i++) {
    ink64 s = DEFAULT_BUFFER_BASE_SIZE * (((ink64)1) << i);
    ink64 a = DEFAULT_BUFFER_ALIGNMENT;
    int n = i <= default_large_iobuffer_size ? DEFAULT_BUFFER_NUMBER : DEFAULT_HUGE_BUFFER_NUMBER;
    if (s < a)
      a = s;

    name = NEW(new char[64]);
    ink_snprintf(name, 64, "ioBufAllocator[%d]", i);
    ioBufAllocator[i].re_init(name, s, n, a);
  }
}

ink64
MIOBuffer::remove_append(IOBufferReader * r)
{
  ink64 l = 0;
  while (r->block) {
    Ptr<IOBufferBlock> b = r->block;
    r->block = r->block->next;
    b->_start += r->start_offset;
    if (b->start() >= b->end()) {
      r->start_offset = -r->start_offset;
      continue;
    }
    r->start_offset = 0;
    l += b->read_avail();
    append_block(b);
  }
  r->mbuf->_writer = NULL;
  return l;
}

ink64
MIOBuffer::write(const void *abuf, ink64 alen)
{
  const char *buf = (const char*)abuf;
  ink64 len = alen;
  while (len) {
    if (!_writer)
      add_block();
    ink64 f = _writer->write_avail();
    f = f < len ? f : len;
    if (f > 0) {
      ::memcpy(_writer->end(), buf, f);
      _writer->fill(f);
      buf += f;
      len -= f;
    }
    if (len) {
      if (!_writer->next)
        add_block();
      else
        _writer = _writer->next;
    }
  }
  return alen;
}


#ifdef WRITE_AND_TRANSFER
  /*
   * Same functionality as write but for the one small difference.
   * The space available in the last block is taken from the original
   * and this space becomes available to the copy.
   *
   */
ink64
MIOBuffer::write_and_transfer_left_over_space(IOBufferReader * r, ink64 alen, ink64 offset)
{
  ink64 rval = write(r, alen, offset);
  // reset the end markers of the original so that it cannot
  // make use of the space in the current block
  if (r->mbuf->_writer)
    r->mbuf->_writer->_buf_end = r->mbuf->_writer->_end;
  // reset the end marker of the clone so that it can make
  // use of the space in the current block
  if (_writer) {
    _writer->_buf_end = _writer->data->data() + _writer->block_size();
  }
  return rval;
}

#endif


ink64
MIOBuffer::write(IOBufferReader * r, ink64 alen, ink64 offset)
{
  ink64 len = alen;
  IOBufferBlock *b = r->block;
  offset += r->start_offset;

  while (b && len > 0) {
    ink64 max_bytes = b->read_avail();
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b = b->next;
      continue;
    }
    ink64 bytes;
    if (len<0 || len>= max_bytes)
      bytes = max_bytes;
    else
      bytes = len;
    IOBufferBlock *bb = b->clone();
    bb->_start += offset;
    bb->_buf_end = bb->_end = bb->_start + bytes;
    append_block(bb);
    offset = 0;
    len -= bytes;
    b = b->next;
  }
  return alen - len;
}

ink64
MIOBuffer::puts(char *s, ink64 len)
{
  char *pc = end();
  char *pb = s;
  while (pc < buf_end()) {
    if (len-- <= 0)
      return -1;
    if (!*pb || *pb == '\n') {
      ink64 n = (ink64) (pb - s);
      memcpy(end(), s, n + 1);  // Upto and including '\n'
      end()[n + 1] = 0;
      fill(n + 1);
      return n + 1;
    }
    pc++;
    pb++;
  }
  return 0;
}

ink64
IOBufferReader::read(void *ab, ink64 len)
{
  char *b = (char*)ab;
  ink64 max_bytes = read_avail();
  ink64 bytes = len <= max_bytes ? len : max_bytes;
  ink64 n = bytes;

  while (n) {
    ink64 l = block_read_avail();
    if (n < l)
      l = n;
    ::memcpy(b, start(), l);
    consume(l);
    b += l;
    n -= l;
  }
  return bytes;
}

ink64
IOBufferReader::memchr(char c, ink64 len, ink64 offset)
{
  IOBufferBlock *b = block;
  offset += start_offset;
  ink64 o = offset;

  while (b && len) {
    ink64 max_bytes = b->read_avail();
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b = b->next;
      continue;
    }
    ink64 bytes;
    if (len<0 || len>= max_bytes)
      bytes = max_bytes;
    else
      bytes = len;
    char *s = b->start() + offset;
    char *p = (char *) ink_memchr(s, c, bytes);
    if (p)
      return (ink64) (o - start_offset + p - s);
    o += bytes;
    len -= bytes;
    b = b->next;
    offset = 0;
  }

  return -1;
}

char *
IOBufferReader::memcpy(void *ap, ink64 len, ink64 offset)
{
  char *p = (char*)ap;
  IOBufferBlock *b = block;
  offset += start_offset;

  while (b && len) {
    ink64 max_bytes = b->read_avail();
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b = b->next;
      continue;
    }
    ink64 bytes;
    if (len<0 || len>= max_bytes)
      bytes = max_bytes;
    else
      bytes = len;
    ::memcpy(p, b->start() + offset, bytes);
    p += bytes;
    len -= bytes;
    b = b->next;
    offset = 0;
  }

  return p;
}
