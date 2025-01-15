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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/ink_resource.h"

// TODO: I think we're overly aggressive here on making MIOBuffer 64-bit
// but not sure it's worthwhile changing anything to 32-bit honestly.

//////////////////////////////////////////////////////////////
//
// returns 0 for DEFAULT_BUFFER_BASE_SIZE,
// +1 for each power of 2
//
//////////////////////////////////////////////////////////////
TS_INLINE int64_t
buffer_size_to_index(int64_t size, int64_t max)
{
  int64_t r = max;

  while (r && BUFFER_SIZE_FOR_INDEX(r - 1) >= size) {
    r--;
  }
  return r;
}

TS_INLINE int64_t
iobuffer_size_to_index(int64_t size, int64_t max)
{
  if (size > BUFFER_SIZE_FOR_INDEX(max)) {
    return BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(size);
  }
  return buffer_size_to_index(size, max);
}

TS_INLINE int64_t
index_to_buffer_size(int64_t idx)
{
  if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(idx)) {
    return BUFFER_SIZE_FOR_INDEX(idx);
  } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(idx)) {
    return BUFFER_SIZE_FOR_XMALLOC(idx);
    // coverity[dead_error_condition]
  } else if (BUFFER_SIZE_INDEX_IS_CONSTANT(idx)) {
    return BUFFER_SIZE_FOR_CONSTANT(idx);
  }
  // coverity[dead_error_line]
  return 0;
}

TS_INLINE IOBufferBlock *
iobufferblock_clone(IOBufferBlock *src, int64_t offset, int64_t len)
{
  IOBufferBlock *start_buf   = nullptr;
  IOBufferBlock *current_buf = nullptr;

  while (src && len >= 0) {
    char   *start     = src->_start;
    char   *end       = src->_end;
    int64_t max_bytes = end - start;

    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      src    = src->next.get();
      continue;
    }

    int64_t bytes = len;
    if (bytes >= max_bytes) {
      bytes = max_bytes;
    }

    IOBufferBlock *new_buf  = src->clone();
    new_buf->_start        += offset;
    new_buf->_buf_end = new_buf->_end = new_buf->_start + bytes;

    if (!start_buf) {
      start_buf   = new_buf;
      current_buf = start_buf;
    } else {
      current_buf->next = new_buf;
      current_buf       = new_buf;
    }

    len    -= bytes;
    src     = src->next.get();
    offset  = 0;
  }

  return start_buf;
}

TS_INLINE IOBufferBlock *
iobufferblock_skip(IOBufferBlock *b, int64_t *poffset, int64_t *plen, int64_t write)
{
  int64_t offset = *poffset;
  int64_t len    = write;

  while (b && len >= 0) {
    int64_t max_bytes = b->read_avail();

    // If this block ends before the start offset, skip it
    // and adjust the offset to consume its length.
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }

    if (len >= max_bytes) {
      b       = b->next.get();
      len    -= max_bytes;
      offset  = 0;
    } else {
      offset = offset + len;
      break;
    }
  }

  *poffset  = offset;
  *plen    -= write;
  return b;
}

TS_INLINE void
iobuffer_mem_inc(const char *_loc, int64_t _size_index)
{
  if (!res_track_memory) {
    return;
  }

  if (!BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index)) {
    return;
  }

  if (!_loc) {
    _loc = "memory/IOBuffer/UNKNOWN-LOCATION";
  }
  ResourceTracker::increment(_loc, index_to_buffer_size(_size_index));
}

TS_INLINE void
iobuffer_mem_dec(const char *_loc, int64_t _size_index)
{
  if (!res_track_memory) {
    return;
  }

  if (!BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index)) {
    return;
  }
  if (!_loc) {
    _loc = "memory/IOBuffer/UNKNOWN-LOCATION";
  }
  ResourceTracker::increment(_loc, -index_to_buffer_size(_size_index));
}

//////////////////////////////////////////////////////////////////
//
// inline functions definitions
//
//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//
//  class IOBufferData --
//         inline functions definitions
//
//////////////////////////////////////////////////////////////////
TS_INLINE int64_t
IOBufferData::block_size()
{
  return index_to_buffer_size(_size_index);
}

TS_INLINE IOBufferData *
new_IOBufferData_internal(const char *location, void *b, int64_t size, int64_t asize_index)
{
  (void)size;
  IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
  d->_size_index  = asize_index;
  ink_assert(BUFFER_SIZE_INDEX_IS_CONSTANT(asize_index) || size <= d->block_size());
  d->_location = location;
  d->_data     = (char *)b;
  return d;
}

TS_INLINE IOBufferData *
new_xmalloc_IOBufferData_internal(const char *location, void *b, int64_t size)
{
  return new_IOBufferData_internal(location, b, size, BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(size));
}

