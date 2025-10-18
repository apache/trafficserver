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
#include "iocore/eventsystem/Thread.h"
#include "tscore/Allocator.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "swoc/Lexicon.h"
#include "tscore/Diags.h"
#include "tscore/ink_memory.h"

#include <optional>

// TODO: I think we're overly aggressive here on making MIOBuffer 64-bit
// but not sure it's worthwhile changing anything to 32-bit honestly.

IOBufferBlock *
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

IOBufferBlock *
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

void
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

void
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
int64_t
IOBufferData::block_size()
{
  return index_to_buffer_size(_size_index);
}

IOBufferData *
new_IOBufferData_internal(const char *location, void *b, int64_t size, int64_t asize_index)
{
  (void)size;
  IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
  d->_size_index  = asize_index;
  ink_assert(BUFFER_SIZE_INDEX_IS_CONSTANT(asize_index) || size <= d->block_size());
  d->_location = location;
  d->_data     = static_cast<char *>(b);
  return d;
}

IOBufferData *
new_xmalloc_IOBufferData_internal(const char *location, void *b, int64_t size)
{
  return new_IOBufferData_internal(location, b, size, BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(size));
}

IOBufferData *
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
void
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
      _data = static_cast<char *>(ioBufAllocator[size_index].alloc_void());
      // coverity[dead_error_condition]
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(size_index)) {
      _data = static_cast<char *>(ats_memalign(ats_pagesize(), index_to_buffer_size(size_index)));
    }
    break;
  default:
  case DEFAULT_ALLOC:
    if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(size_index)) {
      _data = static_cast<char *>(ioBufAllocator[size_index].alloc_void());
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(size_index)) {
      _data = static_cast<char *>(ats_malloc(BUFFER_SIZE_FOR_XMALLOC(size_index)));
    }
    break;
  }
}

// ****** IF YOU CHANGE THIS FUNCTION change that one as well.

