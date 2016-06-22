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

/****************************************************************************


  This file implements an I/O Processor for network I/O on Unix.


 ****************************************************************************/

#ifndef __P_UNIXNETVCONNECTION_H__
#define __P_UNIXNETVCONNECTION_H__

#include "ts/ink_sock.h"
#include "I_NetVConnection.h"
#include "P_UnixNetState.h"
#include "P_Connection.h"

class UnixNetVConnection;
class NetHandler;
struct PollDescriptor;

TS_INLINE void
NetVCOptions::reset()
{
  ip_proto  = USE_TCP;
  ip_family = AF_INET;
  local_ip.invalidate();
  local_port         = 0;
  addr_binding       = ANY_ADDR;
  f_blocking         = false;
  f_blocking_connect = false;
  socks_support      = NORMAL_SOCKS;
  socks_version      = SOCKS_DEFAULT_VERSION;
  socket_recv_bufsize =
#if defined(RECV_BUF_SIZE)
    RECV_BUF_SIZE;
#else
    0;
#endif
  socket_send_bufsize = 0;
  sockopt_flags       = 0;
  packet_mark         = 0;
  packet_tos          = 0;

  etype = ET_NET;

  sni_servername = NULL;
}

TS_INLINE void
NetVCOptions::set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags, unsigned long _packet_mark,
                             unsigned long _packet_tos)
{
  socket_recv_bufsize = _recv_bufsize;
  socket_send_bufsize = _send_bufsize;
  sockopt_flags       = _opt_flags;
  packet_mark         = _packet_mark;
  packet_tos          = _packet_tos;
}

struct OOB_callback : public Continuation {
  char *data;
  int length;
  Event *trigger;
  UnixNetVConnection *server_vc;
  Continuation *server_cont;
  int retry_OOB_send(int, Event *);

  OOB_callback(ProxyMutex *m, NetVConnection *vc, Continuation *cont, char *buf, int len)
    : Continuation(m), data(buf), length(len), trigger(0)
  {
    server_vc   = (UnixNetVConnection *)vc;
    server_cont = cont;
    SET_HANDLER(&OOB_callback::retry_OOB_send);
  }
};

class UnixNetVConnection : public NetVConnection
{
public:
  virtual VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf);
  virtual VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false);

  virtual bool get_data(int id, void *data);

  virtual Action *send_OOB(Continuation *cont, char *buf, int len);
  virtual void cancel_OOB();

  virtual void
  setSSLHandshakeWantsRead(bool /* flag */)
  {
    return;
  }
  virtual bool
  getSSLHandshakeWantsRead()
  {
    return false;
  }
  virtual void
  setSSLHandshakeWantsWrite(bool /* flag */)
  {
    return;
  }

  virtual bool
  getSSLHandshakeWantsWrite()
  {
    return false;
  }

  virtual void do_io_close(int lerrno = -1);
  virtual void do_io_shutdown(ShutdownHowTo_t howto);

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
  virtual void set_active_timeout(ink_hrtime timeout_in);
  virtual void set_inactivity_timeout(ink_hrtime timeout_in);
  virtual void cancel_active_timeout();
  virtual void cancel_inactivity_timeout();
  virtual void set_action(Continuation *c);
  virtual void add_to_keep_alive_queue();
  virtual void remove_from_keep_alive_queue();
  virtual bool add_to_active_queue();
  virtual void remove_from_active_queue();

  // The public interface is VIO::reenable()
  virtual void reenable(VIO *vio);
  virtual void reenable_re(VIO *vio);

  virtual SOCKET get_socket();

  virtual ~UnixNetVConnection();

  /////////////////////////////////////////////////////////////////
  // instances of UnixNetVConnection should be allocated         //
  // only from the free list using UnixNetVConnection::alloc().  //
  // The constructor is public just to avoid compile errors.      //
  /////////////////////////////////////////////////////////////////
  UnixNetVConnection();

private:
  UnixNetVConnection(const NetVConnection &);
  UnixNetVConnection &operator=(const NetVConnection &);

