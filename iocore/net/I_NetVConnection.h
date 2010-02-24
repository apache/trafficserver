/** @file

  This file implements an I/O Processor for network I/O

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

#ifndef __NETVCONNECTION_H__
#define __NETVCONNECTION_H__

#include "I_Action.h"
#include "I_VConnection.h"
#include "I_Event.h"
#include "List.h"
#include "I_IOBuffer.h"
#include "I_Socks.h"

// #define WITH_DETAILED_VCONNECTION_LOGGING 1

#if WITH_DETAILED_VCONNECTION_LOGGING
#include "DetailedLog.h"
#endif

#define CONNECT_SUCCESS   1
#define CONNECT_FAILURE   0

#define SSL_EVENT_SERVER 0
#define SSL_EVENT_CLIENT 1

enum NetDataType
{
  NET_DATA_ATTRIBUTES = VCONNECTION_NET_DATA_BASE
};

/* can this be moved to NT files */
enum
{ IOCORE_NETVC_MAGIC_ALIVE = 0x0000BEEF,
  IOCORE_NETVC_MAGIC_DEAD = 0xDEADBEEF
};

/**
  Holds user options for NetVConnection. This class holds various
  options a user can specify for NetVConnection. Right now this
  passed when we invoke NetProcessor::connect_re().

  Currently defined fields:

  <table>
    <tr><td><b>Field</b></td><td><b>Description</b></td></tr>
    <tr>
      <td><i>local_port</i></td>
      <td>Specifies local port to bind to before connecting</td>
    </tr>
    <tr>
      <td><i>spoof_ip</i></td>
      <td>IP address to spoof instead of our local address If
        <i>spoof_src_ip</i> is set, <i>spoof_src_port</i> should be
        set as well. Spcified in network byte order</td>
    </tr>
    <tr>
      <td><i>spoof_port</i></td>
      <td>Same as <i>spoof_src_ip</i>. Specified in host byte order</td>
    </tr>
    <tr>
      <td><i>socks_version</i></td>
      <td>Set explicit version for Socks</td>
    </tr>
    <tr>
      <td><i>socks_support</i></td>
      <td>Explicitly set to NO_SOCKS to disable SOCKS for the
        connection. Default is to use SOCKS if configiration enables
        SOCKS.</td>
    </tr>
  </table>

  User only needs to set only the option she is interested in--the
  rest get sensible default values.

*/
class NetVCOptions
{

public:
  int local_port;

  inku32 spoof_ip;
  int spoof_port;

  unsigned char socks_support;
  unsigned char socks_version;

  int socket_recv_bufsize;
  int socket_send_bufsize;
  unsigned long sockopt_flags;

  EventType etype;

  void reset();
  void set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags);


    NetVCOptions()
  {
    reset();
  }

  /* Add more options here instead of adding them to connect_re() args etc */
};

/**
  A VConnection for a network socket. Abstraction for a net connection.
  Similar to a socket descriptor VConnections are IO handles to
  streams. In one sense, they serve a purpose similar to file
  descriptors. Unlike file descriptors, VConnections allow for a
  stream IO to be done based on a single read or write call.

*/
class NetVConnection:public VConnection
{
public:

  /**
     Initiates read. Thread safe, may be called when not handling
     an event from the NetVConnection, or the NetVConnection creation
     callback.

    Callbacks: non-reentrant, c's lock taken during callbacks.

    <table>
      <tr><td>c->handleEvent(VC_EVENT_READ_READY, vio)</td><td>data added to buffer</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_READ_COMPLETE, vio)</td><td>finished reading nbytes of data</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_EOS, vio)</td><td>the stream has been shutdown</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_ERROR, vio)</td><td>error</td></tr>
    </table>

    The vio returned during callbacks is the same as the one returned
    by do_io_read(). The vio can be changed only during call backs
    from the vconnection.

    @param c continuation to be called back after (partial) read
    @param nbytes no of bytes to read, if unknown set to INT_MAX
    @param buf buffer to put the data into
    @return vio

  */
  virtual VIO * do_io_read(Continuation * c, ink64 nbytes, MIOBuffer * buf) = 0;

  /**
    Initiates write. Thread-safe, may be called when not handling
    an event from the NetVConnection, or the NetVConnection creation
    callback.

    Callbacks: non-reentrant, c's lock taken during callbacks.

    <table>
      <tr>
        <td>c->handleEvent(VC_EVENT_WRITE_READY, vio)</td>
        <td>signifies data has written from the reader or there are no bytes available for the reader to write.</td>
      </tr>
      <tr>
        <td>c->handleEvent(VC_EVENT_WRITE_COMPLETE, vio)</td>
        <td>signifies the amount of data indicated by nbytes has been read from the buffer</td>
      </tr>
      <tr>
        <td>c->handleEvent(VC_EVENT_ERROR, vio)</td>
        <td>signified that error occured during write.</td>
      </tr>
    </table>
   
    The vio returned during callbacks is the same as the one returned
    by do_io_write(). The vio can be changed only during call backs
    from the vconnection. The vconnection deallocates the reader
    when it is destroyed.

    @param c continuation to be called back after (partial) write
    @param nbytes no of bytes to write, if unknown msut be set to INT_MAX
    @param buf source of data
    @param owner
    @return vio pointer

  */
  virtual VIO *do_io_write(Continuation * c, ink64 nbytes, IOBufferReader * buf, bool owner = false) = 0;