TS_INLINE IOBufferData *
new_IOBufferData_internal(const char *loc, int64_t size_index, AllocType type)
{
  IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
  d->_location    = loc;
  d->alloc(size_index, type);
  return d;
}

// IRIX has a compiler bug which prevents this function
// from being compiled correctly at -O3
// so it is DUPLICATED in IOBuffer.cc
// ****** IF YOU CHANGE THIS FUNCTION change that one as well.
TS_INLINE void
IOBufferData::alloc(int64_t size_index, AllocType type)
{
  if (_data) {
    dealloc();
  }
  _size_index = size_index;
  _mem_type   = type;
  iobuffer_mem_inc(_location, size_index);
  switch (type) {
  case MEMALIGNED:
    if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(size_index)) {
      _data = (char *)ioBufAllocator[size_index].alloc_void();
      // coverity[dead_error_condition]
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(size_index)) {
      _data = (char *)ats_memalign(ats_pagesize(), index_to_buffer_size(size_index));
    }
    break;
  default:
  case DEFAULT_ALLOC:
    if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(size_index)) {
      _data = (char *)ioBufAllocator[size_index].alloc_void();
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(size_index)) {
      _data = (char *)ats_malloc(BUFFER_SIZE_FOR_XMALLOC(size_index));
    }
    break;
  }
}

// ****** IF YOU CHANGE THIS FUNCTION change that one as well.

TS_INLINE void
IOBufferData::dealloc()
{
  iobuffer_mem_dec(_location, _size_index);
  switch (_mem_type) {
  case MEMALIGNED:
    if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index)) {
      ioBufAllocator[_size_index].free_void(_data);
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(_size_index)) {
      ::free((void *)_data);
    }
    break;
  default:
  case DEFAULT_ALLOC:
    if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index)) {
      ioBufAllocator[_size_index].free_void(_data);
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(_size_index)) {
      ats_free(_data);
    }
    break;
  }
  _data       = nullptr;
  _size_index = BUFFER_SIZE_NOT_ALLOCATED;
  _mem_type   = NO_ALLOC;
}

TS_INLINE void
IOBufferData::free()
{
  dealloc();
  THREAD_FREE(this, ioDataAllocator, this_thread());
}

//////////////////////////////////////////////////////////////////
//
//  class IOBufferBlock --
//         inline functions definitions
//
//////////////////////////////////////////////////////////////////
TS_INLINE IOBufferBlock *
new_IOBufferBlock_internal(const char *location)
{
  IOBufferBlock *b = THREAD_ALLOC(ioBlockAllocator, this_thread());
  b->_location     = location;
  return b;
}

TS_INLINE IOBufferBlock *
new_IOBufferBlock_internal(const char *location, IOBufferData *d, int64_t len, int64_t offset)
{
  IOBufferBlock *b = THREAD_ALLOC(ioBlockAllocator, this_thread());
  b->_location     = location;
  b->set(d, len, offset);
  return b;
}

TS_INLINE
IOBufferBlock::IOBufferBlock()
{
  return;
}

TS_INLINE void
IOBufferBlock::consume(int64_t len)
{
  _start += len;
  ink_assert(_start <= _end);
}

TS_INLINE void
IOBufferBlock::fill(int64_t len)
{
  _end += len;
  ink_assert(_end <= _buf_end);
}

TS_INLINE void
IOBufferBlock::reset()
{
  _end = _start = buf();
  _buf_end      = buf() + data->block_size();
}

TS_INLINE void
IOBufferBlock::alloc(int64_t i)
{
  ink_assert(BUFFER_SIZE_ALLOCATED(i));
  data = new_IOBufferData_internal(_location, i);
  reset();
}

TS_INLINE void
IOBufferBlock::clear()
{
  data = nullptr;

  IOBufferBlock *p = next.get();
  while (p) {
    // If our block pointer refcount dropped to zero,
    // recursively free the list.
    if (p->refcount_dec() == 0) {
      IOBufferBlock *n = p->next.detach();
      p->free();
      p = n;
    } else {
      // We don't hold the last refcount, so we are done.
      break;
    }
  }

  // Nuke the next pointer without dropping the refcount
  // because we already manually did that.
  next.detach();

  _buf_end = _end = _start = nullptr;
}

TS_INLINE IOBufferBlock *
IOBufferBlock::clone() const
{
  IOBufferBlock *b = new_IOBufferBlock_internal(_location);
  b->data          = data;
  b->_start        = _start;
  b->_end          = _end;
  b->_buf_end      = _end;
  b->_location     = _location;
  return b;
}

TS_INLINE void
IOBufferBlock::dealloc()
{
  clear();
}

TS_INLINE void
IOBufferBlock::free()
{
  dealloc();
  THREAD_FREE(this, ioBlockAllocator, this_thread());
}