public:
  /////////////////////////
  // UNIX implementation //
  /////////////////////////
  void set_enabled(VIO *vio);

  void get_local_sa();

  // these are not part of the pure virtual interface.  They were
  // added to reduce the amount of duplicate code in classes inherited
  // from NetVConnection (SSL).
  virtual int
  sslStartHandShake(int event, int &err)
  {
    (void)event;
    (void)err;
    return EVENT_ERROR;
  }
  virtual bool
  getSSLHandShakeComplete()
  {
    return (true);
  }
  virtual bool
  getSSLClientConnection()
  {
    return (false);
  }
  virtual void
  setSSLClientConnection(bool state)
  {
    (void)state;
  }
  virtual void net_read_io(NetHandler *nh, EThread *lthread);
  virtual int64_t load_buffer_and_write(int64_t towrite, int64_t &wattempted, int64_t &total_written, MIOBufferAccessor &buf,
                                        int &needs);
  void readDisable(NetHandler *nh);
  void readSignalError(NetHandler *nh, int err);
  int readSignalDone(int event, NetHandler *nh);
  int readSignalAndUpdate(int event);
  void readReschedule(NetHandler *nh);
  void writeReschedule(NetHandler *nh);
  void netActivity(EThread *lthread);
  /**
   * If the current object's thread does not match the t argument, create a new
   * NetVC in the thread t context based on the socket and ssl information in the
   * current NetVC and mark the current NetVC to be closed.
   */
  UnixNetVConnection *migrateToCurrentThread(Continuation *c, EThread *t);

  Action action_;
  volatile int closed;
  NetState read;
  NetState write;

  LINK(UnixNetVConnection, cop_link);
  LINKM(UnixNetVConnection, read, ready_link)
  SLINKM(UnixNetVConnection, read, enable_link)
  LINKM(UnixNetVConnection, write, ready_link)
  SLINKM(UnixNetVConnection, write, enable_link)
  LINK(UnixNetVConnection, keep_alive_queue_link);
  LINK(UnixNetVConnection, active_queue_link);

  ink_hrtime inactivity_timeout_in;
  ink_hrtime active_timeout_in;
#ifdef INACTIVITY_TIMEOUT
  Event *inactivity_timeout;
  Event *activity_timeout;
#else
  ink_hrtime next_inactivity_timeout_at;
  ink_hrtime next_activity_timeout_at;
#endif

  EventIO ep;
  NetHandler *nh;
  unsigned int id;
  // amc - what is this for? Why not use remote_addr or con.addr?
  IpEndpoint server_addr; /// Server address and port.

  union {
    unsigned int flags;
#define NET_VC_SHUTDOWN_READ 1
#define NET_VC_SHUTDOWN_WRITE 2
    struct {
      unsigned int got_local_addr : 1;
      unsigned int shutdown : 2;
    } f;
  };

  Connection con;
  int recursion;
  ink_hrtime submit_time;
  OOB_callback *oob_ptr;
  bool from_accept_thread;

  // es - origin_trace associated connections
  bool origin_trace;
  const sockaddr *origin_trace_addr;
  int origin_trace_port;

  int startEvent(int event, Event *e);
  int acceptEvent(int event, Event *e);
  int mainEvent(int event, Event *e);
  virtual int connectUp(EThread *t, int fd);
  /**
   * Populate the current object based on the socket information in in the
   * con parameter.
   * This is logic is invoked when the NetVC object is created in a new thread context
   */
  virtual int populate(Connection &con, Continuation *c, void *arg);
  virtual void free(EThread *t);

  virtual ink_hrtime get_inactivity_timeout();
  virtual ink_hrtime get_active_timeout();

  virtual void set_local_addr();
  virtual void set_remote_addr();
  virtual int set_tcp_init_cwnd(int init_cwnd);
  virtual int set_tcp_congestion_control(const char *name, int len);
  virtual void apply_options();

  friend void write_to_net_io(NetHandler *, UnixNetVConnection *, EThread *);

  void
  setOriginTrace(bool t)
  {
    origin_trace = t;
  }

  void
  setOriginTraceAddr(const sockaddr *addr)
  {
    origin_trace_addr = addr;
  }

  void
  setOriginTracePort(int port)
  {
    origin_trace_port = port;
  }
};

extern ClassAllocator<UnixNetVConnection> netVCAllocator;

typedef int (UnixNetVConnection::*NetVConnHandler)(int, void *);

TS_INLINE void
UnixNetVConnection::set_remote_addr()
{
  ats_ip_copy(&remote_addr, &con.addr);
}

TS_INLINE void
UnixNetVConnection::set_local_addr()
{
  int local_sa_size = sizeof(local_addr);
  safe_getsockname(con.fd, &local_addr.sa, &local_sa_size);
}

TS_INLINE ink_hrtime
UnixNetVConnection::get_active_timeout()
{
  return active_timeout_in;
}

TS_INLINE ink_hrtime
UnixNetVConnection::get_inactivity_timeout()
{
  return inactivity_timeout_in;
}

TS_INLINE void
UnixNetVConnection::set_inactivity_timeout(ink_hrtime timeout)
{
  Debug("socket", "Set inactive timeout=%" PRId64 ", for NetVC=%p", timeout, this);
  inactivity_timeout_in = timeout;
#ifdef INACTIVITY_TIMEOUT

  if (inactivity_timeout)
    inactivity_timeout->cancel_action(this);
  if (inactivity_timeout_in) {
    if (read.enabled) {
      ink_assert(read.vio.mutex->thread_holding == this_ethread() && thread);
      if (read.vio.mutex->thread_holding == thread)
        inactivity_timeout = thread->schedule_in_local(this, inactivity_timeout_in);
      else
        inactivity_timeout = thread->schedule_in(this, inactivity_timeout_in);
    } else if (write.enabled) {
      ink_assert(write.vio.mutex->thread_holding == this_ethread() && thread);
      if (write.vio.mutex->thread_holding == thread)
        inactivity_timeout = thread->schedule_in_local(this, inactivity_timeout_in);
      else
        inactivity_timeout = thread->schedule_in(this, inactivity_timeout_in);
    } else
      inactivity_timeout = 0;
  } else
    inactivity_timeout = 0;
#else
  if (timeout) {
    next_inactivity_timeout_at = Thread::get_hrtime() + timeout;
  } else {
    next_inactivity_timeout_at = 0;
  }
#endif
}

