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

#include "ts/ink_config.h"
#include <assert.h>
#include <string.h>
#include "AbstractBuffer.h"
/* #include "CacheAtomic.h" */
#include "ts/ink_align.h"

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ABError
AbstractBuffer::checkout_write(int *write_offset, int write_size, uint64_t retries)
{
  VolatileState old_vs;
  VolatileState new_vs;

  write_size = INK_ALIGN(write_size, alignment);

  // Initialize the buffer if it currently isn't in use.
  old_vs = vs;
  new_vs = old_vs;

  if (new_vs.s.state == AB_STATE_UNUSED) {
    new_vs.s.state = AB_STATE_INITIALIZING;
    if (switch_state(old_vs, new_vs)) {
      vs_history[AB_STATE_INITIALIZING] = old_vs;
      initialize();
    }
  }

  while (retries-- > 0) {
    old_vs = vs;
    new_vs = old_vs;

    if (new_vs.s.state != AB_STATE_READ_WRITE) {
      return AB_ERROR_STATE;
    }

    if ((uint32_t)(new_vs.s.offset + write_size) > (uint32_t)size) {
      new_vs.s.state = AB_STATE_READ_ONLY;
      if (switch_state(old_vs, new_vs)) {
        vs_history[AB_STATE_READ_ONLY] = old_vs;
        full();
      }
      return AB_ERROR_FULL;
    }

    *write_offset = new_vs.s.offset;
    new_vs.s.offset += write_size;
    new_vs.s.writer_count += 1;

    if (switch_state(old_vs, new_vs)) {
      ink_assert((*write_offset + write_size) <= size);
      return AB_ERROR_OK;
    }
  }

  return AB_ERROR_BUSY;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ABError
AbstractBuffer::checkout_read(int read_offset, int read_size)
{
  VolatileState old_vs;
  VolatileState new_vs;

  do {
    old_vs = vs;
    new_vs = old_vs;

    if ((new_vs.s.state != AB_STATE_READ_WRITE) && (new_vs.s.state != AB_STATE_READ_ONLY) && (new_vs.s.state != AB_STATE_FLUSH)) {
      return AB_ERROR_STATE;
    }

    if ((uint32_t)(read_offset + read_size) > new_vs.s.offset) {
      return AB_ERROR_OFFSET;
    }

    new_vs.s.reader_count += 1;
  } while (!switch_state(old_vs, new_vs));

  return AB_ERROR_OK;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ABError
AbstractBuffer::checkin_write(int write_offset)
{
  VolatileState old_vs;
  VolatileState new_vs;

  do {
    old_vs = vs;
    new_vs = old_vs;

    ink_assert(new_vs.s.writer_count > 0);
    ink_assert((new_vs.s.state == AB_STATE_READ_WRITE) || (new_vs.s.state == AB_STATE_READ_ONLY));
    ink_assert((uint32_t)write_offset < new_vs.s.offset);

    new_vs.s.writer_count -= 1;
  } while (!switch_state(old_vs, new_vs));

  old_vs = vs;
  new_vs = old_vs;

  while ((new_vs.s.state == AB_STATE_READ_ONLY) && (new_vs.s.writer_count == 0)) {
    new_vs.s.state = AB_STATE_FLUSH;
    if (switch_state(old_vs, new_vs)) {
      vs_history[AB_STATE_FLUSH] = old_vs;
      flush();
      break;
    }

    old_vs = vs;
    new_vs = old_vs;
  }

  return AB_ERROR_OK;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ABError
AbstractBuffer::checkin_read(int read_offset)
{
  VolatileState old_vs;
  VolatileState new_vs;

  do {
    old_vs = vs;
    new_vs = old_vs;

    ink_assert(new_vs.s.reader_count > 0);
    ink_assert(new_vs.s.state != AB_STATE_UNUSED);
    ink_assert((uint32_t)read_offset < new_vs.s.offset);

    new_vs.s.reader_count -= 1;
  } while (!switch_state(old_vs, new_vs));

  if ((new_vs.s.state == AB_STATE_FLUSH_COMPLETE) && (new_vs.s.reader_count == 0)) {
    destroy();
  }

  return AB_ERROR_OK;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
AbstractBuffer::initialize()
{
  ink_assert(vs.s.state == AB_STATE_INITIALIZING);
  ink_assert(vs.s.writer_count == 0);
  ink_assert(vs.s.reader_count == 0);

  if (!unaligned_buffer) {
    unaligned_buffer = new char[size + 511];
    buffer           = (char *)align_pointer_forward(unaligned_buffer, 512);
  }

  vs_history[AB_STATE_READ_WRITE] = vs;

  vs.s.offset = 0;
  vs.s.state  = AB_STATE_READ_WRITE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
AbstractBuffer::full()
{
  if ((vs.s.state == AB_STATE_READ_ONLY) && (vs.s.writer_count == 0)) {
    VolatileState old_vs(vs);
    VolatileState new_vs(old_vs);

    while ((new_vs.s.state == AB_STATE_READ_ONLY) && (new_vs.s.writer_count == 0)) {
      new_vs.s.state = AB_STATE_FLUSH;
      if (switch_state(old_vs, new_vs)) {
        vs_history[AB_STATE_FLUSH] = old_vs;
        flush();
        break;
      }

      old_vs = vs;
      new_vs = old_vs;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
AbstractBuffer::flush()
{
  ink_assert(vs.s.state == AB_STATE_FLUSH);
  ink_assert(vs.s.writer_count == 0);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
AbstractBuffer::flush_complete()
{
  VolatileState old_vs;
  VolatileState new_vs;

  /* INKqa06826 - Race Condition. Must make sure that setting the new state is
     atomic. If there is a context switch in the middle of setting the state to
     AB_STATE_FLUSH_COMPLETE, the checkin_read would be lost, the reader_count
     will never go to 0, resulting in memory leak */

  do {
    old_vs = vs;
    new_vs = old_vs;

    ink_assert(vs.s.state == AB_STATE_FLUSH);
    ink_assert(vs.s.writer_count == 0);
    new_vs.s.state = AB_STATE_FLUSH_COMPLETE;

  } while (!switch_state(old_vs, new_vs));

  vs_history[AB_STATE_FLUSH_COMPLETE] = vs;

  if (vs.s.reader_count == 0) {
    destroy();
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
AbstractBuffer::destroy()
{
  ink_assert(vs.s.state == AB_STATE_FLUSH_COMPLETE);
  ink_assert(vs.s.writer_count == 0);
  ink_assert(vs.s.reader_count == 0);

  vs_history[AB_STATE_UNUSED] = vs;

  vs.s.offset = 0;
  vs.s.state  = AB_STATE_UNUSED;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
AbstractBuffer::clear()
{
  if (unaligned_buffer) {
    delete[] unaligned_buffer;
  }
  unaligned_buffer = buffer = NULL;

  vs_history[AB_STATE_UNUSED] = vs;

  vs.s.writer_count = 0;
  vs.s.reader_count = 0;
  vs.s.offset       = 0;
  vs.s.state        = AB_STATE_UNUSED;
}