TS_INLINE void
IOBufferBlock::set_internal(void *b, int64_t len, int64_t asize_index)
{
  data        = new_IOBufferData_internal(_location, BUFFER_SIZE_NOT_ALLOCATED);
  data->_data = (char *)b;
  iobuffer_mem_inc(_location, asize_index);
  data->_size_index = asize_index;
  reset();
  _end = _start + len;
}

TS_INLINE void
IOBufferBlock::set(IOBufferData *d, int64_t len, int64_t offset)
{
  data     = d;
  _start   = buf() + offset;
  _end     = _start + len;
  _buf_end = buf() + d->block_size();
}

//////////////////////////////////////////////////////////////////
//
//  class IOBufferReader --
//         inline functions definitions
//
//////////////////////////////////////////////////////////////////
TS_INLINE void
IOBufferReader::skip_empty_blocks()
{
  while (block->next && block->next->read_avail() && start_offset >= block->size()) {
    start_offset -= block->size();
    block         = block->next;
  }
}

TS_INLINE bool
IOBufferReader::low_water()
{
  return mbuf->low_water();
}

TS_INLINE bool
IOBufferReader::high_water()
{
  return read_avail() >= mbuf->water_mark;
}

TS_INLINE bool
IOBufferReader::current_low_water()
{
  return mbuf->current_low_water();
}

TS_INLINE IOBufferBlock *
IOBufferReader::get_current_block()
{
  return block.get();
}

TS_INLINE char *
IOBufferReader::start()
{
  if (!block) {
    return nullptr;
  }

  skip_empty_blocks();
  return block->start() + start_offset;
}

TS_INLINE char *
IOBufferReader::end()
{
  if (!block) {
    return nullptr;
  }

  skip_empty_blocks();
  return block->end();
}

TS_INLINE int64_t
IOBufferReader::block_read_avail()
{
  if (!block) {
    return 0;
  }

  skip_empty_blocks();
  return (int64_t)(block->end() - (block->start() + start_offset));
}

inline std::string_view
IOBufferReader::block_read_view()
{
  const char *start = this->start(); // empty blocks are skipped in here.
  return start ? std::string_view{start, static_cast<size_t>(block->end() - start)} : std::string_view{};
}

TS_INLINE int
IOBufferReader::block_count()
{
  int            count = 0;
  IOBufferBlock *b     = block.get();

  while (b) {
    count++;
    b = b->next.get();
  }

  return count;
}

TS_INLINE int64_t
IOBufferReader::read_avail()
{
  int64_t        t = 0;
  IOBufferBlock *b = block.get();

  while (b) {
    t += b->read_avail();
    b  = b->next.get();
  }

  t -= start_offset;
  if (size_limit != INT64_MAX && t > size_limit) {
    t = size_limit;
  }

  return t;
}

TS_INLINE bool
IOBufferReader::is_read_avail_more_than(int64_t size)
{
  int64_t        t = -start_offset;
  IOBufferBlock *b = block.get();

  while (b) {
    t += b->read_avail();
    if (t > size) {
      return true;
    }
    b = b->next.get();
  }
  return false;
}

TS_INLINE void
IOBufferReader::consume(int64_t n)
{
  ink_assert(read_avail() >= n);
  start_offset += n;
  if (size_limit != INT64_MAX) {
    size_limit -= n;
  }

  ink_assert(size_limit >= 0);
  if (!block) {
    return;
  }

  int64_t r = block->read_avail();
  int64_t s = start_offset;
  while (r <= s && block->next && block->next->read_avail()) {
    s            -= r;
    start_offset  = s;
    block         = block->next;
    r             = block->read_avail();
  }
}

TS_INLINE char &
IOBufferReader::operator[](int64_t i)
{
  static char    default_ret = '\0'; // This is just to avoid compiler warnings...
  IOBufferBlock *b           = block.get();

  i += start_offset;
  while (b) {
    int64_t bytes = b->read_avail();
    if (bytes > i) {
      return b->start()[i];
    }
    i -= bytes;
    b  = b->next.get();
  }

  ink_release_assert(!"out of range");
  // Never used, just to satisfy compilers not understanding the fatality of ink_release_assert().
  return default_ret;
}

TS_INLINE void
IOBufferReader::clear()
{
  accessor     = nullptr;
  block        = nullptr;
  mbuf         = nullptr;
  start_offset = 0;
  size_limit   = INT64_MAX;
}

TS_INLINE void
IOBufferReader::reset()
{
  block        = mbuf->_writer;
  start_offset = 0;
  size_limit   = INT64_MAX;
}

////////////////////////////////////////////////////////////////
//
//  class MIOBuffer --
//      inline functions definitions
//
////////////////////////////////////////////////////////////////
extern ClassAllocator<MIOBuffer> ioAllocator;