void
IOBufferData::dealloc()
{
  iobuffer_mem_dec(_location, _size_index);
  switch (_mem_type) {
  case MEMALIGNED:
    if (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index)) {
      ioBufAllocator[_size_index].free_void(_data);
    } else if (BUFFER_SIZE_INDEX_IS_XMALLOCED(_size_index)) {
      ::free(_data);
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

void
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
IOBufferBlock *
new_IOBufferBlock_internal(const char *location)
{
  IOBufferBlock *b = THREAD_ALLOC(ioBlockAllocator, this_thread());
  b->_location     = location;
  return b;
}

IOBufferBlock *
new_IOBufferBlock_internal(const char *location, IOBufferData *d, int64_t len, int64_t offset)
{
  IOBufferBlock *b = THREAD_ALLOC(ioBlockAllocator, this_thread());
  b->_location     = location;
  b->set(d, len, offset);
  return b;
}

IOBufferBlock::IOBufferBlock()
{
  return;
}

void
IOBufferBlock::consume(int64_t len)
{
  _start += len;
  ink_assert(_start <= _end);
}

void
IOBufferBlock::fill(int64_t len)
{
  _end += len;
  ink_assert(_end <= _buf_end);
}

void
IOBufferBlock::reset()
{
  _end = _start = buf();
  _buf_end      = buf() + data->block_size();
}

void
IOBufferBlock::alloc(int64_t i)
{
  ink_assert(BUFFER_SIZE_ALLOCATED(i));
  data = new_IOBufferData_internal(_location, i);
  reset();
}

void
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

IOBufferBlock *
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

void
IOBufferBlock::dealloc()
{
  clear();
}

void
IOBufferBlock::free()
{
  dealloc();
  THREAD_FREE(this, ioBlockAllocator, this_thread());
}

void
IOBufferBlock::set_internal(void *b, int64_t len, int64_t asize_index)
{
  data        = new_IOBufferData_internal(_location, BUFFER_SIZE_NOT_ALLOCATED);
  data->_data = static_cast<char *>(b);
  iobuffer_mem_inc(_location, asize_index);
  data->_size_index = asize_index;
  reset();
  _end = _start + len;
}

void
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
void
IOBufferReader::skip_empty_blocks()
{
  while (block->next && block->next->read_avail() && start_offset >= block->size()) {
    start_offset -= block->size();
    block         = block->next;
  }
}

bool
IOBufferReader::low_water()
{
  return mbuf->low_water();
}

bool
IOBufferReader::high_water()
{
  return read_avail() >= mbuf->water_mark;
}

bool
IOBufferReader::current_low_water()
{
  return mbuf->current_low_water();
}

IOBufferBlock *
IOBufferReader::get_current_block()
{
  return block.get();
}

char *
IOBufferReader::start()
{
  if (!block) {
    return nullptr;
  }

  skip_empty_blocks();
  return block->start() + start_offset;
}

char *
IOBufferReader::end()
{
  if (!block) {
    return nullptr;
  }

  skip_empty_blocks();
  return block->end();
}

int64_t
IOBufferReader::block_read_avail()
{
  if (!block) {
    return 0;
  }

  skip_empty_blocks();
  return static_cast<int64_t>(block->end() - (block->start() + start_offset));
}

std::string_view
IOBufferReader::block_read_view()
{
  const char *start = this->start(); // empty blocks are skipped in here.
  return start ? std::string_view{start, static_cast<size_t>(block->end() - start)} : std::string_view{};
}

int
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

int64_t
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

bool
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

void
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

char &
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

void
IOBufferReader::clear()
{
  accessor     = nullptr;
  block        = nullptr;
  mbuf         = nullptr;
  start_offset = 0;
  size_limit   = INT64_MAX;
}

void
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

MIOBuffer::MIOBuffer(int64_t default_size_index)
{
  clear();
  size_index = default_size_index;
  _location  = nullptr;
  return;
}

MIOBuffer::MIOBuffer()
{
  clear();
  _location = nullptr;
  return;
}

MIOBuffer::~MIOBuffer()
{
  _writer = nullptr;
  dealloc_all_readers();
}

MIOBuffer *
new_MIOBuffer_internal(const char *location, int64_t size_index)
{
  MIOBuffer *b = THREAD_ALLOC(ioAllocator, this_thread());
  b->_location = location;
  b->alloc(size_index);
  b->water_mark = 0;
  return b;
}

void
free_MIOBuffer(MIOBuffer *mio)
{
  mio->_writer = nullptr;
  mio->dealloc_all_readers();
  THREAD_FREE(mio, ioAllocator, this_thread());
}

MIOBuffer *
new_empty_MIOBuffer_internal(const char *location, int64_t size_index)
{
  MIOBuffer *b  = THREAD_ALLOC(ioAllocator, this_thread());
  b->size_index = size_index;
  b->water_mark = 0;
  b->_location  = location;
  return b;
}

void
free_empty_MIOBuffer(MIOBuffer *mio)
{
  THREAD_FREE(mio, ioAllocator, this_thread());
}

IOBufferReader *
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

IOBufferReader *
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

int64_t
MIOBuffer::block_size()
{
  return index_to_buffer_size(size_index);
}
IOBufferReader *
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

int64_t
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
void
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

void
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
void
MIOBuffer::append_block(int64_t asize_index)
{
  ink_assert(BUFFER_SIZE_ALLOCATED(asize_index));
  IOBufferBlock *b = new_IOBufferBlock_internal(_location);
  b->alloc(asize_index);
  append_block_internal(b);
  return;
}

void
MIOBuffer::add_block()
{
  if (this->_writer == nullptr || this->_writer->next == nullptr) {
    append_block(size_index);
  }
}

void
MIOBuffer::check_add_block()
{
  if (!high_water() && current_low_water()) {
    add_block();
  }
}

IOBufferBlock *
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
int64_t
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
int64_t
MIOBuffer::write_avail()
{
  check_add_block();
  return current_write_avail();
}

void
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

int
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

int64_t
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

void
MIOBuffer::set(void *b, int64_t len)
{
  _writer = new_IOBufferBlock_internal(_location);
  _writer->set_internal(b, len, BUFFER_SIZE_INDEX_FOR_CONSTANT_SIZE(len));
  init_readers();
}

void
MIOBuffer::append_xmalloced(void *b, int64_t len)
{
  if (len == 0) {
    return;
  }

  IOBufferBlock *x = new_IOBufferBlock_internal(_location);
  x->set_internal(b, len, BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(len));
  append_block_internal(x);
}

void
MIOBuffer::append_fast_allocated(void *b, int64_t len, int64_t fast_size_index)
{
  IOBufferBlock *x = new_IOBufferBlock_internal(_location);
  x->set_internal(b, len, fast_size_index);
  append_block_internal(x);
}

void
MIOBuffer::alloc(int64_t i)
{
  _writer = new_IOBufferBlock_internal(_location);
  _writer->alloc(i);
  size_index = i;
  init_readers();
}

void
MIOBuffer::dealloc_reader(IOBufferReader *e)
{
  if (e->accessor) {
    ink_assert(e->accessor->writer() == this);
    ink_assert(e->accessor->reader() == e);
    e->accessor->clear();
  }
  e->clear();
}

IOBufferReader *
IOBufferReader::clone()
{
  return mbuf->clone_reader(this);
}

void
IOBufferReader::dealloc()
{
  mbuf->dealloc_reader(this);
}

void
MIOBuffer::dealloc_all_readers()
{
  for (auto &reader : readers) {
    if (reader.allocated()) {
      dealloc_reader(&reader);
    }
  }
}

void
MIOBufferAccessor::reader_for(MIOBuffer *abuf)
{
  mbuf = abuf;
  if (abuf) {
    entry = mbuf->alloc_accessor(this);
  } else {
    entry = nullptr;
  }
}

void
MIOBufferAccessor::reader_for(IOBufferReader *areader)
{
  if (entry == areader) {
    return;
  }
  mbuf  = areader->mbuf;
  entry = areader;
  ink_assert(mbuf);
}

void
MIOBufferAccessor::writer_for(MIOBuffer *abuf)
{
  mbuf  = abuf;
  entry = nullptr;
}

MIOBufferAccessor::~MIOBufferAccessor() {}

//
// General Buffer Allocator
//
#if TS_USE_ALLOCATOR_METRICS
MeteredAllocator<FreelistAllocator> ioBufAllocator[DEFAULT_BUFFER_SIZES];
#else
FreelistAllocator ioBufAllocator[DEFAULT_BUFFER_SIZES];
#endif
ClassAllocator<MIOBuffer>     ioAllocator("ioAllocator", DEFAULT_BUFFER_NUMBER);
ClassAllocator<IOBufferData>  ioDataAllocator("ioDataAllocator", DEFAULT_BUFFER_NUMBER);
ClassAllocator<IOBufferBlock> ioBlockAllocator("ioBlockAllocator", DEFAULT_BUFFER_NUMBER);
int64_t                       default_large_iobuffer_size = DEFAULT_LARGE_BUFFER_SIZE;
int64_t                       default_small_iobuffer_size = DEFAULT_SMALL_BUFFER_SIZE;
int64_t                       max_iobuffer_size           = DEFAULT_BUFFER_SIZES - 1;

//
// Initialization
//
void
init_buffer_allocators(int iobuffer_advice, int chunk_sizes[DEFAULT_BUFFER_SIZES], bool use_hugepages)
{
  for (int i = 0; i < DEFAULT_BUFFER_SIZES; i++) {
    int64_t s = DEFAULT_BUFFER_BASE_SIZE * ((static_cast<int64_t>(1)) << i);
    int64_t a = DEFAULT_BUFFER_ALIGNMENT;
    int     n = chunk_sizes[i];
    if (n == 0) {
      n = i <= default_large_iobuffer_size ? DEFAULT_BUFFER_NUMBER : DEFAULT_HUGE_BUFFER_NUMBER;
    }
    if (s < a) {
      a = s;
    }

    auto name = new char[64];
    if (use_hugepages) {
      snprintf(name, 64, "ioBufAllocatorHP[%d]", i);
    } else {
      snprintf(name, 64, "ioBufAllocator[%d]", i);
    }
    ioBufAllocator[i].re_init(name, s, n, a, use_hugepages, iobuffer_advice);
  }
}

void
init_buffer_allocators(int iobuffer_advice)
{
  int chunk_sizes[DEFAULT_BUFFER_SIZES] = {0};
  init_buffer_allocators(iobuffer_advice, chunk_sizes, false);
}

auto
make_buffer_size_parser()
{
  using L = swoc::Lexicon<int>;
  return [l = L{
            L::with_multi{{0, {"128"}},
                          {1, {"256"}},
                          {2, {"512"}},
                          {3, {"1k", "1024"}},
                          {4, {"2k", "2048"}},
                          {5, {"4k", "4096"}},
                          {6, {"8k", "8192"}},
                          {7, {"16k"}},
                          {8, {"32k"}},
                          {9, {"64k"}},
                          {10, {"128k"}},
                          {11, {"256k"}},
                          {12, {"512k"}},
                          {13, {"1M", "1024k"}},
                          {14, {"2M", "2048k"}}},
            -1
  }](swoc::TextView esize) -> std::optional<int> {
    int result = l[esize];
    if (result == -1) {
      return std::nullopt;
    }
    return result;
  };
}

bool
parse_buffer_chunk_sizes(const char *chunk_sizes_string, int chunk_sizes[DEFAULT_BUFFER_SIZES])
{
  const swoc::TextView delimiters(", ");
  if (chunk_sizes_string != nullptr) {
    swoc::TextView src(chunk_sizes_string, swoc::TextView::npos);
    auto           parser = make_buffer_size_parser();
    int            n      = 0;
    while (!src.empty()) {
      swoc::TextView token{src.take_prefix_at(delimiters)};

      swoc::TextView esize = token.take_prefix_at(':');
      if (!token.empty()) {
        auto parsed = parser(esize);
        if (parsed) {
          n = *parsed;
        } else {
          Error("Failed to parse size for %.*s", static_cast<int>(esize.size()), esize.data());
          return false;
        }
      } else {
        // element didn't have a colon so its just a chunk size
        token = esize;
      }

      // Check if n goes out of bounds
      if (n >= DEFAULT_BUFFER_SIZES) {
        Error("Invalid IO buffer chunk sizes string");
        return false;
      }

      auto x = swoc::svto_radix<10>(token);
      if (token.empty() && x != std::numeric_limits<decltype(x)>::max()) {
        chunk_sizes[n++] = x;
      } else {
        Error("Failed to parse chunk size");
        return false;
      }

      src.ltrim(delimiters);
    }
  }
  return true;
}

//
// MIOBuffer
//
int64_t
MIOBuffer::write(const void *abuf, int64_t alen)
{
  const char *buf = static_cast<const char *>(abuf);
  int64_t     len = alen;
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
    int64_t max_bytes  = b->read_avail();
    max_bytes         -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }
    int64_t bytes;
    if (len >= max_bytes) {
      bytes = max_bytes;
    } else {
      bytes = len;
    }
    IOBufferBlock *bb  = b->clone();
    bb->_start        += offset;
    bb->_buf_end = bb->_end = bb->_start + bytes;
    append_block(bb);
    offset  = 0;
    len    -= bytes;
    b       = b->next.get();
  }

  return alen - len;
}

