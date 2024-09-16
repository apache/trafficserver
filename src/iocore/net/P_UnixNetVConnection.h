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

#include <memory>

#include "tscore/ink_sock.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/NetVConnection.h"
#include "P_Connection.h"
#include "P_NetAccept.h"
#include "iocore/net/NetEvent.h"

#if HAVE_STRUCT_MPTCP_INFO_SUBFLOWS
#include <linux/mptcp.h>
#endif

class UnixNetVConnection;
class NetHandler;
struct PollDescriptor;

enum tcp_congestion_control_t { CLIENT_SIDE, SERVER_SIDE };

// WARNING:  many or most of the member functions of UnixNetVConnection should only be used when it is instantiated
// directly.  They should not be used when UnixNetVConnection is a base class.
class UnixNetVConnection : public NetVConnection, public NetEvent
{
public:
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;

  bool get_data(int id, void *data) override;

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
  virtual void  set_active_timeout(ink_hrtime timeout_in) override;
  virtual void  set_inactivity_timeout(ink_hrtime timeout_in) override;
  virtual void  set_default_inactivity_timeout(ink_hrtime timeout_in) override;
  virtual bool  is_default_inactivity_timeout() override;
  virtual void  cancel_active_timeout() override;
  virtual void  cancel_inactivity_timeout() override;
  void          set_action(Continuation *c) override;
  const Action *get_action() const;
  virtual void  add_to_keep_alive_queue() override;
  virtual void  remove_from_keep_alive_queue() override;
  virtual bool  add_to_active_queue() override;
  virtual void  remove_from_active_queue();

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

  /** Track this inbound connection for limiting per client connections.

   * @param[in] group ConnectionTracker group to track this connection in.
   */
  void enable_inbound_connection_tracking(std::shared_ptr<ConnectionTracker::Group> group);

  /** Release the inbound connection tracking for this connection. */
  void release_inbound_connection_tracking();

  int         populate_protocol(std::string_view *results, int n) const override;
  const char *protocol_contains(std::string_view tag) const override;

