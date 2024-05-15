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

#include "P_CacheDoc.h"

#include "iocore/eventsystem/Continuation.h"

#include "tscore/ink_memory.h"
#include "tscore/List.h"

#include <cstring>

#define AGG_SIZE       (4 * 1024 * 1024) // 4MB
#define AGG_HIGH_WATER (AGG_SIZE / 2)    // 2MB

struct CacheVC;

class AggregateWriteBuffer
{
public:
  AggregateWriteBuffer()
  {
    this->_buffer = static_cast<char *>(ats_memalign(ats_pagesize(), AGG_SIZE));
    memset(this->_buffer, 0, AGG_SIZE);
  }

  ~AggregateWriteBuffer() { ats_free(this->_buffer); }

  AggregateWriteBuffer(AggregateWriteBuffer const &)            = delete;
  AggregateWriteBuffer &operator=(AggregateWriteBuffer const &) = delete;

  // move semantics not supported yet to keep things simple
  AggregateWriteBuffer(AggregateWriteBuffer &&other)            = delete;
  AggregateWriteBuffer &operator=(AggregateWriteBuffer &&other) = delete;

  /**
   * Check whether the internal buffer is empty.
   *
   * @return Returns true if the buffer is empty, otherwise false.
   */
  bool is_empty() const;

  /**
   * Add a new document to the buffer.
   *
   * This method copies the provided document into the buffer.
   *
   * This method may only be called if there is space at the current
   * buffer position for the document, and the document has a correct len
   * field. Use reset_buffer_pos to reset to the beginning of the buffer
   * when it gets full. If this condition is not met, the new document may
   * overrun the buffer.
   *
   * The buffer position will be updated to the end of the document's data
   * and the document length will be subtracted from the bytes pending
   * aggregation.
   *
   * @param document: A pointer to the document to add to the buffer. It must
   *   have a correct len field, and its headers and data must follow it. This
   *   requires a pointer to a full document buffer - not just the Doc struct.
   * @param approx_size The approximate size of all headers and data as
   *   determined by Stripe::round_to_approx_size. The document may not need
   *   this much space.
   *
   */
  void add(Doc const *document, int approx_size);

  /**
   * Create a new document in the buffer.
   *
   * This method may only be called if there is space at the current
   * buffer position for the document. Use reset_buffer_pos to reset
   * to the beginning of the buffer when it gets full. If this
   * condition is not met, the new document may overrun the buffer.
   *
   * The buffer position will be updated to the end of the document's data
   * and the document length will be subtracted from the bytes pending
   * aggregation.
   *
   * The new document will be uninitialized.
   *
   * @param approx_size The approximate size of all headers and data as
   *   determined by Stripe::round_to_approx_size. The document may not need
   *   this much space.
   * @return Returns a non-owning pointer to the new document.
   */
  Doc *emplace(int approx_size);

  /**
   * Flush the internal buffer to disk.
   *
   * This method should be called during shutdown. It must not be called
   * during regular operation.
   *
   * Flushing the buffer only writes the buffer to disk; it does not
   * modify the contents of the buffer. To reset the buffer, call
   * reset_buffer_pos().
   *
   * @param fd File descriptor to write to.
   * @param write_pos The offset at which to write the buffer data.
   * @return Returns true if all bytes were flushed, otherwise false.
   */
  bool flush(int fd, off_t write_pos) const;

  /**
   * Copy part of the buffer.
   *
   * The range of bytes to copy must fit within the written buffer.
   *
   * @param dest: The destination buffer.
   * @param offset: Byte offset to begin copying at.
   * @param nbytes: Number of bytes to copy.
   */
  void copy_from(char *dest, int offset, size_t nbytes) const;

  Queue<CacheVC, Continuation::Link_link> &get_pending_writers();
  char                                    *get_buffer();
  int                                      get_buffer_pos() const;
  void                                     seek(int offset);
  void                                     reset_buffer_pos();
  int                                      get_bytes_pending_aggregation() const;
  void                                     add_bytes_pending_aggregation(int size);

private:
  Queue<CacheVC, Continuation::Link_link> _pending_writers;
  char                                   *_buffer                    = nullptr;
  int                                     _bytes_pending_aggregation = 0;
  int                                     _buffer_pos                = 0;
};

inline Queue<CacheVC, Continuation::Link_link> &
AggregateWriteBuffer::get_pending_writers()
{
  return this->_pending_writers;
}

inline char *
AggregateWriteBuffer::get_buffer()
{
  return this->_buffer;
}

inline int
AggregateWriteBuffer::get_buffer_pos() const
{
  return this->_buffer_pos;
}

inline void
AggregateWriteBuffer::seek(int offset)
{
  this->_buffer_pos = offset;
}

inline void
AggregateWriteBuffer::reset_buffer_pos()
{
  this->seek(0);
}

inline int
AggregateWriteBuffer::get_bytes_pending_aggregation() const
{
  return this->_bytes_pending_aggregation;
}

inline void
AggregateWriteBuffer::add_bytes_pending_aggregation(int size)
{
  this->_bytes_pending_aggregation += size;
}

inline bool
AggregateWriteBuffer::is_empty() const
{
  return this->_buffer_pos == 0;
}
