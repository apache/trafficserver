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
#define I_VIO_h

#include "ts/ink_platform.h"
#include "I_EventSystem.h"
#if !defined(I_IOBuffer_h)
#error "include I_IOBuffer.h"
---include I_IOBuffer.h
#endif
#include "ts/ink_apidefs.h"
   class Continuation;
class VConnection;
class IOVConnection;
class MIOBuffer;
class ProxyMutex;

/**
  Descriptor for an IO operation.

  A VIO is a descriptor for an in progress IO operation. It is
  returned from do_io_read() and do_io_write() methods on VConnections.
  Through the VIO, the state machine can monitor the progress of
  an operation and reenable the operation when data becomes available.

  The VIO operation represents several types of operations, and
  they can be identified through the 'op' member. It can take any
  of the following values:

  <table>
    <tr>
      <td align="center"><b>Constant</b></td>
      <td align="center"><b>Meaning</b></td>
    </tr>
    <tr><td>READ</td><td>The VIO represents a read operation</td></tr>
    <tr><td>WRITE</td><td>The VIO represents a write operation</td></tr>
    <tr><td>CLOSE</td><td>The VIO represents the request to close the
                          VConnection</td></tr>
    <tr><td>ABORT</td><td></td></tr>
    <tr><td>SHUTDOWN_READ</td><td></td></tr>
    <tr><td>SHUTDOWN_WRITE</td><td></td></tr>
    <tr><td>SHUTDOWN_READWRITE</td><td></td></tr>
    <tr><td>SEEK</td><td></td></tr>
    <tr><td>PREAD</td><td></td></tr>
    <tr><td>PWRITE</td><td></td></tr>
    <tr><td>STAT</td><td></td></tr>
  </table>

*/
class VIO
{
public:
  ~VIO() {}
  /** Interface for the VConnection that owns this handle. */
  Continuation *get_continuation();
  void set_continuation(Continuation *cont);

  /**
    Set nbytes to be what is current available.

    Interface to set nbytes to be ndone + buffer.reader()->read_avail()
    if a reader is set.
  */
  void done();

  /**
    Determine the number of bytes remaining.

    Convenience function to determine how many bytes the operation
    has remaining.

    @return The number of bytes to be processed by the operation.

  */
  int64_t ntodo();

  /////////////////////
  // buffer settings //
  /////////////////////
  void set_writer(MIOBuffer *writer);
  void set_reader(IOBufferReader *reader);
  MIOBuffer *get_writer();
  IOBufferReader *get_reader();

  /**
    Reenable the IO operation.

    Interface that the state machine uses to reenable an I/O
    operation.  Reenable tells the VConnection that more data is
    available for the operation and that it should try to continue
    the operation in progress.  I/O operations become disabled when
    they can make no forward progress.  For a read this means that
    it's buffer is full. For a write, that it's buffer is empty.
    If reenable is called and progress is still not possible, it
    is ignored and no events are generated. However, unnecessary
    reenables (ones where no progress can be made) should be avoided
    as they hurt system throughput and waste CPU.

  */
  inkcoreapi void reenable();

  /**
    Reenable the IO operation.

    Interface that the state machine uses to reenable an I/O
    operation.  Reenable tells the VConnection that more data is
    available for the operation and that it should try to continue
    the operation in progress.  I/O operations become disabled when
    they can make no forward progress.  For a read this means that
    it's buffer is full. For a write, that it's buffer is empty.
    If reenable is called and progress is still not possible, it
    is ignored and no events are generated. However, unnecessary
    reenables (ones where no progress can be made) should be avoided
    as they hurt system throughput and waste CPU.

  */
  inkcoreapi void reenable_re();

  VIO(int aop);
  VIO();

  enum {
    NONE = 0,
    READ,
    WRITE,
    CLOSE,
    ABORT,
    SHUTDOWN_READ,
    SHUTDOWN_WRITE,
    SHUTDOWN_READWRITE,
    SEEK,
    PREAD,
    PWRITE,
    STAT,
  };

public:
  /**
    Continuation to callback.

    Used by the VConnection to store who is the continuation to
    call with events for this operation.

  */
  Continuation *cont;

  /**
    Number of bytes to be done for this operation.

    The total number of bytes this operation must complete.

  */
  int64_t nbytes;

  /**
    Number of bytes already completed.

    The number of bytes that already have been completed for the
    operation. Processor can update this value only if they hold
    the lock.

  */
  int64_t ndone;

  /**
    Type of operation.

    The type of operation that this VIO represents.

  */
  int op;

  /**
    Provides access to the reader or writer for this operation.

    Contains a pointer to the IOBufferReader if the operation is a
    write and a pointer to a MIOBuffer if the operation is a read.

  */
  MIOBufferAccessor buffer;

  /**
    Internal backpointer to the VConnection for use in the reenable
    functions.

  */
  VConnection *vc_server;

  /**
    Reference to the state machine's mutex.

    Maintains a reference to the state machine's mutex to allow
    processors to safely lock the operation even if the state machine
    has closed the VConnection and deallocated itself.

  */
  Ptr<ProxyMutex> mutex;
};

#include "I_VConnection.h"
