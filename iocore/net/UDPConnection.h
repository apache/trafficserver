/** @file
 *
 *  OpenSSL socket BIO that does TCP Fast Open.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#pragma once

#include <memory>

#include "tscore/ink_sock.h"
#include "I_EventSystem.h"
#include "P_Net.h"
#include "NetEvent.h"
#include "UDPPacket.h"

#define NET_EVENT_DATAGRAM_CONNECT_SUCCESS (NET_EVENT_EVENTS_START + 170)
#define NET_EVENT_DATAGRAM_CONNECT_ERROR (NET_EVENT_DATAGRAM_CONNECT_SUCCESS + 1)
#define NET_EVENT_DATAGRAM_WRITE_READY (NET_EVENT_DATAGRAM_CONNECT_SUCCESS + 1)

class AcceptUDP2ConnectionImpl;

class UDP2Connection : public NetEvent
{
public:
  virtual ~UDP2Connection() {}
  virtual int send(UDP2Packet *) = 0;
  virtual UDP2Packet *recv()     = 0;

  virtual int close()                              = 0;
  virtual void set_continuation(Continuation *con) = 0;
  virtual IpEndpoint from()                        = 0;
  virtual IpEndpoint to()                          = 0;
};

class UDP2ConnectionImpl : public UDP2Connection, public Continuation, public RefCountObj
{
public:
  UDP2ConnectionImpl() = delete;
  UDP2ConnectionImpl(AcceptUDP2ConnectionImpl *accpet, Continuation *con, EThread *ethread = nullptr);
  // independent allocate.
  UDP2ConnectionImpl(Continuation *con, EThread *ethread = nullptr);
  ~UDP2ConnectionImpl();

  enum class UDPEvents : uint8_t {
    UDP_CONNECT_EVENT,
    UDP_SEND_EVENT,
    UDP_USER_READ_READY,
    UDP_CLOSE_EVENT,
  };

  void
  free() override
  {
    ink_release_assert(!"unimplement");
  }

  // NetEventHandler
  void net_read_io(NetHandler *nh, EThread *lthread) override;
  void net_write_io(NetHandler *nh, EThread *lthread) override;
  void free(EThread *t) override;
  int callback(int event = CONTINUATION_EVENT_NONE, void *data = nullptr) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  EThread *get_thread() override;
  int close() override;
  int get_fd() override;
  Ptr<ProxyMutex> &get_mutex() override;
  ContFlags &get_control_flags() override;
  sockaddr const *get_remote_addr() override;
  const NetVCOptions &get_options() override;
  int start_io();

  // UDP2Connection
  int send(UDP2Packet *packet) override;
  UDP2Packet *recv() override;
  IpEndpoint from() override;
  IpEndpoint to() override;
  void set_continuation(Continuation *con) override;

  int create_socket(sockaddr const *addr, int recv_buf = 0, int send_buf = 0);
  int connect(sockaddr const *addr);
  bool is_connected();
  void bind_thread(EThread *thread);
  int receive(UDP2Packet *packet);

  int startEvent(int event, void *data);
  int mainEvent(int event, void *data);
  int endEvent(int event, void *data);

protected:
  bool _is_closed() const;
  void _reschedule(UDPEvents e, void *data);
  void _reenable(VIO *vio);
  virtual void _process_close_connection(UDP2ConnectionImpl *con);

  // Because Accept UDPConnection need to dispatch packet to different UDP2Connection.
  // The recv buffer should be visiable to AcceptUDPConnection.
  // FIXME: These should more abstract
  virtual void _process_recv_event();
  ASLL(UDP2Packet, in_link) _recv_queue;
  std::deque<UDP2PacketUPtr> _recv_list;

  Continuation *_con = nullptr;
  EThread *_thread   = nullptr;

private:
  // control max data size per read, This can be calculated as MAX_NIOV * 1024 / read
  static constexpr int MAX_NIOV = 1;

  ASLL(UDP2Packet, out_link) _send_queue;

  // internal schedule.
  int _readv(struct iovec *iov, int len);
  int _readv_from(IpEndpoint &from, struct iovec *iov, int len);
  int _send_to(UDP2Packet *p);
  int _send(UDP2Packet *p);
  int _connect(sockaddr const *addr);

  IpEndpoint _from{};
  IpEndpoint _to{};

  int _fd                               = -1;
  AcceptUDP2ConnectionImpl *_accept_con = nullptr;

  // TODO removed
  NetVCOptions _options{};
  ContFlags _cont_flags{};

  std::deque<UDP2PacketUPtr> _send_list;
};

// Accept UDP2Connection is ranning in ET_UDP, and dispatch UDPPacket to different sub connections in _connection_map
// So PacketHandler should manager all AcceptUDP2Connection to find the correct connection across diffierent listen
// Addrs.
// FIXME: In current implementable, every AcceptUDP2ConnectionImpl are independent. That means the client should always
// send to the same local addr.
class AcceptUDP2ConnectionImpl : public UDP2ConnectionImpl
{
public:
  AcceptUDP2ConnectionImpl(Continuation *c, EThread *thread) : UDP2ConnectionImpl(c, thread) {}
  AcceptUDP2ConnectionImpl() = delete;
  UDP2ConnectionImpl *create_connection(const IpEndpoint &from, const IpEndpoint &to, Continuation *c, EThread *thread);
  UDP2ConnectionImpl *find_connection(const IpEndpoint &peer);
  int close_connection(UDP2ConnectionImpl *con);

  int mainEvent(int event, void *data);

private:
  virtual void _process_close_connection(UDP2ConnectionImpl *con) override;
  virtual void _process_recv_event() override;

  UDP2ConnectionImpl *_create_connection(const IpEndpoint &from, const IpEndpoint &to, Continuation *c, EThread *thread);
  std::unordered_map<uint64_t, std::list<UDP2ConnectionImpl *>> _connection_map;
};

using UDP2ConnectionSPtr = std::shared_ptr<UDP2Connection>;