TS_INLINE
MIOBuffer::MIOBuffer(int64_t default_size_index)
{
  clear();
  size_index = default_size_index;
  _location  = nullptr;
  return;
}

TS_INLINE
MIOBuffer::MIOBuffer()
{
  clear();
  _location = nullptr;
  return;
}

TS_INLINE
MIOBuffer::~MIOBuffer()
{
  _writer = nullptr;
  dealloc_all_readers();
}

TS_INLINE MIOBuffer *
new_MIOBuffer_internal(const char *location, int64_t size_index)
{
  MIOBuffer *b = THREAD_ALLOC(ioAllocator, this_thread());
  b->_location = location;
  b->alloc(size_index);
  b->water_mark = 0;
  return b;
}

TS_INLINE void
free_MIOBuffer(MIOBuffer *mio)
{
#if TS_USE_LINUX_SPLICE
  // check if mio is PipeIOBuffer using dynamic_cast
  PipeIOBuffer *pipe_mio = dynamic_cast<PipeIOBuffer *>(mio);
  if (pipe_mio) {
    free_PipeIOBuffer(pipe_mio);
    return;
  }
#endif

  mio->_writer = nullptr;
  mio->dealloc_all_readers();
  THREAD_FREE(mio, ioAllocator, this_thread());
}

TS_INLINE MIOBuffer *
new_empty_MIOBuffer_internal(const char *location, int64_t size_index)
{
  MIOBuffer *b  = THREAD_ALLOC(ioAllocator, this_thread());
  b->size_index = size_index;
  b->water_mark = 0;
  b->_location  = location;
  return b;
}

TS_INLINE void
free_empty_MIOBuffer(MIOBuffer *mio)
{
  THREAD_FREE(mio, ioAllocator, this_thread());
}

TS_INLINE IOBufferReader *
MIOBuffer::alloc_accessor(MIOBufferAccessor *anAccessor)
{
  int i;
  for (i = 0; i < MAX_MIOBUFFER_READERS; i++) {
    if (!readers[i].allocated()) {
      break;
    }
  }

  // TODO refactor code to return nullptr at some point
  ink_release_assert(i < MAX_MIOBUFFER_READERS);

  IOBufferReader *e = &readers[i];
  e->mbuf           = this;
  e->reset();
  e->accessor = anAccessor;

  return e;
}

TS_INLINE IOBufferReader *
MIOBuffer::alloc_reader()
{
  int i;
  for (i = 0; i < MAX_MIOBUFFER_READERS; i++) {
    if (!readers[i].allocated()) {
      break;
    }
  }

  // TODO refactor code to return nullptr at some point
  ink_release_assert(i < MAX_MIOBUFFER_READERS);

  IOBufferReader *e = &readers[i];
  e->mbuf           = this;
  e->reset();
  e->accessor = nullptr;

  return e;
}

TS_INLINE int64_t
MIOBuffer::block_size()
{
  return index_to_buffer_size(size_index);
}
TS_INLINE IOBufferReader *
MIOBuffer::clone_reader(IOBufferReader *r)
{
  int i;
  for (i = 0; i < MAX_MIOBUFFER_READERS; i++) {
    if (!readers[i].allocated()) {
      break;
    }
  }

  // TODO refactor code to return nullptr at some point
  ink_release_assert(i < MAX_MIOBUFFER_READERS);

  IOBufferReader *e = &readers[i];
  e->mbuf           = this;
  e->accessor       = nullptr;
  e->block          = r->block;
  e->start_offset   = r->start_offset;
  e->size_limit     = r->size_limit;
  ink_assert(e->size_limit >= 0);

  return e;
}

TS_INLINE int64_t
MIOBuffer::block_write_avail()
{
  IOBufferBlock *b = first_write_block();
  return b ? b->write_avail() : 0;
}

////////////////////////////////////////////////////////////////
//
//  MIOBuffer::append_block()
//
//  Appends a block to writer->next and make it the current
//  block.
//  Note that the block is not appended to the end of the list.
//  That means that if writer->next was not null before this
//  call then the block that writer->next was pointing to will
//  have its reference count decremented and writer->next
//  will have a new value which is the new block.
//  In any case the new appended block becomes the current
//  block.
//
////////////////////////////////////////////////////////////////
TS_INLINE void
MIOBuffer::append_block_internal(IOBufferBlock *b)
{
  // It would be nice to remove an empty buffer at the beginning,
  // but this breaks HTTP.
  if (!_writer) {
    _writer = b;
    init_readers();
  } else {
    ink_assert(!_writer->next || !_writer->next->read_avail());
    _writer->next = b;
    while (b->read_avail()) {
      _writer = b;
      b       = b->next.get();
      if (!b) {
        break;
      }
    }
  }
  while (_writer->next && !_writer->write_avail() && _writer->next->read_avail()) {
    _writer = _writer->next;
  }
}

