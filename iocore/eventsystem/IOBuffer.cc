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

/**************************************************************************
  UIOBuffer.cc

**************************************************************************/
#include "tscore/ink_defs.h"
#include "I_MIOBufferWriter.h"
#include "P_EventSystem.h"

//
// General Buffer Allocator
//
inkcoreapi Allocator ioBufAllocator[DEFAULT_BUFFER_SIZES];
inkcoreapi ClassAllocator<MIOBuffer> ioAllocator("ioAllocator", DEFAULT_BUFFER_NUMBER);
inkcoreapi ClassAllocator<IOBufferData> ioDataAllocator("ioDataAllocator", DEFAULT_BUFFER_NUMBER);
inkcoreapi ClassAllocator<IOBufferBlock> ioBlockAllocator("ioBlockAllocator", DEFAULT_BUFFER_NUMBER);
int64_t default_large_iobuffer_size = DEFAULT_LARGE_BUFFER_SIZE;
int64_t default_small_iobuffer_size = DEFAULT_SMALL_BUFFER_SIZE;
int64_t max_iobuffer_size           = DEFAULT_BUFFER_SIZES - 1;

//
// Initialization
//
void
init_buffer_allocators(int iobuffer_advice)
{
  for (int i = 0; i < DEFAULT_BUFFER_SIZES; i++) {
    int64_t s = DEFAULT_BUFFER_BASE_SIZE * ((static_cast<int64_t>(1)) << i);
    int64_t a = DEFAULT_BUFFER_ALIGNMENT;
    int n     = i <= default_large_iobuffer_size ? DEFAULT_BUFFER_NUMBER : DEFAULT_HUGE_BUFFER_NUMBER;
    if (s < a) {
      a = s;
    }

    auto name = new char[64];
    snprintf(name, 64, "ioBufAllocator[%d]", i);
    ioBufAllocator[i].re_init(name, s, n, a, iobuffer_advice);
  }
}

//
// MIOBuffer
//
int64_t
MIOBuffer::remove_append(IOBufferReader *r)
{
  int64_t l = 0;
  while (r->block) {
    Ptr<IOBufferBlock> b = r->block;
    r->block             = r->block->next;
    b->_start += r->start_offset;
    if (b->start() >= b->end()) {
      r->start_offset = -r->start_offset;
      continue;
    }
    r->start_offset = 0;
    l += b->read_avail();
    append_block(b.get());
  }
  r->mbuf->_writer = nullptr;
  return l;
}

int64_t
MIOBuffer::write(const void *abuf, int64_t alen)
{
  const char *buf = static_cast<const char *>(abuf);
  int64_t len     = alen;
  while (len) {
    if (!_writer) {
      add_block();
    }
    int64_t f = _writer->write_avail();
    f         = f < len ? f : len;
    if (f > 0) {
      ::memcpy(_writer->end(), buf, f);
      _writer->fill(f);
      buf += f;
      len -= f;
    }
    if (len) {
      if (!_writer->next) {
        add_block();
      } else {
        _writer = _writer->next;
      }
    }
  }
  return alen;
}

int64_t
MIOBuffer::write(IOBufferReader *r, int64_t len, int64_t offset)
{
  return this->write(r->block.get(), len, offset + r->start_offset);
}

int64_t
MIOBuffer::write(IOBufferChain const *chain, int64_t len, int64_t offset)
{
  return this->write(chain->head(), std::min(len, chain->length()), offset);
}

int64_t
MIOBuffer::write(IOBufferBlock const *b, int64_t alen, int64_t offset)
{
  int64_t len = alen;

  while (b && len > 0) {
    int64_t max_bytes = b->read_avail();
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }
    int64_t bytes;
    if (len < 0 || len >= max_bytes) {
      bytes = max_bytes;
    } else {
      bytes = len;
    }
    IOBufferBlock *bb = b->clone();
    bb->_start += offset;
    bb->_buf_end = bb->_end = bb->_start + bytes;
    append_block(bb);
    offset = 0;
    len -= bytes;
    b = b->next.get();
  }

  return alen - len;
}

int64_t
MIOBuffer::puts(char *s, int64_t len)
{
  char *pc = end();
  char *pb = s;
  while (pc < buf_end()) {
    if (len-- <= 0) {
      return -1;
    }
    if (!*pb || *pb == '\n') {
      int64_t n = static_cast<int64_t>(pb - s);
      memcpy(end(), s, n + 1); // Up to and including '\n'
      end()[n + 1] = 0;
      fill(n + 1);
      return n + 1;
    }
    pc++;
    pb++;
  }
  return 0;
}

//
// IOBufferReader
//
int64_t
IOBufferReader::read(void *ab, int64_t len)
{
  char *b       = static_cast<char *>(ab);
  int64_t n     = len;
  int64_t l     = block_read_avail();
  int64_t bytes = 0;

  while (n && l) {
    if (n < l) {
      l = n;
    }
    ::memcpy(b, start(), l);
    consume(l);
    b += l;
    n -= l;
    bytes += l;
    l = block_read_avail();
  }
  return bytes;
}

