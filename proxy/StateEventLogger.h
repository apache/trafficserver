/** @file

  Small impl of an event logging system

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

  @section details Details

  This is a small, (hopefully fast) implementation of a event logging
  system (per-cost call (including disk i/o) on Sparc was between 25 and
  34 us, on Alpha was between 19 and 32 us -- run test_StateEventLogger
  with N_THREADS set to "1" to determine this.)  It just streams
  events to a file.  The intended use is for state machine tracing and
  debugging and event trace collection, e.g. disk i/o events or object
  creation/deletion events.

  The usage is:
    -# Create a global state logger object for the particular event
       type -- there is no requirement for this, just a convention so
       that each log file contains events of the same type.
    -# When an event occurs in your code, create an event instance and
       pass it to the state logger.
    -# repeat 2.

  Events can be logged with high resolution timestamps and application
  specific data. The event instance is marshalled into a machine
  independent form (Sparc byte order) before storing to disk.

  These functions will be inlined, keep the code path short.

  To extend, simply: define a new class derived from StateEvent that
  provides marshal() and size() and pass those classes to the state
  logger object.  Look at TestStateEvent for an example.

  Example usage:

  @code
   StateEventLogger disklog("disk.out");

   // ...
   disklog(TestStateEvent(my_param1a,my_param2a));
   // ...
   disklog(TestStateEvent(my_param1b,my_param2b));
  @endcode

*/

#ifndef _StateEventLogger_h_
#define _StateEventLogger_h_

// nice property of ring buffer is that buffer update maintenance
// (e.g. writing out data and freeing up buffer space) is constant
// cost, and append is constant.  The only cost is that sequential
// acces gets split into possibly two non-contiguous operations.
#include "inktomi++.h"
#include "ink_platform.h"
#include <time.h>

#ifdef __alpha
// these macros convert to sparc byte ordering from Alpha form.
#define my_byteorder_int(to,from) \
  to = (((unsigned int)from) >> 16) | (from << 16)
#define my_byteorder_hrtime(to,from) \
  to = ((from & 0xffff) << 48) | \
       ((from & 0xffff0000) << 16) | \
       ((from & 0xffff00000000) >> 16) | \
       ((from & 0xffff000000000000) >> 48)

#else
#define my_byteorder_int(to,from) \
  to=from
#define my_byteorder_hrtime(to,from) \
  to = from

#endif
//#define USE_RINGBUF

// This code is too complex for the minimal I/O system call reduction
// that it might provide.

#ifdef USE_RINGBUF
class RingBuf
{
public:
  RingBuf(int len);             // alloc ring buffer of N bytes
   ~RingBuf();
  int free();                   // space available for storing
  int avail();                  // data available for reading
  int flush(int fd, int len);   // write data to file
  int append(char *buf, int len);       // add to buffer

  // this is a costly operation.  Don't use it.  It just was pulled in
  // from the library.
  char *as_buf(int *len);       // returns buffer of length len representing the read.

private:
  char *d_buf;
  int d_len;                    // length of buffer
  int d_free;                   // free space
  int d_next;                   // next data
};
#endif

class StateEvent
{                               // interface class
public:
  virtual ~ StateEvent();
  // convert to machine independent form and whack into buffer
#ifdef USE_RINGBUF
  virtual void marshal(RingBuf * buf) const = 0;
#else
  virtual void marshal(char *buf) const = 0;
#endif
  // size of marshalled parameters
  virtual int size() const = 0;
};

class TestStateEvent:public StateEvent
{
public:
  TestStateEvent()
  {
    d_param1 = d_param2 = 0;
    d_ts = ink_get_hrtime();
  }
  TestStateEvent(int d1, int d2)
  {
    d_param1 = d1;
    d_param2 = d2;
    d_ts = ink_get_hrtime();
  }
  virtual ~ TestStateEvent() {
  };
#ifdef USE_RINGBUF
  virtual void marshal(RingBuf * buf) const
  {
    int x = ntohl(d_param1);
      buf->append((char *) &x, sizeof(x));
      x = ntohl(d_param2);
      buf->append((char *) &x, sizeof(x));
  };
#else
  virtual void marshal(char *buf) const
  {
    ink_hrtime t;
      my_byteorder_hrtime(t, d_ts);
      memcpy(buf, &t, sizeof(t));
      buf += sizeof(t);

    int x;

      my_byteorder_int(x, d_param1);
      memcpy(buf, &x, sizeof(x));
      buf += sizeof(x);

      my_byteorder_int(x, d_param2);
      memcpy(buf, &x, sizeof(x));
  };
#endif
  virtual int size() const
  {
    return sizeof(d_param1) + sizeof(d_param2) + sizeof(d_ts);
  };
  // just set these manually or via constructor
  ink_hrtime d_ts;
  int d_param1;
  int d_param2;
};

class StateEventLogger
{
public:
#ifdef USE_RINGBUF
  StateEventLogger(char *fname, const StateEvent * s, int nevents = 10000) {
#else
  StateEventLogger(char *fname)
  {
#endif
    printf("opening\n");
    fflush(stdout);
    d_fd = open(fname, O_RDWR | O_CREAT, 0644);
#ifdef USE_RINGBUF
    ink_mutex_init(&d_head_lock, "StateEventLogger:head_lock");
    ink_mutex_init(&d_tail_lock, "StateEventLogger:tail_lock");
    d_buf = dNEWnew RingBuf(nevents * s->size());
    d_highwater = nevents * s->size() / 2;
#endif
  };
  ~StateEventLogger() {
    printf("cleaning up\n");
    fflush(stdout);
#ifdef USE_RINGBUF
    //  it would be nice to flush out buffers.
    d_buf->flush(d_fd, d_buf->avail());
#endif
    close(d_fd);
  }
  void operator() (const StateEvent & x);
private:
  int d_fd;                     // file to write events to
#ifdef USE_RINGBUF
  int d_highwater;              // about 1/2 of buffer
  RingBuf *d_buf;
  ink_mutex d_head_lock;
  ink_mutex d_tail_lock;
#endif
};
#endif