  /**
    Closes the vconnection. A state machine MUST call do_io_close()
    when it has finished with a VConenction. do_io_close() indicates
    that the VConnection can be deallocated. After a close has been
    called, the VConnection and underlying processor must NOT send
    any more events related to this VConnection to the state machine.
    Likeswise, state machine must not access the VConnectuion or
    any returned VIOs after calling close. lerrno indicates whether
    a close is a normal close or an abort. The difference between
    a normal close and an abort depends on the underlying type of
    the VConnection. Passing VIO::CLOSE for lerrno indicates a
    normal close while passing VIO::ABORT indicates an abort.

    @param lerrno VIO:CLOSE for regular close or VIO::ABORT for aborts

  */
  virtual void do_io_close(int lerrno = -1) = 0;

  /** 
    Shuts down read side, write side, or both. do_io_shutdown() can
    be used to terminate one or both sides of the VConnection. The
    howto is one of IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE,
    IO_SHUTDOWN_READWRITE. Once a side of a VConnection is shutdown,
    no further I/O can be done on that side of the connections and
    the underlying processor MUST NOT send any further events
    (INCLUDING TIMOUT EVENTS) to the state machine. The state machine
    MUST NOT use any VIOs from a shutdown side of a connection.
    Even if both sides of a connection are shutdown, the state
    machine MUST still call do_io_close() when it wishes the
    VConnection to be deallocated.

    @param howto IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE, IO_SHUTDOWN_READWRITE

  */
  virtual void do_io_shutdown(ShutdownHowTo_t howto) = 0;


  /** 
    Sends out of band messages over the connection. This function
    is used to send out of band messages (Ctrl-C in ftp for instance).
    cont is called back with VC_EVENT_OOB_COMPLETE - on successful
    send or VC_EVENT_EOS - if the other side has shutdown the
    connection. These callbacks could be re-entrant. Only one
    send_OOB can be in progress at any time for a VC.

    @param cont to be called back with events.
    @param buf message buffer.
    @param len length of the message.

  */
  virtual Action *send_OOB(Continuation * cont, char *buf, int len);

  /**
    Cancels a scheduled send_OOB. Part of the message could have
    been sent already. Not callbacks to the cont are made after
    this call. The Action returned by send_OOB should not be accessed
    after cancel_OOB.

  */
  virtual void cancel_OOB();

  ////////////////////////////////////////////////////////////
  // Set the timeouts associated with this connection.      //
  // active_timeout is for the total elasped time of        //
  // the connection.                                        //
  // inactivity_timeout is the elapsed time from the time   //
  // a read or a write was scheduled during which the       //
  // connection  was unable to sink/provide data.           //
  // calling these functions repeatedly resets the timeout. //
  // These functions are NOT THREAD-SAFE, and may only be   //
  // called when handing an  event from this NetVConnection,//
  // or the NetVConnection creation callback.               //
  ////////////////////////////////////////////////////////////

  /** 
    Sets time after which SM should be notified.

    Sets the amount of time (in nanoseconds) after which the state
    machine using the NetVConnection should receive a
    VC_EVENT_ACTIVE_TIMEOUT event. The timeout is value is ignored
    if neither the read side nor the write side of the connection
    is currently active. The timer is reset if the function is
    called repeatedly This call can be used by SMs to make sure
    that it does not keep any connections open for a really long
    time.

    Timeout symantics:

    Should a timeout occur, the state machine for the read side of
    the NetVConnection is signaled first assuming that a read has
    been initiated on the NetVConnection and that the read side of
    the NetVConnection has not been shutdown. Should either of the
    two conditions not be met, the NetProcessor will attempt to
    signal the write side. If a timeout is sent to the read side
    state machine and its handler, return EVENT_DONE, a timeout
    will not be sent to the write side. Should the return from the
    handler not be EVENT_DONE and the write side state machine is
    different (in terms of pointer comparison) from the read side
    state machine, the NetProcessor will try to signal the write
    side state machine as well. To signal write side, a write must
    have been initiated on it and the write must not have been
    shutdown.

    Receiving a timeout is only a notification that the timer has
    expired. The NetVConnection is still usable. Further timeouts
    of the type signaled will not be generated unless the timeout
    is reset via the set_active_timeout() or set_inactivity_timeout()
    interfaces.

  */
  virtual void set_active_timeout(ink_hrtime timeout_in) = 0;