  // noncopyable
  UnixNetVConnection(const NetVConnection &)            = delete;
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
  virtual void free_thread(EThread *t) override;
  virtual int
  close() override
  {
    return this->con.close();
  }
  virtual int
  get_fd() override
  {
    return this->con.sock.get_fd();
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
  void            readDisable(NetHandler *nh);
  void            readSignalError(NetHandler *nh, int err);
  int             readSignalDone(int event, NetHandler *nh);
  int             readSignalAndUpdate(int event);
  void            readReschedule(NetHandler *nh);
  void            writeReschedule(NetHandler *nh);
  void            netActivity(EThread *lthread);
  /**
   * If the current object's thread does not match the t argument, create a new
   * NetVC in the thread t context based on the socket and ssl information in the
   * current NetVC and mark the current NetVC to be closed.
   */
  UnixNetVConnection *migrateToCurrentThread(Continuation *c, EThread *t);

  Action action_;

  unsigned int id = 0;

  Connection con;
  int        recursion          = 0;
  bool       from_accept_thread = false;
  NetAccept *accept_object      = nullptr;

  int         startEvent(int event, Event *e);
  int         acceptEvent(int event, Event *e);
  int         mainEvent(int event, Event *e);
  virtual int connectUp(EThread *t, int fd);
  /**
   * Populate the current object based on the socket information in the
   * con parameter.
   * This is logic is invoked when the NetVC object is created in a new thread context
   */
  virtual int  populate(Connection &con, Continuation *c, void *arg);
  virtual void clear();

  ink_hrtime get_inactivity_timeout() override;
  ink_hrtime get_active_timeout() override;

  virtual void set_local_addr() override;
  void         set_mptcp_state() override;
  virtual void set_remote_addr() override;
  void         set_remote_addr(const sockaddr *) override;
  int          set_tcp_congestion_control(int side) override;
  void         apply_options() override;

  friend void write_to_net_io(NetHandler *, UnixNetVConnection *, EThread *);

  // set_context() should be called before calling this member function.
  void mark_as_tunnel_endpoint() override;

  bool
  is_tunnel_endpoint() const
  {
    return _is_tunnel_endpoint;
  }

private:
  virtual void         *_prepareForMigration();
  virtual NetProcessor *_getNetProcessor();

  bool _is_tunnel_endpoint{false};

  // Called by make_tunnel_endpiont() when the far end of the TCP connection is the active/client end.
  virtual void _in_context_tunnel();

  // Called by make_tunnel_endpiont() when the far end of the TCP connection is the passive/server end.
  virtual void _out_context_tunnel();

  inline static DbgCtl _dbg_ctl_socket{"socket"};
  inline static DbgCtl _dbg_ctl_socket_mptcp{"socket_mptcp"};

  /** The shared group across all connections for this IP to track incoming
   * connections for connection limiting. */
  std::shared_ptr<ConnectionTracker::Group> conn_track_group;
};

extern ClassAllocator<UnixNetVConnection> netVCAllocator;

using NetVConnHandler = int (UnixNetVConnection::*)(int, void *);

inline void
UnixNetVConnection::set_remote_addr()
{
  ats_ip_copy(&remote_addr, &con.addr);
  this->control_flags.set_flag(ContFlags::DEBUG_OVERRIDE, diags()->test_override_ip(remote_addr));
  set_cont_flags(get_control_flags());
}

inline void
UnixNetVConnection::set_remote_addr(const sockaddr *new_sa)
{
  ats_ip_copy(&remote_addr, new_sa);
  this->control_flags.set_flag(ContFlags::DEBUG_OVERRIDE, diags()->test_override_ip(remote_addr));
  set_cont_flags(get_control_flags());
}

inline void
UnixNetVConnection::set_local_addr()
{
  int local_sa_size = sizeof(local_addr);
  // This call will fail if fd is closed already. That is ok, because the
  // `local_addr` is checked within get_local_addr() and the `got_local_addr`
  // is set only with a valid `local_addr`.
  ATS_UNUSED_RETURN(safe_getsockname(get_fd(), &local_addr.sa, &local_sa_size));
}

// Update the internal VC state variable for MPTCP
inline void
UnixNetVConnection::set_mptcp_state()
{
#if defined(HAVE_STRUCT_MPTCP_INFO_SUBFLOWS) && defined(MPTCP_INFO) && MPTCP_INFO == 1
  struct mptcp_info minfo;
  int               minfo_len = sizeof(minfo);

  Dbg(_dbg_ctl_socket_mptcp, "MPTCP_INFO and struct mptcp_info defined");
  if (0 == safe_getsockopt(get_fd(), SOL_MPTCP, MPTCP_INFO, &minfo, &minfo_len)) {
    if (minfo_len > 0) {
      Dbg(_dbg_ctl_socket_mptcp, "MPTCP socket state (remote key received): %d",
          (minfo.mptcpi_flags & MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED));
      mptcp_state = (minfo.mptcpi_flags & MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED);
      return;
    }
  } else {
    mptcp_state = 0;
    Dbg(_dbg_ctl_socket_mptcp, "MPTCP failed getsockopt(%d, MPTCP_INFO): %s", get_fd(), strerror(errno));
  }
#endif
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
  Dbg(_dbg_ctl_socket, "Set active timeout=%" PRId64 ", NetVC=%p", timeout_in, this);
  active_timeout_in        = timeout_in;
  next_activity_timeout_at = (active_timeout_in > 0) ? ink_get_hrtime() + timeout_in : 0;
}

inline void
UnixNetVConnection::cancel_inactivity_timeout()
{
  Dbg(_dbg_ctl_socket, "Cancel inactive timeout for NetVC=%p", this);
  inactivity_timeout_in      = 0;
  next_inactivity_timeout_at = 0;
}

inline void
UnixNetVConnection::cancel_active_timeout()
{
  Dbg(_dbg_ctl_socket, "Cancel active timeout for NetVC=%p", this);
  active_timeout_in        = 0;
  next_activity_timeout_at = 0;
}

inline UnixNetVConnection::~UnixNetVConnection()
{
  release_inbound_connection_tracking();
}

inline SOCKET
UnixNetVConnection::get_socket()
{
  return get_fd();
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