TS_INLINE void
UnixNetVConnection::set_active_timeout(ink_hrtime timeout)
{
  Debug("socket", "Set active timeout=%" PRId64 ", NetVC=%p", timeout, this);
  active_timeout_in = timeout;
#ifdef INACTIVITY_TIMEOUT
  if (active_timeout)
    active_timeout->cancel_action(this);
  if (active_timeout_in) {
    if (read.enabled) {
      ink_assert(read.vio.mutex->thread_holding == this_ethread() && thread);
      if (read.vio.mutex->thread_holding == thread)
        active_timeout = thread->schedule_in_local(this, active_timeout_in);
      else
        active_timeout = thread->schedule_in(this, active_timeout_in);
    } else if (write.enabled) {
      ink_assert(write.vio.mutex->thread_holding == this_ethread() && thread);
      if (write.vio.mutex->thread_holding == thread)
        active_timeout = thread->schedule_in_local(this, active_timeout_in);
      else
        active_timeout = thread->schedule_in(this, active_timeout_in);
    } else
      active_timeout = 0;
  } else
    active_timeout = 0;
#else
  next_activity_timeout_at   = Thread::get_hrtime() + timeout;
#endif
}

TS_INLINE void
UnixNetVConnection::cancel_inactivity_timeout()
{
  Debug("socket", "Cancel inactive timeout for NetVC=%p", this);
  inactivity_timeout_in = 0;
#ifdef INACTIVITY_TIMEOUT
  if (inactivity_timeout) {
    Debug("socket", "Cancel inactive timeout for NetVC=%p", this);
    inactivity_timeout->cancel_action(this);
    inactivity_timeout = NULL;
  }
#else
  next_inactivity_timeout_at = 0;
#endif
}

TS_INLINE void
UnixNetVConnection::cancel_active_timeout()
{
  Debug("socket", "Cancel active timeout for NetVC=%p", this);
  active_timeout_in = 0;
#ifdef INACTIVITY_TIMEOUT
  if (active_timeout) {
    Debug("socket", "Cancel active timeout for NetVC=%p", this);
    active_timeout->cancel_action(this);
    active_timeout = NULL;
  }
#else
  next_activity_timeout_at   = 0;
#endif
}

TS_INLINE int
UnixNetVConnection::set_tcp_init_cwnd(int init_cwnd)
{
#ifdef TCP_INIT_CWND
  int rv;
  uint32_t val = init_cwnd;
  rv           = setsockopt(con.fd, IPPROTO_TCP, TCP_INIT_CWND, &val, sizeof(val));
  Debug("socket", "Setting TCP initial congestion window (%d) -> %d", init_cwnd, rv);
  return rv;
#else
  Debug("socket", "Setting TCP initial congestion window %d -> unsupported", init_cwnd);
  return -1;
#endif
}

TS_INLINE int
UnixNetVConnection::set_tcp_congestion_control(const char *name, int len)
{
#ifdef TCP_CONGESTION
  int rv = 0;
  rv     = setsockopt(con.fd, IPPROTO_TCP, TCP_CONGESTION, reinterpret_cast<void *>(const_cast<char *>(name)), len);
  if (rv < 0) {
    Error("Unable to set TCP congestion control on socket %d to \"%.*s\", errno=%d (%s)", con.fd, len, name, errno,
          strerror(errno));
  } else {
    Debug("socket", "Setting TCP congestion control on socket [%d] to \"%.*s\" -> %d", con.fd, len, name, rv);
  }
  return rv;
#else
  Debug("socket", "Setting TCP congestion control %.*s is not supported on this platform.", len, name);
  return -1;
#endif
}

TS_INLINE UnixNetVConnection::~UnixNetVConnection()
{
}

TS_INLINE SOCKET
UnixNetVConnection::get_socket()
{
  return con.fd;
}

TS_INLINE void
UnixNetVConnection::set_action(Continuation *c)
{
  action_ = c;
}

// declarations for local use (within the net module)

void close_UnixNetVConnection(UnixNetVConnection *vc, EThread *t);
void write_to_net(NetHandler *nh, UnixNetVConnection *vc, EThread *thread);
void write_to_net_io(NetHandler *nh, UnixNetVConnection *vc, EThread *thread);

#endif