bool
MIOBuffer::is_max_read_avail_more_than(int64_t size)
{
  bool no_reader = true;
  for (auto &reader : this->readers) {
    if (reader.allocated()) {
      if (reader.is_read_avail_more_than(size)) {
        return true;
      }
      no_reader = false;
    }
  }

  if (no_reader && this->_writer) {
    return (this->_writer->read_avail() > size);
  }

  return false;
}

//
// IOBufferReader
//
int64_t
IOBufferReader::read(void *ab, int64_t len)
{
  char   *b     = static_cast<char *>(ab);
  int64_t n     = len;
  int64_t l     = block_read_avail();
  int64_t bytes = 0;

  while (n && l) {
    if (n < l) {
      l = n;
    }
    ::memcpy(b, start(), l);
    consume(l);
    b     += l;
    n     -= l;
    bytes += l;
    l      = block_read_avail();
  }
  return bytes;
}

// TODO: I don't think this method is used anywhere, so perhaps get rid of it ?
int64_t
IOBufferReader::memchr(char c, int64_t len, int64_t offset)
{
  IOBufferBlock *b  = block.get();
  offset           += start_offset;
  int64_t o         = offset;

  while (b && len) {
    int64_t max_bytes  = b->read_avail();
    max_bytes         -= offset;
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
    o      += bytes;
    len    -= bytes;
    b       = b->next.get();
    offset  = 0;
  }

  return -1;
}

