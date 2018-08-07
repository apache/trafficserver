/** @file

  Public VConnection declaration and associated declarations

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

#include "tscore/ink_platform.h"
#include "I_EventSystem.h"

#if !defined(I_VIO_h)
#error "include I_VIO.h"
#endif

#include <array>

static constexpr int TS_VCONN_MAX_USER_ARG = 4;

//
// Data Types
//
#define VCONNECTION_CACHE_DATA_BASE 0
#define VCONNECTION_NET_DATA_BASE 100
#define VCONNECTION_API_DATA_BASE 200

//
// Event signals
//

#define VC_EVENT_NONE EVENT_NONE

/** When a Continuation is first scheduled on a processor. */
#define VC_EVENT_IMMEDIATE EVENT_IMMEDIATE

#define VC_EVENT_READ_READY VC_EVENT_EVENTS_START

/**
  Any data in the associated buffer *will be written* when the
  Continuation returns.

*/
#define VC_EVENT_WRITE_READY (VC_EVENT_EVENTS_START + 1)

#define VC_EVENT_READ_COMPLETE (VC_EVENT_EVENTS_START + 2)
#define VC_EVENT_WRITE_COMPLETE (VC_EVENT_EVENTS_START + 3)

/**
  No more data (end of stream). It should be interpreted by a
  protocol engine as either a COMPLETE or ERROR.

*/
#define VC_EVENT_EOS (VC_EVENT_EVENTS_START + 4)

#define VC_EVENT_ERROR EVENT_ERROR

/**
  VC_EVENT_INACTIVITY_TIMEOUT indicates that the operation (read or write) has:
    -# been enabled for more than the inactivity timeout period
       (for a read, there has been space in the buffer)
       (for a write, there has been data in the buffer)
    -# no progress has been made
       (for a read, no data has been read from the connection)
       (for a write, no data has been written to the connection)

*/
#define VC_EVENT_INACTIVITY_TIMEOUT (VC_EVENT_EVENTS_START + 5)

/**
  Total time for some operation has been exceeded, regardless of any
  intermediate progress.

*/
#define VC_EVENT_ACTIVE_TIMEOUT (VC_EVENT_EVENTS_START + 6)

#define VC_EVENT_OOB_COMPLETE (VC_EVENT_EVENTS_START + 7)

//
// Event names
//

//
// VC_EVENT_READ_READ occurs when data *has been written* into
// the associated buffer.
//
// VC_EVENT_ERROR indicates that some error has occurred.  The
// "data" will be either 0 if the errno is unavailable or errno.
//
// VC_EVENT_INTERVAL indicates that an interval timer has expired.
//

//
// Event return codes
//
#define VC_EVENT_DONE CONTINUATION_DONE
#define VC_EVENT_CONT CONTINUATION_CONT

//////////////////////////////////////////////////////////////////////////////
//
//      Support Data Structures
//
//////////////////////////////////////////////////////////////////////////////

/** Used in VConnection::shutdown(). */
enum ShutdownHowTo_t {
  IO_SHUTDOWN_READ = 0,
  IO_SHUTDOWN_WRITE,
  IO_SHUTDOWN_READWRITE,
};

/** Used in VConnection::get_data(). */
enum TSApiDataType {
  TS_API_DATA_READ_VIO = VCONNECTION_API_DATA_BASE,
  TS_API_DATA_WRITE_VIO,
  TS_API_DATA_OUTPUT_VC,
  TS_API_DATA_CLOSED,
  TS_API_DATA_LAST ///< Used by other classes to extend the enum values.
};

extern "C" {
typedef struct tsapi_vio *TSVIO;
}

/**
  Base class for the connection classes that provide IO capabilities.

  The VConnection class is an abstract representation of a uni or
  bi-directional data conduit returned by a Processor. In a sense,
  they serve a similar purpose to file descriptors. A VConnection
  is a pure base class that defines methods to perform stream IO.
  It is also a Continuation that is called back from processors.

*/
class VConnection : public Continuation
{
public:
  ~VConnection() override;

