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

#pragma once

#include "tscore/ink_sock.h"
#include "I_NetVConnection.h"
#include "P_UnixNetState.h"
#include "P_Connection.h"
#include "P_NetAccept.h"
#include "NetEvent.h"

class UnixNetVConnection;
class NetHandler;
struct PollDescriptor;

inline void
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
  socket_send_bufsize  = 0;
  sockopt_flags        = 0;
  packet_mark          = 0;
  packet_tos           = 0;
  packet_notsent_lowat = 0;

  etype = ET_NET;

  sni_servername              = nullptr;
  ssl_servername              = nullptr;
  sni_hostname                = nullptr;
  ssl_client_cert_name        = nullptr;
  ssl_client_private_key_name = nullptr;
  outbound_sni_policy         = nullptr;
}

inline void
NetVCOptions::set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags, unsigned long _packet_mark,
                             unsigned long _packet_tos, unsigned long _packet_notsent_lowat)
{
  socket_recv_bufsize  = _recv_bufsize;
  socket_send_bufsize  = _send_bufsize;
  sockopt_flags        = _opt_flags;
  packet_mark          = _packet_mark;
  packet_tos           = _packet_tos;
  packet_notsent_lowat = _packet_notsent_lowat;
}

enum tcp_congestion_control_t { CLIENT_SIDE, SERVER_SIDE };

class UnixNetVConnection : public NetVConnection, public NetEvent
{
public:
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;

  bool get_data(int id, void *data) override;

  const char *
  get_server_name() const override
  {
    return nullptr;
  }

  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;

  ////////////////////////////////////////////////////////////
  // Set the timeouts associated with this connection.      //
  // active_timeout is for the total elapsed time of        //
  // the connection.                                        //
  // inactivity_timeout is the elapsed time from the time   //
  // a read or a write was scheduled during which the       //
  // connection  was unable to sink/provide data.           //
  // calling these functions repeatedly resets the timeout. //
  // These functions are NOT THREAD-SAFE, and may only be   //
  // called when handing an  event from this NetVConnection,//
  // or the NetVConnection creation callback.               //
  ////////////////////////////////////////////////////////////
  virtual void set_active_timeout(ink_hrtime timeout_in) override;
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) override;
  virtual void set_default_inactivity_timeout(ink_hrtime timeout_in) override;
  virtual bool is_default_inactivity_timeout() override;
  virtual void cancel_active_timeout() override;
  virtual void cancel_inactivity_timeout() override;
  void set_action(Continuation *c) override;
  const Action *get_action() const;
  virtual void add_to_keep_alive_queue() override;
  virtual void remove_from_keep_alive_queue() override;
  virtual bool add_to_active_queue() override;
  virtual void remove_from_active_queue();

  // The public interface is VIO::reenable()
  void reenable(VIO *vio) override;
  void reenable_re(VIO *vio) override;

  SOCKET get_socket() override;

  ~UnixNetVConnection() override;

  /////////////////////////////////////////////////////////////////
  // instances of UnixNetVConnection should be allocated         //
  // only from the free list using UnixNetVConnection::alloc().  //
  // The constructor is public just to avoid compile errors.      //
  /////////////////////////////////////////////////////////////////
  UnixNetVConnection();

  int populate_protocol(std::string_view *results, int n) const override;
  const char *protocol_contains(std::string_view tag) const override;

  // noncopyable
  UnixNetVConnection(const NetVConnection &) = delete;
  UnixNetVConnection &operator=(const NetVConnection &) = delete;

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
  getSSLHandShakeComplete() const
  {
    return (true);
  }

  virtual bool
  trackFirstHandshake()
  {
    return false;
  }

  // NetEvent
  virtual void net_read_io(NetHandler *nh, EThread *lthread) override;
  virtual void net_write_io(NetHandler *nh, EThread *lthread) override;
  virtual void free(EThread *t) override;
  virtual int
  close() override
  {
    return this->con.close();
  }
  virtual int
  get_fd() override
  {
    return this->con.fd;
  }

  virtual EThread *
  get_thread() override
  {
    return this->thread;
  }

  virtual int
  callback(int event = CONTINUATION_EVENT_NONE, void *data = nullptr) override
  {
    return this->handleEvent(event, data);
  }

  virtual Ptr<ProxyMutex> &
  get_mutex() override
  {
    return this->mutex;
  }

  virtual ContFlags &
  get_control_flags() override
  {
    return this->control_flags;
  }

  virtual int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs);
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

  unsigned int id = 0;

  Connection con;
  int recursion            = 0;
  bool from_accept_thread  = false;
  NetAccept *accept_object = nullptr;

  int startEvent(int event, Event *e);
  int acceptEvent(int event, Event *e);
  int mainEvent(int event, Event *e);
  virtual int connectUp(EThread *t, int fd);
  /**
   * Populate the current object based on the socket information in the
   * con parameter.
   * This is logic is invoked when the NetVC object is created in a new thread context
   */
  virtual int populate(Connection &con, Continuation *c, void *arg);
  virtual void clear();

  ink_hrtime get_inactivity_timeout() override;
  ink_hrtime get_active_timeout() override;

  virtual void set_local_addr() override;
  void set_mptcp_state() override;
  virtual void set_remote_addr() override;
  void set_remote_addr(const sockaddr *) override;
  int set_tcp_congestion_control(int side) override;
  void apply_options() override;

  friend void write_to_net_io(NetHandler *, UnixNetVConnection *, EThread *);