char *
IOBufferReader::memcpy(void *ap, int64_t len, int64_t offset)
{
  char          *p  = static_cast<char *>(ap);
  IOBufferBlock *b  = block.get();
  offset           += start_offset;

  while (b && len) {
    int64_t max_bytes  = b->read_avail();
    max_bytes         -= offset;
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
    p      += bytes;
    len    -= bytes;
    b       = b->next.get();
    offset  = 0;
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
      int64_t        bytes = std::min(n, block_bytes - offset);
      IOBufferBlock *bb    = blocks->clone();
      if (offset) {
        bb->consume(offset);
        block_bytes -= offset; // bytes really available to use.
        offset       = 0;
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
  _len   += length;
  return length;
}

int64_t
IOBufferChain::write(IOBufferData *data, int64_t length, int64_t offset)
{
  int64_t        zret = 0;
  IOBufferBlock *b    = new_IOBufferBlock();

  if (length < 0) {
    length = 0;
  }

  b->set(data, length, offset);
  this->append(b);

  zret  = b->read_avail();
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
      _head  = _head->next;
      zret  += bytes;
      size  -= bytes;
    } else {
      _head->consume(size);
      zret += size;
      size  = 0;
    }
  }
  _len -= zret;
  if (_head == nullptr || _len == 0) {
    _head = nullptr, _tail = nullptr, _len = 0;
  }
  return zret;
}
