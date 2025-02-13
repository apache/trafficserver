//
// Created by Yihong Jin on 1/26/25.
//
#pragma once

#include "tscore/ink_platform.h"
#include "tscore/ink_resource.h"

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

TS_INLINE void
PipeIOBuffer::free()
{
  clear();
  THREAD_FREE(this, pipeIOAllocator, this_thread());
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