// TODO: I don't think this method is used anywhere, so perhaps get rid of it ?
int64_t
IOBufferReader::memchr(char c, int64_t len, int64_t offset)
{
  IOBufferBlock *b = block.get();
  offset += start_offset;
  int64_t o = offset;

  while (b && len) {
    int64_t max_bytes = b->read_avail();
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }
    int64_t bytes;
    if (len < 0 || len >= max_bytes) {
      bytes = max_bytes;
    } else {
      bytes = len;
    }
    char *s = b->start() + offset;
    char *p = static_cast<char *>(::memchr(s, c, bytes));
    if (p) {
      return static_cast<int64_t>(o - start_offset + p - s);
    }
    o += bytes;
    len -= bytes;
    b      = b->next.get();
    offset = 0;
  }

  return -1;
}

char *
IOBufferReader::memcpy(void *ap, int64_t len, int64_t offset)
{
  char *p          = static_cast<char *>(ap);
  IOBufferBlock *b = block.get();
  offset += start_offset;

  while (b && len) {
    int64_t max_bytes = b->read_avail();
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }
    int64_t bytes;
    if (len < 0 || len >= max_bytes) {
      bytes = max_bytes;
    } else {
      bytes = len;
    }
    ::memcpy(p, b->start() + offset, bytes);
    p += bytes;
    len -= bytes;
    b      = b->next.get();
    offset = 0;
  }

  return p;
}

//
// IOBufferChain
//
int64_t
IOBufferChain::write(IOBufferBlock *blocks, int64_t length, int64_t offset)
{
  int64_t n = length;

  while (blocks && n > 0) {
    int64_t block_bytes = blocks->read_avail();
    if (block_bytes <= offset) { // skip the entire block
      offset -= block_bytes;
    } else {
      int64_t bytes     = std::min(n, block_bytes - offset);
      IOBufferBlock *bb = blocks->clone();
      if (offset) {
        bb->consume(offset);
        block_bytes -= offset; // bytes really available to use.
        offset = 0;
      }
      if (block_bytes > n) {
        bb->_end -= (block_bytes - n);
      }
      // Attach the cloned block since its data will be kept.
      this->append(bb);
      n -= bytes;
    }
    blocks = blocks->next.get();
  }

  length -= n; // actual bytes written to chain.
  _len += length;
  return length;
}

int64_t
IOBufferChain::write(IOBufferData *data, int64_t length, int64_t offset)
{
  int64_t zret     = 0;
  IOBufferBlock *b = new_IOBufferBlock();

  if (length < 0) {
    length = 0;
  }

  b->set(data, length, offset);
  this->append(b);

  zret = b->read_avail();
  _len += zret;
  return zret;
}

void
IOBufferChain::append(IOBufferBlock *block)
{
  if (nullptr == _tail) {
    _head = block;
    _tail = block;
  } else {
    _tail->next = block;
    _tail       = block;
  }
}

int64_t
IOBufferChain::consume(int64_t size)
{
  int64_t zret = 0;
  int64_t bytes;
  size = std::min(size, _len);

  while (_head != nullptr && size > 0 && (bytes = _head->read_avail()) > 0) {
    if (size >= bytes) {
      _head = _head->next;
      zret += bytes;
      size -= bytes;
    } else {
      _head->consume(size);
      zret += size;
      size = 0;
    }
  }
  _len -= zret;
  if (_head == nullptr || _len == 0) {
    _head = nullptr, _tail = nullptr, _len = 0;
  }
  return zret;
}

//
// MIOBufferWriter
//
MIOBufferWriter &
MIOBufferWriter::write(const void *data_, size_t length)
{
  const char *data = static_cast<const char *>(data_);

  while (length) {
    IOBufferBlock *iobbPtr = _miob->first_write_block();

    if (!iobbPtr) {
      addBlock();

      iobbPtr = _miob->first_write_block();

      ink_assert(iobbPtr);
    }

    size_t writeSize = iobbPtr->write_avail();

    if (length < writeSize) {
      writeSize = length;
    }

    std::memcpy(iobbPtr->end(), data, writeSize);
    iobbPtr->fill(writeSize);

    data += writeSize;
    length -= writeSize;

    _numWritten += writeSize;
  }

  return *this;
}

std::ostream &
MIOBufferWriter::operator>>(std::ostream &stream) const
{
  IOBufferReader *r = _miob->alloc_reader();
  if (r) {
    IOBufferBlock *b;
    while (nullptr != (b = r->get_current_block())) {
      auto n = b->read_avail();
      stream.write(b->start(), n);
      r->consume(n);
    }
    _miob->dealloc_reader(r);
  }
  return stream;
}

ssize_t
MIOBufferWriter::operator>>(int fd) const
{
  ssize_t zret           = 0;
  IOBufferReader *reader = _miob->alloc_reader();
  if (reader) {
    IOBufferBlock *b;
    while (nullptr != (b = reader->get_current_block())) {
      auto n = b->read_avail();
      auto r = ::write(fd, b->start(), n);
      if (r <= 0) {
        break;
      } else {
        reader->consume(r);
        zret += r;
      }
    }
    _miob->dealloc_reader(reader);
  }
  return zret;
}