  /** 
    Sets time after which SM should be notified if the requested
    IO could not be performed. Sets the amount of time (in nanoseconds),
    if the NetVConnection is idle on both the read or write side,
    after which the state machine using the NetVConnection should
    receive a VC_EVENT_INACTIVITY_TIMEOUT event. Either read or
    write traffic will cause timer to be reset. Calling this function
    again also resets the timer. The timeout is value is ignored
    if neither the read side nor the write side of the connection
    is currently active. See section on timeout semantics above.

   */
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;

  /** 
    Clears the active timeout. No active timeouts will be sent until
    set_active_timeout() is used to reset the active timeout.

  */
  virtual void cancel_active_timeout() = 0;

  /** 
    Clears the inactivity timeout. No inactivity timeouts will be
    sent until set_inactivity_timeout() is used to reset the
    inactivity timeout.

  */
  virtual void cancel_inactivity_timeout() = 0;

  /** @return the current active_timeout value in nanosecs */
  virtual ink_hrtime get_active_timeout() = 0;

  /** @return current inactivity_timeout value in nanosecs */
  virtual ink_hrtime get_inactivity_timeout() = 0;

  /**
    Boosts the priority of the VConnection. Used by SM to indicate
    that the connection is likely to be actively used soon.

  */
  virtual void boost();

  /** Returns local sockaddr in. */
  const struct sockaddr_in &get_local_addr();

  /** Returns local ip. */
  unsigned int get_local_ip();

  /** Returns local port. */
  int get_local_port();

  /** Returns remote sockaddr in. */
  const struct sockaddr_in &get_remote_addr();

  /** Returns remote ip. */
  unsigned int get_remote_ip();

  /** Returns remote port. */
  int get_remote_port();

  /** Structure holding user options. */
  NetVCOptions options;

  //
  // Private
  //

  //The following variable used to obtain host addr when transparency
  //is enabled by SocksProxy
  SocksAddrType socks_addr;

  unsigned int attributes;
  EThread *thread;

  /// PRIVATE: The public interface is VIO::reenable()
  virtual void reenable(VIO * vio) = 0;

  /// PRIVATE: The public interface is VIO::reenable()
  virtual void reenable_re(VIO * vio) = 0;

  /// PRIVATE
    virtual ~ NetVConnection();

  /**
    PRIVATE: instances of NetVConnection cannot be created directly
    by the state machines. The objects are created by NetProcessor
    calls like accept connect_re() etc. The constructor is public
    just to avoid compile errors.

  */
    NetVConnection();

  /// PRIVATE: SSL specific stuff 
  virtual bool is_over_ssl() = 0;

  /// NOT IMPLEMENTED
  virtual SOCKET get_socket();

  /// NOT IMPLEMENTED
  virtual int get_last_error();

  /** Set local sock addr struct. */
  virtual void set_local_addr() = 0;

  /** Set remote sock addr struct. */
  virtual void set_remote_addr() = 0;

#if WITH_DETAILED_VCONNECTION_LOGGING
  void loggingInit()
  {
    if (logging == NULL) {
      logging = new DetailedLog();
    }
  }
  void addLogMessage(const char *message)
  {
    if (logging != NULL) {
      logging->add(message);
      logging->print();
    }
  }
  void printLogs() const
  {
    if (logging != NULL)
    {
      logging->print();
    }
  }
  void clearLogs()
  {
    if (logging != NULL) {
      logging->clear();
    }
  }
  ink_hrtime getLogsTotalTime() const
  {
    if (logging != NULL)
    {
      return logging->totalTime();
    } else
    {
      return 0;
    }
  }
  bool loggingEnabled() const
  {
    return (logging != NULL);
  }
  DetailedLog *logging;
#else
  void addLogMessage(const char *message) {}
  void loggingInit() {}
  bool loggingEnabled() const { return false; }
  ink_hrtime getLogsTotalTime() const { return 0; }
  void printLogs() const {}
  void clearLogs() {}
#endif

private:
  NetVConnection(const NetVConnection &);
  NetVConnection & operator =(const NetVConnection &);

protected:
  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;

  int got_local_addr;
  int got_remote_addr;
};

inline
NetVConnection::NetVConnection():
VConnection(NULL),
attributes(0),
thread(NULL),
#if WITH_DETAILED_VCONNECTION_LOGGING
logging(NULL),
#endif
got_local_addr(0),
got_remote_addr(0)
{
  memset(&local_addr, 0, sizeof(local_addr));
  memset(&remote_addr, 0, sizeof(remote_addr));
}

#if defined (_IOCORE_WIN32_WINNT)
#include "NTNetVConnection.h"
#endif

#endif