TS_INLINE void
MIOBuffer::append_block(IOBufferBlock *b)
{
  ink_assert(b->read_avail());
  append_block_internal(b);
}

////////////////////////////////////////////////////////////////
//
//  MIOBuffer::append_block()
//
//  Allocate a block, appends it to current->next
//  and make the new block the current block (writer).
//
////////////////////////////////////////////////////////////////
TS_INLINE void
MIOBuffer::append_block(int64_t asize_index)
{
  ink_assert(BUFFER_SIZE_ALLOCATED(asize_index));
  IOBufferBlock *b = new_IOBufferBlock_internal(_location);
  b->alloc(asize_index);
  append_block_internal(b);
  return;
}

TS_INLINE void
MIOBuffer::add_block()
{
  if (this->_writer == nullptr || this->_writer->next == nullptr) {
    append_block(size_index);
  }
}

TS_INLINE void
MIOBuffer::check_add_block()
{
  if (!high_water() && current_low_water()) {
    add_block();
  }
}

TS_INLINE IOBufferBlock *
MIOBuffer::get_current_block()
{
  return first_write_block();
}

//////////////////////////////////////////////////////////////////
//
//  MIOBuffer::current_write_avail()
//
//  returns the total space available in all blocks.
//  This function is different than write_avail() because
//  it will not append a new block if there is no space
//  or below the watermark space available.
//
//////////////////////////////////////////////////////////////////
TS_INLINE int64_t
MIOBuffer::current_write_avail()
{
  int64_t        t = 0;
  IOBufferBlock *b = _writer.get();
  while (b) {
    t += b->write_avail();
    b  = b->next.get();
  }
  return t;
}

//////////////////////////////////////////////////////////////////
//
//  MIOBuffer::write_avail()
//
//  returns the number of bytes available in the current block.
//  If there is no current block or not enough free space in
//  the current block then a new block is appended.
//
//////////////////////////////////////////////////////////////////
TS_INLINE int64_t
MIOBuffer::write_avail()
{
  check_add_block();
  return current_write_avail();
}

TS_INLINE void
MIOBuffer::fill(int64_t len)
{
  int64_t f = _writer->write_avail();
  while (f < len) {
    _writer->fill(f);
    len -= f;
    if (len > 0) {
      _writer = _writer->next;
    }
    f = _writer->write_avail();
  }
  _writer->fill(len);
}

TS_INLINE int
MIOBuffer::max_block_count()
{
  int maxb = 0;
  for (auto &reader : readers) {
    if (reader.allocated()) {
      int c = reader.block_count();
      if (c > maxb) {
        maxb = c;
      }
    }
  }
  return maxb;
}

TS_INLINE int64_t
MIOBuffer::max_read_avail()
{
  int64_t s     = 0;
  int     found = 0;
  for (auto &reader : readers) {
    if (reader.allocated()) {
      int64_t ss = reader.read_avail();
      if (ss > s) {
        s = ss;
      }
      found = 1;
    }
  }
  if (!found && _writer) {
    return _writer->read_avail();
  }
  return s;
}

TS_INLINE void
MIOBuffer::set(void *b, int64_t len)
{
  _writer = new_IOBufferBlock_internal(_location);
  _writer->set_internal(b, len, BUFFER_SIZE_INDEX_FOR_CONSTANT_SIZE(len));
  init_readers();
}

TS_INLINE void
MIOBuffer::append_xmalloced(void *b, int64_t len)
{
  IOBufferBlock *x = new_IOBufferBlock_internal(_location);
  x->set_internal(b, len, BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(len));
  append_block_internal(x);
}

TS_INLINE void
MIOBuffer::append_fast_allocated(void *b, int64_t len, int64_t fast_size_index)
{
  IOBufferBlock *x = new_IOBufferBlock_internal(_location);
  x->set_internal(b, len, fast_size_index);
  append_block_internal(x);
}

TS_INLINE void
MIOBuffer::alloc(int64_t i)
{
  _writer = new_IOBufferBlock_internal(_location);
  _writer->alloc(i);
  size_index = i;
  init_readers();
}

TS_INLINE void
MIOBuffer::dealloc_reader(IOBufferReader *e)
{
  if (e->accessor) {
    ink_assert(e->accessor->writer() == this);
    ink_assert(e->accessor->reader() == e);
    e->accessor->clear();
  }
  e->clear();
}

TS_INLINE IOBufferReader *
IOBufferReader::clone()
{
  return mbuf->clone_reader(this);
}

TS_INLINE void
IOBufferReader::dealloc()
{
  mbuf->dealloc_reader(this);
}

TS_INLINE void
MIOBuffer::dealloc_all_readers()
{
  for (auto &reader : readers) {
    if (reader.allocated()) {
      dealloc_reader(&reader);
    }
  }
}

