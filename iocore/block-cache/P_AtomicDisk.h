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



#ifndef _AtomicDisk_H_
#define _AtomicDisk_H_

class Action;
class Continuation;
class IOBufferBlock;
class MIOBuffer;

/**

 handle large writes to disk so that they appear to complete
 atomically, but use AIO to do it.

 Like other processors, this doesn't have a lock or dedicated thread
 of its own.  It uses the caller's Continuation's lock and thread for
 activity.
 */

class AtomicDisk
{
public:
  AtomicDisk();
  virtual ~ AtomicDisk();
  /**
    initialize and start the processor
    */
  void init();
  /**
     write IOBufferBlock chain to disk starting at offset.

     data is written out with header and footer blocked into 512 byte
     regions.
     <pre>
     [header data][header data][header data][footer data]
     </pre>
     header contains # of blocks.  header and footers contains
     sequence # which must match across blocks.

     Calls back continuation when complete.  Cancel Action to avoid
     callback, but that doesn't necessarily cancel the disk
     operation in progress.

     @return Action* Cancellable operation
     @param c
     @param offset offset must be multiple of underlying transfer size (512)
     @param blocks IOBufferBlock chain
     @param len  how much data from chain to write to disk.
  */
  Action *write(Continuation * c, int offset, IOBufferBlock * blocks, int len);
  /**
      read first header and region from disk at offset.  length is
      that of DISK_BLOCK.

      Calls back continuation when complete with a void *descriptor
      for region.  Cancel Action to avoid callback.
      @return Action* Cancellable
      @param c Continuation to call back
      @param offset
      @param buf
   */
  Action *startRead(Continuation * c, int offset, char *buf);
  /**
     return how much data in disk region.  This region may or may not
     actually be valid (i.e. completely written correctly)
     @param descriptor
   */
  int length(void *descriptor);
  /**
     read from rest of disk into MIOBuffer.

     successful if all data was written down,
     failure if any of header sequence #s mismatch.

     Callback c when complete with success or failure.  Cancel Action
     to avoid callback.

     @return Action* Cancellable
     @param c Continuation to be called back when complete.
     @param descriptor descriptor returned by startRead
     @param buf MIOBuffer to write into
   */
  Action *continueReadIOBuffer(Continuation * c, void *descriptor, int offset, MIOBuffer * buf, int len);
private:
  /// fd of underlying disk device
  int m_fd;
};

extern AtomicDisk atomicDiskProcessor;
#endif
