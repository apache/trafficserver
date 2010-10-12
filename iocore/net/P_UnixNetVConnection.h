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

#include "ink_sock.h"
#include "I_NetVConnection.h"
#include "P_UnixNetState.h"
#include "P_Connection.h"

class UnixNetVConnection;
class NetHandler;
struct PollDescriptor;

TS_INLINE void
NetVCOptions::reset()
{
  ip_proto = USE_TCP;
  local_port = 0;
  port_binding = ANY_PORT;
  local_addr = 0;
  addr_binding = ANY_ADDR;
  f_blocking = false;
  f_blocking_connect = false;
  socks_support = NORMAL_SOCKS;
  socks_version = SOCKS_DEFAULT_VERSION;
  socket_recv_bufsize = 
#if defined(RECV_BUF_SIZE)
    RECV_BUF_SIZE;
#else
    0;
#endif
  socket_send_bufsize = 0;
  sockopt_flags = 0;
  etype = ET_NET;
}

TS_INLINE void
NetVCOptions::set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags)
{
  socket_recv_bufsize = _recv_bufsize;
  socket_send_bufsize = _send_bufsize;
  sockopt_flags = _opt_flags;
}

struct OOB_callback:public Continuation
{
  char *data;
  int length;
  Event *trigger;
  UnixNetVConnection *server_vc;
  Continuation *server_cont;
  int retry_OOB_send(int, Event *);

    OOB_callback(ProxyMutex *m, NetVConnection *vc, Continuation *cont,
                 char *buf, int len):Continuation(m), data(buf), length(len), trigger(0)
  {
    server_vc = (UnixNetVConnection *) vc;
    server_cont = cont;
    SET_HANDLER(&OOB_callback::retry_OOB_send);
  }
};

class UnixNetVConnection:public NetVConnection
{
public:

  virtual VIO *do_io_read(Continuation *c, int64 nbytes, MIOBuffer *buf);
  virtual VIO *do_io_write(Continuation *c, int64 nbytes, IOBufferReader *buf, bool owner = false);

  virtual Action *send_OOB(Continuation *cont, char *buf, int len);
  virtual void cancel_OOB();

  virtual void setSSLHandshakeWantsRead(bool flag) { NOWARN_UNUSED(flag); return; }
  virtual bool getSSLHandshakeWantsRead() { return false; }
  virtual void setSSLHandshakeWantsWrite(bool flag) { NOWARN_UNUSED(flag); return; }

  virtual bool getSSLHandshakeWantsWrite() { return false; }

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

  // The public interface is VIO::reenable()
  virtual void reenable(VIO *vio);
  virtual void reenable_re(VIO *vio);

  virtual SOCKET get_socket();

  virtual ~ UnixNetVConnection();

  /////////////////////////////////////////////////////////////////
  // instances of UnixNetVConnection should be allocated         //
  // only from the free list using UnixNetVConnection::alloc().  //
  // The constructor is public just to avoid compile errors.      //
  /////////////////////////////////////////////////////////////////
  UnixNetVConnection();

private:
  UnixNetVConnection(const NetVConnection &);
  UnixNetVConnection & operator =(const NetVConnection &);

public:

  /////////////////////////
  // UNIX implementation //
  /////////////////////////
  void set_enabled(VIO *vio);

  void get_local_sa();

  // these are not part of the pure virtual interface.  They were
  // added to reduce the amount of duplicate code in classes inherited
  // from NetVConnection (SSL).
  virtual int sslStartHandShake(int event, int &err) {
    (void) event;
    (void) err;
    return EVENT_ERROR;
  }
  virtual bool getSSLHandShakeComplete() {
    return (true);
  }
  virtual bool getSSLClientConnection()
  {
    return (false);
  }
  virtual void setSSLClientConnection(bool state)
  {
    (void) state;
  }
  virtual void net_read_io(NetHandler *nh, EThread *lthread);
  virtual int64 load_buffer_and_write(int64 towrite, int64 &wattempted, int64 &total_wrote, MIOBufferAccessor & buf);
  void readTempPriority(NetHandler *nh, int priority);
  void readDisable(NetHandler *nh);
  void readSignalError(NetHandler *nh, int err);
  void readSetPriority(NetHandler *nh, int priority);
  void readUpdatePriority(NetHandler *nh, int bytesRead, int nBytes, EThread *lthread);
  int readSignalDone(int event, NetHandler *nh);
  int readSignalAndUpdate(int event);
  void readReschedule(NetHandler *nh);
  void writeReschedule(NetHandler *nh);
  void netActivity(EThread *lthread);

  Action *action()
  {
    return &action_;
  }

