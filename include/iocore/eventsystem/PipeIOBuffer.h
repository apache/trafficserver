//
// Created by Yihong Jin on 1/26/25.
//

#pragma once


#if TS_USE_LINUX_SPLICE

#include "IOBuffer.h"

class PipeIOBuffer;
class PipeIOBufferReader;

class PipeIOBufferReader : public IOBufferReader
{
 public:
  PipeIOBufferReader(){};
  // Overridden methods from IOBufferReader
  char            *start() override;
  char            *end() override;
  int64_t          read_avail() override;
  bool             is_read_avail_more_than(int64_t size) override;
  int              block_count() override;
  int64_t          block_read_avail() override;
  std::string_view block_read_view() override;
  void             skip_empty_blocks() override;
  void             clear() override;
  void             reset() override;
  void             consume(int64_t n) override;
  IOBufferReader  *clone() override;
  void             dealloc() override;
  IOBufferBlock   *get_current_block() override;
  bool             current_low_water() override;
  bool             low_water() override;
  bool             high_water() override;
  int64_t          memchr(char c, int64_t len = INT64_MAX, int64_t offset = 0) override;
  int64_t          read(void *buf, int64_t len) override;
  char            *memcpy(void *buf, int64_t len = INT64_MAX, int64_t offset = 0) override;
  char            &operator[](int64_t i) override;
};

class PipeIOBuffer : public MIOBuffer
{
 public:
  PipeIOBuffer();
  ~PipeIOBuffer() override;

  // MIOBuffer methods
  void    fill(int64_t len) override;
  void    consume(int64_t len);
  void    append_block(IOBufferBlock *b) override;
  void    append_block(int64_t asize_index) override;
  void    add_block() override;
  void    append_xmalloced(void *b, int64_t len) override;
  void    append_fast_allocated(void *b, int64_t len, int64_t fast_size_index) override;
  int64_t write(const void *buf, int64_t nbytes) override;
  int64_t write(IOBufferReader *r, int64_t len = INT64_MAX, int64_t offset = 0) override;
  int64_t write(IOBufferChain const *chain, int64_t len = INT64_MAX, int64_t offset = 0) override;

  IOBufferBlock *first_write_block() override;
  char          *buf() override;
  char          *buf_end() override;
  char          *start() override;
  char          *end() override;

  int64_t block_write_avail() override;
  int64_t current_write_avail() override;
  int64_t write_avail() override;
  int64_t block_size() override;
  bool    high_water() override;
  bool    low_water() override;
  bool    current_low_water() override;

  IOBufferReader *alloc_accessor(MIOBufferAccessor *anAccessor) override;
  IOBufferReader *alloc_reader() override;
  IOBufferReader *clone_reader(IOBufferReader *r) override;
  void            dealloc_reader(IOBufferReader *e) override;
  void            set(void *b, int64_t len) override;
  void            alloc(int64_t i) override;
  void            append_block_internal(IOBufferBlock *b) override;
  int64_t         write(IOBufferBlock const *b, int64_t len, int64_t offset) override;

  int64_t max_read_avail() override;
  bool    is_max_read_avail_more_than(int64_t size) override;
  int     max_block_count() override;
  void    check_add_block() override;

  void reset() override;
  void init_readers() override;
  void dealloc() override;
  void free() override;

  // clear() is called by the constructor and destructor of MIOBuffer
  // so it could not be virtual and override in derived class.
  void clear();

  // Public data members
  int                fd[2];            // Pipe file descriptors
  PipeIOBufferReader pipe_reader;      // Single reader instance for the pipe
  bool               reader_allocated; // Tracks if the reader is currently allocated
  int64_t            data_in_pipe;     // Amount of data currently in the pipe and not consumed
  int64_t            pipe_capacity;    // Total capacity of the pipe
};

extern PipeIOBuffer *new_PipeIOBuffer_internal(const char *loc, int64_t pipe_capacity);

class PipeIOBuffer_tracker
{
  const char *loc;

 public:
  explicit PipeIOBuffer_tracker(const char *_loc) : loc(_loc) {}
  PipeIOBuffer *
  operator()(int64_t size_index)
  {
    return new_PipeIOBuffer_internal(loc, BUFFER_SIZE_FOR_INDEX(size_index));
  }
};

/// MIOBuffer allocator/deallocator
#define new_PipeIOBuffer PipeIOBuffer_tracker(RES_PATH("memory/IOBuffer/"))
extern void free_PipeIOBuffer(PipeIOBuffer *mio);

#endif // TS_USE_LINUX_SPLICE