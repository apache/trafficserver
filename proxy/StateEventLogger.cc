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

#include "ink_unused.h"  /* MAGIC_EDITING_TAG */
#include "StateEventLogger.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef USE_RINGBUF
/*
 * two situations: . = data.   d_next== d_free is empty
 * d_next          d_free                     "A"
 * v...............v
 *
 *       d_free          d_next   v (d_len-1)        "B"
 * ......v               v.........
 * d_len points off the end of the array
 */

RingBuf::RingBuf(int len)
{
  d_buf = (char *) calloc(1, len + 1);
  d_len = len + 1;
  d_free = 0;
  d_next = 0;
}

RingBuf::~RingBuf()
{
  xfree(d_buf);
}

int
RingBuf::avail()
{
  if (d_next <= d_free) {
    return d_free - d_next;     // "A"
  } else {
    return (d_len - d_next + d_free);
  }
}

                                // for length 1 array
                                //
                                //
int
RingBuf::free()
{
  if (d_next <= d_free) {
    return (d_next + d_len - d_free - 1);       // "A"
  } else {
    return (d_next - d_free - 1);
  }
}

int
RingBuf::append(char *buf, int len)
{
  int towrite = min(len, free());
  if (d_next <= d_free) {
    int wrote = 0;
    while (d_free<d_len && towrite> 0) {
      d_buf[d_free] = *buf;
      buf++;
      towrite--;
      d_free++;
      wrote++;
    }
    // fell off end of array or source buf
    if (d_free == d_len) {
      d_free = 0;
    }
    while (d_free<(d_next - 1) && towrite> 0) {
      d_buf[d_free] = *buf;
      buf++;
      towrite--;
      d_free++;
      wrote++;
    }
    // we always have a free space
    assert(d_free != d_next);
    return wrote;
  } else {
    int wrote = 0;
    while (d_free<d_next && towrite> 0) {
      d_buf[d_free] = *buf;
      buf++;
      towrite--;
      d_free++;
      wrote++;
    }
    // fell off end of array or source buf
    // we always have a free space
    assert(d_free != d_next);
    return wrote;
  }
}

int
RingBuf::flush(int d_fd, int len)
{
  int toflush = min(len, avail());
  int nflushed = 0;
  if (d_next <= d_free) {
    if (d_next<d_free && toflush> 0) {
      int w = min(d_free - d_next, toflush);
      write(d_fd, &d_buf[d_next], w);
      nflushed = w;
      d_next += w;
      toflush -= w;
    }
    assert(toflush == 0);
    return (nflushed);
  } else {
    if (d_next<d_len && toflush> 0) {
      int w = min(d_len - d_next, toflush);
      write(d_fd, &d_buf[d_next], w);
      d_next += w;
      nflushed = w;
      toflush -= w;
    }
    if (d_next == d_len) {
      d_next = 0;
    }
    if (d_next<d_free && toflush> 0) {
      int w = min(d_free - d_next, toflush);
      write(d_fd, &d_buf[d_next], w);
      d_next += w;
      nflushed += w;
      toflush -= w;
    }
    assert(toflush == 0);
    return (nflushed);
  }
}

#ifdef TEST_RINGBUF
#define test(x) \
if (!(x)) { \
  printf (# x " failed\n"); \
}

int
main()
{
  RingBuf m(1);
  char buf[2];
  test(m.avail() == 0);
  test(m.free() == 1);
  test(m.append("b", 2) == 1);
  test(m.avail() == 1);
  test(m.free() == 0);
  test(m.flush(0, 2) == 1);     // write to stdout
  //test(buf[0]=='b');
}
#endif
#endif /* USE_RINGBUF */

StateEvent::~StateEvent()
{
}

#ifdef USE_RINGBUF
void
StateEventLogger::operator () (const StateEvent & x)
{
  if (d_fd < 0) {
    return;
  }
  // try lock d_buf head pointer
  if (ink_mutex_try_acquire(&d_head_lock)) {
    // if lock taken and
    if (d_buf->avail() > d_highwater) {
      // flush buffer & update head pointer
      d_buf->flush(d_fd, d_buf->avail());
    }
    // if locked unlock d_buf head pointer
    ink_mutex_release(&d_head_lock);
  }
  // if lock failed, we could be writing to the end of the buffer,
  // even while another thread is flushing the buffer.

  // lock d_buf tail pointer
  if (ink_mutex_acquire(&d_tail_lock)) {
    if (x.size() < d_buf->free()) {
      x.marshal(d_buf);
    } else {
      // this is bad too.
      assert(0);
    }
    // unlock d_buf tail pointer
    ink_mutex_release(&d_tail_lock);
  } else {
    assert(0);                  // this should never happen
  }
}
#else

void
StateEventLogger::operator () (const StateEvent & x)
{
  if (d_fd < 0) {
    return;
  }
  char buf[12];
  x.marshal(buf);
  write(d_fd, buf, x.size());
}

#endif

#ifdef TEST_STATELOGGER
#define test(x) \
if (!(x)) { \
  printf (# x " failed\n"); \
}

TestStateEvent x;
StateEventLogger *sel;

#define 	N_THREADS 10

void *
test_thread(void *i)
{
  int pi = (int) i;
  int j;
  int calls = 70000;
  ink_hrtime start, finish;
  start = ink_get_hrtime();
  for (j = 0; j < calls; j++) {
    (*sel) (TestStateEvent(pi, j));
  }
  finish = ink_get_hrtime();
  printf("StateEventLogger cost = %" PRId64 "ns\n", (finish - start) / (N_THREADS * calls));
  return NULL;
}

int
main()
{
  // create some threads.
  int i;
  ink_thread t[N_THREADS];
  sel = NEW(new StateEventLogger("sel.out"));

  for (i = 0; i < N_THREADS; i++) {
    t[i] = ink_thread_create(test_thread, (void *) i);
  }
  for (i = 0; i < N_THREADS; i++) {
    ink_thread_join(t[i]);
  }
  // check that output is not garbled
  // actually just print out the cost of calling this function:
  delete sel;

}
#endif
