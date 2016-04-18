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

#ifndef __ABSTRACT_BUFFER_H__
#define __ABSTRACT_BUFFER_H__

#include "ts/ink_platform.h"
#include "ts/ink_atomic.h"
#include "ts/ink_assert.h"

enum ABError {
  AB_ERROR_OK,
  AB_ERROR_BUSY,
  AB_ERROR_STATE,
  AB_ERROR_FULL,
  AB_ERROR_OFFSET,
};

class AbstractBuffer
{
public:
  enum AbstractBufferState {
    AB_STATE_UNUSED,
    AB_STATE_INITIALIZING,
    AB_STATE_READ_WRITE,
    AB_STATE_READ_ONLY,
    AB_STATE_FLUSH,
    AB_STATE_FLUSH_COMPLETE
  };

protected:
  union VolatileState {
    VolatileState() { ival = 0; }
    VolatileState(volatile VolatileState &vs) { ival = vs.ival; }
    VolatileState &
    operator=(volatile VolatileState &vs)
    {
      ival = vs.ival;
      return *this;
    }

    int64_t ival;
    struct {
      uint16_t reader_count;
      uint16_t writer_count;
      uint32_t offset : 29;
      uint32_t state : 3;
    } s;
  };

public:
  AbstractBuffer(int xsize, int xalignment) : buffer(NULL), unaligned_buffer(NULL), size(xsize), alignment(xalignment) { clear(); }
  virtual ~AbstractBuffer() { clear(); }
  char *
  data()
  {
    return buffer;
  }
  char &operator[](int idx)
  {
    ink_assert(idx >= 0);
    ink_assert(idx < size);
    return buffer[idx];
  }
  int
  offset()
  {
    return vs.s.offset;
  }

  virtual ABError checkout_write(int *write_offset, int write_size, uint64_t retries = (uint64_t)-1);
  virtual ABError checkout_read(int read_offset, int read_size);
  virtual ABError checkin_write(int write_offset);
  virtual ABError checkin_read(int read_offset);

  virtual void initialize();
  virtual void full();
  virtual void flush();
  virtual void flush_complete();
  virtual void destroy();
  virtual void clear();

  bool switch_state(VolatileState &old_state, VolatileState &new_state);

public:
  volatile VolatileState vs;
  char *buffer;
  char *unaligned_buffer;
  int size;
  int alignment;

public:
  VolatileState vs_history[AB_STATE_FLUSH_COMPLETE + 1];
};

class AbstractBufferReader
{
public:
  AbstractBufferReader(AbstractBuffer *xbuffer, int xoffset) : buffer(xbuffer), offset(xoffset) {}
  ~AbstractBufferReader() { buffer->checkin_read(offset); }
private:
  AbstractBuffer *buffer;
  int offset;
};

class AbstractBufferWriter
{
public:
  AbstractBufferWriter(AbstractBuffer *xbuffer, int xoffset) : buffer(xbuffer), offset(xoffset) {}
  ~AbstractBufferWriter() { buffer->checkin_write(offset); }
private:
  AbstractBuffer *buffer;
  int offset;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
AbstractBuffer::switch_state(VolatileState &old_vs, VolatileState &new_vs)
{
  if (ink_atomic_cas((int64_t *)&vs.ival, old_vs.ival, new_vs.ival)) {
    return true;
  }

  return false;
}

#endif /* __ABSTRACT_BUFFER_H__ */