  /**
    Read data from the VConnection.

    Called by a state machine to read data from the VConnection.
    Processors implementing read functionality take out lock, put
    new bytes on the buffer and call the continuation back before
    releasing the lock in order to enable the state machine to
    handle transfer schemes where the end of a given transaction
    is marked by a special character (ie: NNTP).

    <b>Possible Event Codes</b>

    On the callback to the continuation, the VConnection may use
    on of the following values for the event code:

    <table border="1">
      <tr>
        <td align="center"><b>Event code</b></td>
        <td align="center"><b>Meaning</b></td>
      </tr>
      <tr>
        <td>VC_EVENT_READ_READY</td>
        <td>Data has been added to the buffer or the buffer is full</td>
      </tr>
      <tr>
        <td>VC_EVENT_READ_COMPLETE</td>
        <td>The amount of data indicated by 'nbytes' has been read into the
            buffer</td>
      </tr>
      <tr>
        <td>VC_EVENT_EOS</td>
        <td>The stream being read from has been shutdown</td>
      </tr>
      <tr>
        <td>VC_EVENT_ERROR</td>
        <td>An error occurred during the read</td>
      </tr>
    </table>

    @param c Continuation to be called back with events.
    @param nbytes Number of bytes to read. If unknown, nbytes must
      be set to INT64_MAX.
    @param buf buffer to read into.
    @return VIO representing the scheduled IO operation.

  */
  virtual VIO *do_io_read(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) = 0;

  /**
    Write data to the VConnection.

    This method is called by a state machine to write data to the
    VConnection.

    <b>Possible Event Codes</b>

    On the callback to the continuation, the VConnection may use
    on of the following event codes:

    <table border="1">
      <tr>
        <td align="center"><b>Event code</b></td>
        <td align="center"><b>Meaning</b></td>
      </tr>
      <tr>
        <td>VC_EVENT_WRITE_READY</td>
        <td>Data was written from the reader or there are no bytes available
        for the reader to write.</td>
      </tr>
      <tr>
        <td>VC_EVENT_WRITE_COMPLETE</td>
        <td>The amount of data indicated by 'nbytes' has been written to the
            VConnection</td>
      </tr>
      <tr>
        <td>VC_EVENT_INACTIVITY_TIMEOUT</td>
        <td>No activity was performed for a certain period.</td>
      </tr>
      <tr>
        <td>VC_EVENT_ACTIVE_TIMEOUT</td>
        <td>Write operation continued beyond a time limit.</td>
      </tr>
      <tr>
        <td>VC_EVENT_ERROR</td>
        <td>An error occurred during the write</td>
      </tr>
    </table>

    @param c Continuation to be called back with events.
    @param nbytes Number of bytes to write. If unknown, nbytes must
      be set to INT64_MAX.
    @param buf Reader whose data is to be read from.
    @param owner
    @return VIO representing the scheduled IO operation.

  */
  virtual VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                           bool owner = false) = 0;

  /**
    Indicate that the VConnection is no longer needed.

    Once the state machine has finished using this VConnection, it
    must call this function to indicate that the VConnection can
    be deallocated.  After a close has been called, the VConnection
    and underlying processor must not send any more events related
    to this VConnection to the state machine. Likewise, the state
    machine must not access the VConnection or any VIOs obtained
    from it after calling this method.

    @param lerrno indicates where a close is a normal close or an
      abort. The difference between a normal close and an abort
      depends on the underlying type of the VConnection.

  */
  virtual void do_io_close(int lerrno = -1) = 0;

  /**
    Terminate one or both directions of the VConnection.

    Indicates that one or both sides of the VConnection should be
    terminated. After this call is issued, no further I/O can be
    done on the specified direction of the connection. The processor
    must not send any further events (including timeout events) to
    the state machine, and the state machine must not use any VIOs
    from a shutdown direction of the connection. Even if both sides
    of a connection are shutdown, the state machine must still call
    do_io_close() when it wishes the VConnection to be deallocated.

    <b>Possible howto values</b>

    <table border="1">
      <tr>
        <td align="center"><b>Value</b></td>
        <td align="center"><b>Meaning</b></td>
      </tr>
      <tr>
        <td>IO_SHUTDOWN_READ</td>
        <td>Indicates that this VConnection should not generate any more
        read events</td>
      </tr>
      <tr>
        <td>IO_SHUTDOWN_WRITE</td>
        <td>Indicates that this VConnection should not generate any more
        write events</td>
      </tr>
      <tr>
        <td>IO_SHUTDOWN_READWRITE</td>
        <td>Indicates that this VConnection should not generate any more
        read nor write events</td>
      </tr>
    </table>

    @param howto Specifies which direction of the VConnection to
      shutdown.

  */
  virtual void do_io_shutdown(ShutdownHowTo_t howto) = 0;

  VConnection(ProxyMutex *aMutex);
  VConnection(Ptr<ProxyMutex> &aMutex);

  // Private
  // Set continuation on a given vio. The public interface
  // is through VIO::set_continuation()
  virtual void set_continuation(VIO *vio, Continuation *cont);

  // Reenable a given vio.  The public interface is through VIO::reenable
  virtual void reenable(VIO *vio);
  virtual void reenable_re(VIO *vio);

  /**
    Convenience function to retrieve information from VConnection.

    This function is provided as a convenience for state machines
    to transmit information from/to a VConnection without breaking
    the VConnection abstraction. Its behavior varies depending on
    the type of VConnection being used.

    @param id Identifier associated to interpret the data field
    @param data Value or pointer with state machine or VConnection data.
    @return True if the operation is successful.

  */
  virtual bool
  get_data(int id, void *data)
  {
    (void)id;
    (void)data;
    return false;
  }

  /**
    Convenience function to set information into the VConnection.

    This function is provided as a convenience for state machines
    to transmit information from/to a VConnection without breaking
    the VConnection abstraction. Its behavior varies depending on
    the type of VConnection being used.

    @param id Identifier associated to interpret the data field.
    @param data Value or pointer with state machine or VConnection data.
    @return True if the operation is successful.

  */
  virtual bool
  set_data(int id, void *data)
  {
    (void)id;
    (void)data;
    return false;
  }

