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

#ifndef _I_BlockCacheVConnection_H_
#define _I_BlockCacheVConnection_H_

/** Block cache object manipulation public API. */
class BlockCacheVConnection:public VConnection
{
public:

  /** Constructor */
  BlockCacheVConnection();

  /**
    Prepare to write to segment at most len bytes (len is a hint).

    If an active writer already exists, returns
    BlockCacheVConnection::e_open_write_segment_failed

    If there are already readers for the particular key, then
    a second segment is created (using new storage) and all new
    readers will continue to use existing segment until writer
    is done. A BlockCacheVConnection is returned with event code
    BlockCacheVConnection::e_open_write_segment.

    If there are no readers for the particular key, then
    BlockcCacheSegmentVConnection is returned. All subsequent readers will
    access old segment data on disk (if any exist) until writer is done.

    @param c caller.
    @param key of segment to write (this should been created with
      BlockCacheKey_util::new_from_segmentid).
    @param len how much data will be written (estimated).
    @return cancels the callback.

  */
  Action *open_write_segment(Continuation * c, BlockCacheKey * key, int len) = 0;

  /**
    Same as open_write_segment, but this instructs cache that readers
    can read from this actively written segment.

    (same as open_write_segment) If an active writer already exists,
    returns BlockCacheVConnection::e_open_write_segment_failed.

    (differing from open_write_segment) If there are already readers
    for the particular key, then a second segment is created (using
    new storage) and all new readers will continue to use this
    <b>new</b> segment until writer is done.  A BlockCacheVConnection
    is returned with event code BlockCacheVConnection::e_open_write_segment.

    (differing from open_write_segment) If there are no readers for
    the particular key, then BlockcCacheSegmentVConnection is returned
    with event code BlockCacheVConnection::e_open_write_segment.  All
    subsequent readers will access this actively written segment.

    @param c caller.
    @param key of segment to write (this should been created with
      BlockCacheKey_util::new_from_segmentid).
    @param len how much data will be written (estimated).
    @return cancels the callback.

  */

  Action *open_unabortable_write_segment(Continuation * c, BlockCacheKey * key, int len) = 0;

  /**
    @em Likely we will also want to be able to return an IOBufferReader
    interface into the segment that the caller can use instead so that
    the caller can take its IO cues solely from the cache rather than
    the downstream client.  Then we need to add a close_read() interface
    so that we can make cache stop sending data to us even though we
    continue to send data to it.

    returns BlockCacheVConnection::e_open_read_segment and the
    BlockCacheSegmentVConnection if data exists, otherwise returns
    BlockCacheVConnection::e_open_read_segment_failed.

    @param c caller.
    @param key of segment to write (this should been created with
      BlockCacheKey_util::new_from_segmentid).
    @return cancels the callback.

  */

  Action *open_read_segment(Continuation * c, BlockCacheKey * key) = 0;

  /**
    Hide segment data from new readers and remove segment data when all
    readers go away.

    If active writer or reader for segment exists, then mark that as
    being invisible to new readers.  I.e. an open_read_segment call on
    the key will get BlockCacheVConnection::e_open_read_segment_failed.

    Callback is BlockCacheVConnection::e_remove_segment.  If segment
    never existed, then callback is
    BlockCacheVConnection::e_remove_segment_failed.

    @param c Caller
    @param key of segment to remove (this should been created with
      BlockCacheKey_util::new_from_segmentid).
    @return cancels the callback, however segment will be removed.

    */

  Action *remove_segment(Continuation * c, BlockCacheKey * key) = 0;

  /**
    Close off or abort all BlockCacheSegmentVConnections opened by
    this BlockCacheVConnection.

    Semantics of the do_io_close are those of the individual
    BlockCacheSegmentVConnections.

    @param err not provided if a normal close;
      err is provided (>= 0) if it is an abort.
    */

  void do_io_close(int err = -1) = 0;

  /**
    @em Try to close off or abort all BlockCacheSegmentVConnections opened
    by this BlockCacheVConnection.

    Semantics of the do_io_close are those of the individual
    BlockCacheSegmentVConnections.  If this is a normal close and any
    of the BlockCacheSegmentVConnections fails to close properly, then
    this call fails and some of the segments stay open.  caller needs
    to continue to write to these remaining segments.

    @param err not provided if a normal close;
      err is provided (>= 0) if it is an abort.

  */
  int try_do_io_close(int err = -1) = 0;

  /**
    Force data to disk.

    Calls back Continuation when data and directory has hit the disk
    consistently for <b>all</b> segments that have been closed().  We
    want this to be called only after do_io_close().

    @param c caller.
    @return canceling this cancels the callback, but sync will still occur.

  */
  Action *sync(Continuation * c) = 0;

  /// callback event code
  enum EventType
  {
    e_open_write_segment = BLOCK_CACHE_EVENT_EVENTS_START + 10,
    e_open_write_segment_failed,
    e_open_read_segment,
    e_open_read_segment_failed,
    e_remove_segment,
    e_remove_segment_failed,
  };

private:
};

#endif