TS_INLINE void
MIOBufferAccessor::reader_for(MIOBuffer *abuf)
{
  mbuf = abuf;
  if (abuf) {
    entry = mbuf->alloc_accessor(this);
  } else {
    entry = nullptr;
  }
}

TS_INLINE void
MIOBufferAccessor::reader_for(IOBufferReader *areader)
{
  if (entry == areader) {
    return;
  }
  mbuf  = areader->mbuf;
  entry = areader;
  ink_assert(mbuf);
}

TS_INLINE void
MIOBufferAccessor::writer_for(MIOBuffer *abuf)
{
  mbuf  = abuf;
  entry = nullptr;
}

TS_INLINE
MIOBufferAccessor::~MIOBufferAccessor() {}

#if TS_USE_LINUX_SPLICE
extern ClassAllocator<PipeIOBuffer> pipeIOAllocator;

TS_INLINE PipeIOBuffer *
new_PipeIOBuffer_internal(const char *location, int64_t pipe_capacity)
{
  PipeIOBuffer *b = THREAD_ALLOC(pipeIOAllocator, this_thread());
  b->_location    = location;
  b->alloc(pipe_capacity);
  b->water_mark = 0;
  return b;
}

TS_INLINE void
free_PipeIOBuffer(PipeIOBuffer *mio)
{
  // static cast to PipeIOBuffer
  PipeIOBuffer *b = static_cast<PipeIOBuffer *>(mio);
  b->clear();
  THREAD_FREE(b, pipeIOAllocator, this_thread());
}

//////////////////////////////////////////////////////////////////
//
//  class PipeIOBufferReader --
//         inline functions definitions
//
//////////////////////////////////////////////////////////////////
TS_INLINE char *
PipeIOBufferReader::start()
{
  throw std::runtime_error("Not applicable for PipeIOBufferReader");
}

TS_INLINE char *
PipeIOBufferReader::end()
{
  throw std::runtime_error("Not applicable for PipeIOBufferReader");
}

TS_INLINE int64_t
PipeIOBufferReader::read_avail()
{
  return static_cast<PipeIOBuffer *>(mbuf)->data_in_pipe;
}

TS_INLINE bool
PipeIOBufferReader::is_read_avail_more_than(int64_t size)
{
  return read_avail() > size;
}

TS_INLINE int
PipeIOBufferReader::block_count()
{
  return 1;
}

TS_INLINE int64_t
PipeIOBufferReader::block_read_avail()
{
  return read_avail();
}

TS_INLINE std::string_view
          PipeIOBufferReader::block_read_view()
{
  throw std::runtime_error("Not applicable for PipeIOBufferReader");
}

TS_INLINE void
PipeIOBufferReader::skip_empty_blocks()
{
  // No-op for PipeIOBufferReader
}

TS_INLINE void
PipeIOBufferReader::clear()
{
  // call base class clear
  IOBufferReader::clear();
}

TS_INLINE void
PipeIOBufferReader::reset()
{
  // call base class reset
  IOBufferReader::reset();
}

TS_INLINE void
PipeIOBufferReader::consume(int64_t n)
{
  auto *pipe_buf = static_cast<PipeIOBuffer *>(mbuf);
  pipe_buf->consume(n);

  char    buffer[n];
  ssize_t bytes_read = ::read(pipe_buf->fd[0], buffer, n);
  if (bytes_read < 0) {
    throw std::runtime_error("Pipe read failed during consume");
  }
}

TS_INLINE IOBufferReader *
PipeIOBufferReader::clone()
{
  throw std::runtime_error("Cloning not supported for PipeIOBufferReader");
}

TS_INLINE void
PipeIOBufferReader::dealloc()
{
  static_cast<PipeIOBuffer *>(mbuf)->dealloc_reader(this);
}

TS_INLINE IOBufferBlock *
PipeIOBufferReader::get_current_block()
{
  throw std::runtime_error("Not applicable for PipeIOBufferReader");
}

TS_INLINE bool
PipeIOBufferReader::current_low_water()
{
  return static_cast<PipeIOBuffer *>(mbuf)->current_low_water();
}

TS_INLINE bool
PipeIOBufferReader::low_water()
{
  return static_cast<PipeIOBuffer *>(mbuf)->low_water();
}

TS_INLINE bool
PipeIOBufferReader::high_water()
{
  return static_cast<PipeIOBuffer *>(mbuf)->high_water();
}

TS_INLINE int64_t
PipeIOBufferReader::memchr([[maybe_unused]] char c, [[maybe_unused]] int64_t len, [[maybe_unused]] int64_t offset)
{
  throw std::runtime_error("Not supported in PipeIOBufferReader");
}