  Action action_;
  volatile int closed;
  NetState read;
  NetState write;

  LINKM(UnixNetVConnection, read, ready_link)
  SLINKM(UnixNetVConnection, read, enable_link)
  LINKM(UnixNetVConnection, write, ready_link)
  SLINKM(UnixNetVConnection, write, enable_link)

  ink_hrtime inactivity_timeout_in;
  ink_hrtime active_timeout_in;
#ifdef INACTIVITY_TIMEOUT
  Event *inactivity_timeout;
#else
  ink_hrtime next_inactivity_timeout_at;
#endif
  Event *active_timeout;
  EventIO ep;
  NetHandler *nh;
  unsigned int id;
  unsigned int ip;
  //  unsigned int _interface; // 'interface' conflicts with the C++ keyword
  int accept_port;
  int port;

  union
  {
    unsigned int flags;
#define NET_VC_SHUTDOWN_READ  1
#define NET_VC_SHUTDOWN_WRITE 2
    struct
    {
      unsigned int got_local_sa:1;
      unsigned int shutdown:2;
    } f;
  };
  struct sockaddr_in local_sa;

  Connection con;
  int recursion;
  ink_hrtime submit_time;
  OOB_callback *oob_ptr;

  int startEvent(int event, Event *e);
  int acceptEvent(int event, Event *e);
  int mainEvent(int event, Event *e);
  virtual int connectUp(EThread *t);
  virtual void free(EThread *t);

  virtual ink_hrtime get_inactivity_timeout();
  virtual ink_hrtime get_active_timeout();

  virtual void set_local_addr();
  virtual void set_remote_addr();
};

extern ClassAllocator<UnixNetVConnection> netVCAllocator;

typedef int (UnixNetVConnection::*NetVConnHandler) (int, void *);


TS_INLINE void
UnixNetVConnection::set_remote_addr()
{
  remote_addr.sin_family = con.sa.sin_family;
  remote_addr.sin_port = htons(port);
  remote_addr.sin_addr.s_addr = ip;
}

TS_INLINE void
UnixNetVConnection::set_local_addr()
{
  int local_sa_size = sizeof(local_addr);
  safe_getsockname(con.fd, (sockaddr *) & local_addr, &local_sa_size);
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
  inactivity_timeout_in = timeout;
#ifndef INACTIVITY_TIMEOUT
  next_inactivity_timeout_at = ink_get_hrtime() + timeout;
#else
  if (inactivity_timeout)
    inactivity_timeout->cancel_action(this);
  if (inactivity_timeout_in) {
    if (read.enabled) {
      ink_debug_assert(read.vio.mutex->thread_holding == this_ethread());
      inactivity_timeout = read.vio.mutex->thread_holding->schedule_in_local(this, inactivity_timeout_in);
    } else if (write.enabled) {
      ink_debug_assert(write.vio.mutex->thread_holding == this_ethread());
      inactivity_timeout = write.vio.mutex->thread_holding->schedule_in_local(this, inactivity_timeout_in);
    } else
      inactivity_timeout = 0;
  } else
    inactivity_timeout = 0;
#endif
}

TS_INLINE void
UnixNetVConnection::set_active_timeout(ink_hrtime timeout)
{
  active_timeout_in = timeout;
  if (active_timeout)
    active_timeout->cancel_action(this);
  if (active_timeout_in) {
    if (read.enabled) {
      ink_debug_assert(read.vio.mutex->thread_holding == this_ethread());
      active_timeout = thread->schedule_in(this, active_timeout_in);
    } else if (write.enabled) {
      ink_debug_assert(write.vio.mutex->thread_holding == this_ethread());
      active_timeout = thread->schedule_in(this, active_timeout_in);
    } else
      active_timeout = 0;
  } else
    active_timeout = 0;
}

TS_INLINE void
UnixNetVConnection::cancel_inactivity_timeout()
{
  inactivity_timeout_in = 0;
#ifdef INACTIVITY_TIMEOUT
  if (inactivity_timeout) {
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
  if (active_timeout) {
    active_timeout->cancel_action(this);
    active_timeout = NULL;
    active_timeout_in = 0;
  }
}

TS_INLINE UnixNetVConnection::~UnixNetVConnection() { }

TS_INLINE SOCKET
UnixNetVConnection::get_socket() {
  return con.fd;
}

// declarations for local use (within the net module)

void close_UnixNetVConnection(UnixNetVConnection * vc, EThread * t);
void write_to_net(NetHandler * nh, UnixNetVConnection * vc, PollDescriptor * pd, EThread * thread);
void write_to_net_io(NetHandler * nh, UnixNetVConnection * vc, EThread * thread);

#endif