private:
  virtual void *_prepareForMigration();
  virtual NetProcessor *_getNetProcessor();
};

extern ClassAllocator<UnixNetVConnection> netVCAllocator;

typedef int (UnixNetVConnection::*NetVConnHandler)(int, void *);

inline void
UnixNetVConnection::set_remote_addr()
{
  ats_ip_copy(&remote_addr, &con.addr);
  this->control_flags.set_flag(ContFlags::DEBUG_OVERRIDE, diags->test_override_ip(remote_addr));
  set_cont_flags(get_control_flags());
}

inline void
UnixNetVConnection::set_remote_addr(const sockaddr *new_sa)
{
  ats_ip_copy(&remote_addr, new_sa);
  this->control_flags.set_flag(ContFlags::DEBUG_OVERRIDE, diags->test_override_ip(remote_addr));
  set_cont_flags(get_control_flags());
}

inline void
UnixNetVConnection::set_local_addr()
{
  int local_sa_size = sizeof(local_addr);
  // This call will fail if fd is closed already. That is ok, because the
  // `local_addr` is checked within get_local_addr() and the `got_local_addr`
  // is set only with a valid `local_addr`.
  ATS_UNUSED_RETURN(safe_getsockname(con.fd, &local_addr.sa, &local_sa_size));
}

// Update the internal VC state variable for MPTCP
inline void
UnixNetVConnection::set_mptcp_state()
{
  int mptcp_enabled      = -1;
  int mptcp_enabled_size = sizeof(mptcp_enabled);

  if (0 == safe_getsockopt(con.fd, IPPROTO_TCP, MPTCP_ENABLED, (char *)&mptcp_enabled, &mptcp_enabled_size)) {
    Debug("socket_mptcp", "MPTCP socket state: %d", mptcp_enabled);
    mptcp_state = mptcp_enabled > 0 ? true : false;
  } else {
    Debug("socket_mptcp", "MPTCP failed getsockopt(): %s", strerror(errno));
  }
}

inline ink_hrtime
UnixNetVConnection::get_active_timeout()
{
  return active_timeout_in;
}

inline ink_hrtime
UnixNetVConnection::get_inactivity_timeout()
{
  return inactivity_timeout_in;
}

inline void
UnixNetVConnection::set_active_timeout(ink_hrtime timeout_in)
{
  Debug("socket", "Set active timeout=%" PRId64 ", NetVC=%p", timeout_in, this);
  active_timeout_in        = timeout_in;
  next_activity_timeout_at = (active_timeout_in > 0) ? Thread::get_hrtime() + timeout_in : 0;
}

inline void
UnixNetVConnection::cancel_inactivity_timeout()
{
  Debug("socket", "Cancel inactive timeout for NetVC=%p", this);
  inactivity_timeout_in      = 0;
  next_inactivity_timeout_at = 0;
}

inline void
UnixNetVConnection::cancel_active_timeout()
{
  Debug("socket", "Cancel active timeout for NetVC=%p", this);
  active_timeout_in        = 0;
  next_activity_timeout_at = 0;
}

inline UnixNetVConnection::~UnixNetVConnection() {}

inline SOCKET
UnixNetVConnection::get_socket()
{
  return con.fd;
}

inline void
UnixNetVConnection::set_action(Continuation *c)
{
  action_ = c;
}

inline const Action *
UnixNetVConnection::get_action() const
{
  return &action_;
}

// declarations for local use (within the net module)

void write_to_net(NetHandler *nh, UnixNetVConnection *vc, EThread *thread);
void write_to_net_io(NetHandler *nh, UnixNetVConnection *vc, EThread *thread);
void net_activity(UnixNetVConnection *vc, EThread *thread);