// Read data from the pipe into the buffer.
TS_INLINE int64_t
PipeIOBufferReader::read(void *buf, int64_t len)
{
  auto   *pipe_buf      = static_cast<PipeIOBuffer *>(mbuf);
  int64_t bytes_to_read = std::min(len, pipe_buf->data_in_pipe);
  ssize_t bytes_read    = ::read(pipe_buf->fd[0], buf, bytes_to_read);
  if (bytes_read < 0) {
    throw std::runtime_error("Pipe read failed");
  }
  pipe_buf->consume(bytes_read);
  return bytes_read;
}

TS_INLINE char *
PipeIOBufferReader::memcpy([[maybe_unused]] void *buf, [[maybe_unused]] int64_t len, [[maybe_unused]] int64_t offset)
{
  throw std::runtime_error("Not supported in PipeIOBufferReader");
}

TS_INLINE char &
PipeIOBufferReader::operator[]([[maybe_unused]] int64_t i)
{
  throw std::runtime_error("Not supported in PipeIOBufferReader");
}

//////////////////////////////////////////////////////////////////
//
//  class PipeIOBuffer --
//         inline functions definitions
//
//////////////////////////////////////////////////////////////////
TS_INLINE
PipeIOBuffer::PipeIOBuffer() : fd{-1, -1}, reader_allocated(false), data_in_pipe(0), pipe_capacity(0)
{
  // no ops due to ClassAllocator design
}

TS_INLINE PipeIOBuffer::~PipeIOBuffer()
{
  // no ops due to ClassAllocator design
}

TS_INLINE void
PipeIOBuffer::fill(int64_t len)
{
  if (len + data_in_pipe > pipe_capacity) {
    throw std::runtime_error("Not enough space in pipe to fill");
  }
  data_in_pipe += len;
}

TS_INLINE void
PipeIOBuffer::consume(int64_t len)
{
  if (len > data_in_pipe) {
    throw std::runtime_error("Attempt to consume more data than available in pipe");
  }
  data_in_pipe -= len;
}

TS_INLINE void
PipeIOBuffer::append_block([[maybe_unused]] IOBufferBlock *b)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE void
PipeIOBuffer::append_block([[maybe_unused]] int64_t asize_index)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE void
PipeIOBuffer::add_block()
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE void
PipeIOBuffer::append_xmalloced([[maybe_unused]] void *b, [[maybe_unused]] int64_t len)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE void
PipeIOBuffer::append_fast_allocated([[maybe_unused]] void *b, [[maybe_unused]] int64_t len,
                                    [[maybe_unused]] int64_t fast_size_index)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE int64_t
PipeIOBuffer::write(const void *buf, int64_t nbytes)
{
  if (nbytes > write_avail()) {
    throw std::runtime_error("Not enough space in pipe to write");
  }
  ssize_t written = ::write(fd[1], buf, nbytes);
  if (written < 0) {
    throw std::runtime_error("Pipe write failed");
  }
  fill(written); // Update data_in_pipe to reflect the amount written
  return written;
}

TS_INLINE int64_t
PipeIOBuffer::write([[maybe_unused]] IOBufferReader *r, [[maybe_unused]] int64_t len, [[maybe_unused]] int64_t offset)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE int64_t
PipeIOBuffer::write([[maybe_unused]] IOBufferChain const *chain, [[maybe_unused]] int64_t len, [[maybe_unused]] int64_t offset)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE IOBufferBlock *
PipeIOBuffer::first_write_block()
{
  throw std::runtime_error("Not applicable for PipeIOBuffer");
}

TS_INLINE char *
PipeIOBuffer::buf()
{
  throw std::runtime_error("Not applicable for PipeIOBuffer");
}

TS_INLINE char *
PipeIOBuffer::buf_end()
{
  throw std::runtime_error("Not applicable for PipeIOBuffer");
}

TS_INLINE char *
PipeIOBuffer::start()
{
  throw std::runtime_error("Not applicable for PipeIOBuffer");
}

TS_INLINE char *
PipeIOBuffer::end()
{
  throw std::runtime_error("Not applicable for PipeIOBuffer");
}

TS_INLINE int64_t
PipeIOBuffer::block_write_avail()
{
  return write_avail();
}

TS_INLINE int64_t
PipeIOBuffer::current_write_avail()
{
  return write_avail();
}

TS_INLINE int64_t
PipeIOBuffer::write_avail()
{
  return pipe_capacity - data_in_pipe;
}

TS_INLINE int64_t
PipeIOBuffer::block_size()
{
  return pipe_capacity;
}

TS_INLINE bool
PipeIOBuffer::high_water()
{
  return is_max_read_avail_more_than(this->water_mark);
}

TS_INLINE bool
PipeIOBuffer::low_water()
{
  return write_avail() <= water_mark;
}

TS_INLINE bool
PipeIOBuffer::current_low_water()
{
  return low_water();
}

