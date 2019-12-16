/** @file
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
#include "AtomicEvent.h"

#define NET_EVENT_DATAGRAM_CONNECT_SUCCESS (NET_EVENT_EVENTS_START + 170)
#define NET_EVENT_DATAGRAM_CONNECT_ERROR (NET_EVENT_DATAGRAM_CONNECT_SUCCESS + 1)
#define NET_EVENT_DATAGRAM_WRITE_READY (NET_EVENT_DATAGRAM_CONNECT_SUCCESS + 1)

class AcceptUDP2ConnectionImpl;
class UDP2ConnectionManager;

class UDP2Connection : public NetEvent
{
public:
  virtual ~UDP2Connection() {}
  virtual int send(UDP2Packet *, bool flush = true) = 0;
  virtual UDP2Packet *recv()                        = 0;
  virtual void flush()                              = 0;

  virtual int close()                              = 0;
  virtual void set_continuation(Continuation *con) = 0;
  virtual IpEndpoint from()                        = 0;
  virtual IpEndpoint to()                          = 0;

  SLINK(UDP2Connection, closed_link);
};

class UDP2ConnectionImpl : public UDP2Connection, public Continuation
{
public:
  UDP2ConnectionImpl() = delete;
  // independent allocate.
  UDP2ConnectionImpl(UDP2ConnectionManager &manager, Continuation *con, EThread *ethread = nullptr);
  ~UDP2ConnectionImpl();

  enum class UDPEvents : uint8_t {
    UDP_START_EVENT,
    UDP_CONNECT_EVENT,
    UDP_USER_READ_READY,
    UDP_SEND_READY,
  };

  // NetEventHandler
  virtual void net_read_io(NetHandler *nh, EThread *lthread) override;
  void net_write_io(NetHandler *nh, EThread *lthread) override;
  void free(EThread *t) override;
  int callback(int event = CONTINUATION_EVENT_NONE, void *data = nullptr) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  EThread *get_thread() override;
  int close() override;
  int get_fd() override;
  Ptr<ProxyMutex> &get_mutex() override;
  ContFlags &get_control_flags() override;
  int start_io();

  // UDP2Connection
  int send(UDP2Packet *packet, bool flush = true) override;
  void flush() override;
  UDP2Packet *recv() override;
  IpEndpoint from() override;
  IpEndpoint to() override;
  void set_continuation(Continuation *con) override;

  int create_socket(sockaddr const *addr, int recv_buf = 0, int send_buf = 0);
  int connect(sockaddr const *addr);
  bool is_connected() const;
  void bind_thread(EThread *thread);
  int dispatch(UDP2Packet *packet);
  void reschedule_read();
  void reschedule_write();

  int startEvent(int event, void *data);
  int mainEvent(int event, void *data);

protected:
  // control max data size per read, This can be calculated as MAX_NIOV * 1024 / read
  static constexpr int MAX_NIOV = 1;

  bool _is_closed() const;
  void _reschedule(UDPEvents e, void *data, int64_t delay = 0);
  void _reenable(VIO *vio);
  void _read_from_net(NetHandler *nh, EThread *t, bool callback = true);
  virtual int _send(UDP2Packet *p);
  virtual int _read(struct iovec *iov, int len, IpEndpoint &from, IpEndpoint &to);

  ASLL(UDP2Packet, in_link) _recv_queue;
  std::deque<UDP2PacketUPtr> _recv_list;

  Continuation *_con = nullptr;
  EThread *_thread   = nullptr;

  UDP2ConnectionManager &_manager;

private:
  ASLL(UDP2Packet, out_link) _send_queue;

  // internal schedule.
  void _close_event(UDPEvents e);
  void _close_event(int e);
  int _connect();

  IpEndpoint _from{};
  IpEndpoint _to{};

  int _fd               = -1;
  bool _connected       = false;
  Event *_start_event   = nullptr;
  Event *_connect_event = nullptr;
  AtomicEvent _send_ready_event;
  AtomicEvent _user_read_ready_event;

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
  AcceptUDP2ConnectionImpl(UDP2ConnectionManager &manager, Continuation *c, EThread *thread)
    : UDP2ConnectionImpl(manager, c, thread)
  {
  }

  AcceptUDP2ConnectionImpl() = delete;
  UDP2ConnectionImpl *create_sub_connection(const IpEndpoint &from, const IpEndpoint &to, Continuation *c, EThread *thread);

  void net_read_io(NetHandler *nh, EThread *lthread) override;

private:
  int _send(UDP2Packet *p) override;
  int _read(struct iovec *iov, int len, IpEndpoint &from, IpEndpoint &to) override;
};