public:
  /**
    The error code from the last error.

    Indicates the last error on the VConnection. They are either
    system error codes or from the InkErrno.h file.

  */
  int lerrno;
};

/**
  Subclass of VConnection to provide support for user arguments

  Inherited by DummyVConnection (down to INKContInternal) and NetVConnection
*/
class AnnotatedVConnection : public VConnection
{
  using self_type  = AnnotatedVConnection;
  using super_type = VConnection;

public:
  AnnotatedVConnection(ProxyMutex *aMutex) : super_type(aMutex){};
  AnnotatedVConnection(Ptr<ProxyMutex> &aMutex) : super_type(aMutex){};

  void *
  get_user_arg(unsigned ix) const
  {
    ink_assert(ix < user_args.size());
    return this->user_args[ix];
  };
  void
  set_user_arg(unsigned ix, void *arg)
  {
    ink_assert(ix < user_args.size());
    user_args[ix] = arg;
  };

protected:
  std::array<void *, TS_VCONN_MAX_USER_ARG> user_args{{nullptr}};
};

struct DummyVConnection : public AnnotatedVConnection {
  VIO *
  do_io_write(Continuation * /* c ATS_UNUSED */, int64_t /* nbytes ATS_UNUSED */, IOBufferReader * /* buf ATS_UNUSED */,
              bool /* owner ATS_UNUSED */) override
  {
    ink_assert(!"VConnection::do_io_write -- "
                "cannot use default implementation");
    return nullptr;
  }

  VIO *
  do_io_read(Continuation * /* c ATS_UNUSED */, int64_t /* nbytes ATS_UNUSED */, MIOBuffer * /* buf ATS_UNUSED */) override
  {
    ink_assert(!"VConnection::do_io_read -- "
                "cannot use default implementation");
    return nullptr;
  }

  void
  do_io_close(int /* alerrno ATS_UNUSED */) override
  {
    ink_assert(!"VConnection::do_io_close -- "
                "cannot use default implementation");
  }

  void do_io_shutdown(ShutdownHowTo_t /* howto ATS_UNUSED */) override
  {
    ink_assert(!"VConnection::do_io_shutdown -- "
                "cannot use default implementation");
  }

  DummyVConnection(ProxyMutex *m) : AnnotatedVConnection(m) {}
};
