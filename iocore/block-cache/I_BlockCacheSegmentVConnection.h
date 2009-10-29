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


#ifndef _I_BlockCacheSegmentVConnection_H_
#define _I_BlockCacheSegmentVConnection_H_

#include "I_EventSystem.h"

/**

  segment manipulation public API.

  It is expected that this API will be the same regardless of
  underlying implementation, i.e. regardless of whether data is stored
  in conventional filesystem or in object store

  To add:
    - pinning
    - extra metadata
    - how should partitioning be specified?
  
  For testability, these can be created and activated without external
  structures, e.g. I_BlockCacheVConnection, BC_OpenDir,
  BC_OpenSegment.  If the external structures aren't supplied then
  default behavior is performed instead.
  
 */

class BlockCacheSegmentVConnection:public VConnection
{
public:

  Link<BlockCacheSegmentVConnection> opensegment_link;
  /// constructor
  BlockCacheSegmentVConnection(ProxyMutex * p);

  /** use implementational class's destructor.
   */
  virtual ~ BlockCacheSegmentVConnection();

  /// How data will be accessed in the segment
  enum AccessType
  { e_for_read, e_for_write };

  /**
    associate BC_OpenSegment with this connection.

    @param seg BC_OpenSegment to associate with.
    @param type which type of access
    
  */

  virtual void setBCOpenSegment(BC_OpenSegment * seg, AccessType type);

  /**
    force data to disk.

    Calls back Continuation when data and directory has hit the disk
    for <b>this particular segment</b>.  No further operations allowed
    on the BlockCacheSegmentVConnection during this time.  <b><i>What
    about pending do_io_writes?</i></b> Writes are not serviced until
    after the sync.  It only is valid to call this after a successful
    do_io_close().
    
    @param c Caller
    @return Action* Canceling this cancels the callback, but sync
    will still occur.

    */
  virtual Action *sync(Continuation * c) = 0;

  /**
    Write to segment reading from source at most len bytes.

    Returns either VC_EVENT_WRITE_COMPLETE,VIO* when no more data to be
    written, or VC_EVENT_WRITE_READY,VIO* when space (< watermark) is free.

    @param c Caller
    @param nbytes  how much data to write
    @param buf Where to read data from
    @param owner

    */
  virtual VIO *do_io_write(Continuation * c = NULL,
                           int nbytes = INT_MAX, IOBufferReader * buf = NULL, bool owner = false) = 0;

  /**

    Read from segment starting at offset in the segment into MIOBuffer
    buf of at most len bytes.

    Returns either VC_EVENT_READ_COMPLETE,VIO* when no more data or
    VC_EVENT_READ_READY,VIO* when some data (> watermark) is readable.

    @param c Caller
    @param nbytes how much data to read
    @param buf where to write data

    */
  virtual VIO *do_io_read(Continuation * c = NULL, int nbytes = INT_MAX, MIOBuffer * buf = NULL) = 0;

  /**
    close off object.

    On abort, any attached readers are aborted too when they reach the
    end of the data already written. The data is not written to disk.

    On normal close, any attached readers get VC_EVENT_READ_COMPLETE
    when they reach the end of data.

    @param err err is not provided if a normal close.
    err is provided (>= 0) if it is an abort.

    */
  virtual void do_io_close(int err = -1) = 0;

/**
  Try to close off object.

  If err >= 0 (this is an abort), and this was an unabortable write,
  and there is an active reader, then this call fails and the
  BlockCacheVConnection lives. Otherwise, it succeeds and
  BlockCacheVConnection is closed and data thrown away on disk.
  
  This is similar to do_io_close with error (i.e. abort), but we don't
  allow VC to die if readers are still active.  So, caller should only
  call this if they are prepared to stay alive until the object is
  written.

  @param err  err is not provided if a normal close
  err is provided (>= 0) if it is an abort.

  */
  virtual int try_do_io_close(int err = -1) = 0;

  /**
    no implementation
    */
  virtual void do_io_shutdown(ShutdownHowTo_t howto)
  {
    (void) howto;
  };
private:
};

/**

  BlockCacheSegmentVConnection construction interfaces -- because we
  want to hide implementation, we don't expose the constructor (and
  thereby the class) of the underlying implementation.  Instead we
  expose a wrapper function.

  This should really be in a separate header file.  The individual
  definitions will actually be spread across multiple files, not just
  the BlockCacheSegmentVConnection.cc file.
  
  */
struct BlockCacheSegmentVConnection_util
{
  /**
    instantiate and return new object
   */
  static BlockCacheSegmentVConnection *create(ProxyMutex *, int fd);
};

#endif