// Allocate a reader for the PipeIOBuffer with the given MIOBufferAccessor
TS_INLINE IOBufferReader *
PipeIOBuffer::alloc_accessor(MIOBufferAccessor *anAccessor)
{
  if (reader_allocated) {
    throw std::runtime_error("PipeIOBuffer supports only a single reader");
  }
  pipe_reader.mbuf     = this;
  pipe_reader.accessor = anAccessor;
  reader_allocated     = true;
  return &pipe_reader;
}

// Allocate a reader for the PipeIOBuffer but without an MIOBufferAccessor
TS_INLINE IOBufferReader *
PipeIOBuffer::alloc_reader()
{
  if (reader_allocated) {
    throw std::runtime_error("PipeIOBuffer supports only a single reader");
  }
  Dbg(DbgCtl("http_tunnel"), "PipeIOBuffer::alloc_reader() called");
  pipe_reader.mbuf     = this;
  pipe_reader.accessor = nullptr;
  reader_allocated     = true;
  return &pipe_reader;
}

TS_INLINE IOBufferReader *
PipeIOBuffer::clone_reader([[maybe_unused]] IOBufferReader *r)
{
  // PipeIOBuffer supports only a single reader, so cloning is not supported, but return the existing reader
  return &pipe_reader;
}

TS_INLINE void
PipeIOBuffer::dealloc_reader(IOBufferReader *e)
{
  if (&pipe_reader == e) {
    // clear the accessor if it is set but keep the accessor pointer
    if (pipe_reader.accessor) {
      ink_assert(pipe_reader.accessor->writer() == this);
      ink_assert(pipe_reader.accessor->reader() == e);
      pipe_reader.accessor->clear();
    }
    pipe_reader.clear();
    reader_allocated = false;
  } else {
    throw std::runtime_error("Attempt to deallocate a non-existing reader");
  }
}

TS_INLINE void
PipeIOBuffer::set([[maybe_unused]] void *b, [[maybe_unused]] int64_t len)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE void
PipeIOBuffer::alloc(int64_t pipe_capacity)
{
  // The default pipe capacity is 64KB equivalent to 16 pages on x86_64
  if (pipe2(fd, O_NONBLOCK) < 0) {
    throw std::runtime_error("Pipe creation failed");
  }
  this->pipe_capacity = pipe_capacity;

  // Set the pipe capacity to the requested value if it is not the default value which is 16 pages
  if (pipe_capacity != 16 * getpagesize()) {
    // https://man7.org/linux/man-pages/man2/fcntl.2.html#:~:text=Changing%20the%20capacity%20of%20a%20pipe
    // When allocating the buffer for the pipe, the kernel may use a capacity larger than arg, if that is convenient for
    // the implementation.  (In the current implementation, the allocation is the next higher power-of-two page-size
    // multiple of the requested size.)
    if (fcntl(fd[1], F_SETPIPE_SZ, pipe_capacity) < 0) {
      close(fd[0]);
      close(fd[1]);
      throw std::runtime_error("Pipe capacity setting failed");
    }
  }
}

TS_INLINE void
PipeIOBuffer::append_block_internal([[maybe_unused]] IOBufferBlock *b)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE int64_t
PipeIOBuffer::write([[maybe_unused]] IOBufferBlock const *b, [[maybe_unused]] int64_t len, [[maybe_unused]] int64_t offset)
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE int64_t
PipeIOBuffer::max_read_avail()
{
  return data_in_pipe;
}

TS_INLINE bool
PipeIOBuffer::is_max_read_avail_more_than(int64_t size)
{
  return data_in_pipe > size;
}

// PipeIOBuffer has only one block, which is the pipe itself, so block count is always 1
TS_INLINE int
PipeIOBuffer::max_block_count()
{
  return 1;
}

TS_INLINE void
PipeIOBuffer::check_add_block()
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

TS_INLINE void
PipeIOBuffer::reset()
{
  // clear internal state and release external resources
  clear();

  // call alloc to reinitialize the pipe
  alloc(pipe_capacity);
}

TS_INLINE void
PipeIOBuffer::init_readers()
{
  throw std::runtime_error("Not supported in PipeIOBuffer");
}

// Release the external resources associated with the PipeIOBuffer
TS_INLINE void
PipeIOBuffer::dealloc()
{
  // close the pipe if it is open
  if (fd[0] != -1) {
    close(fd[0]);
  }
  if (fd[1] != -1) {
    close(fd[1]);
  }
  // set the file descriptors to -1
  fd[0] = -1;
  fd[1] = -1;
  dealloc_reader(&pipe_reader);
}

// Clear internal state and call dealloc to release external resources
TS_INLINE void
PipeIOBuffer::clear()
{
  data_in_pipe  = 0;
  pipe_capacity = 0;
  dealloc();
}

#endif // TS_USE_LINUX_SPLICE
