/** @file

  Server side connection management.

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

#include "proxy/PoolableSession.h"

#include <set>
#include <string>

class HttpSM;

/** Represents a server side session entry in a ConnectionPool to an origin. */
class ConnectingEntry : public Continuation
{
public:
  ConnectingEntry() = default;
  ~ConnectingEntry() override;
  void remove_entry();
  int state_http_server_open(int event, void *data);
  static PoolableSession *create_server_session(HttpSM *root_sm, NetVConnection *netvc, MIOBuffer *netvc_read_buffer,
                                                IOBufferReader *netvc_reader);

public:
  std::string sni;
  std::string cert_name;
  IpEndpoint ipaddr;
  std::string hostname;
  std::set<HttpSM *> connect_sms;
  ProxyTransaction *ua_txn = nullptr;
  NetVConnection *netvc    = nullptr;
  bool is_no_plugin_tunnel = false;

private:
  MIOBuffer *_netvc_read_buffer = nullptr;
  IOBufferReader *_netvc_reader = nullptr;
  Action *_pending_action       = nullptr;
  NetVCOptions opt;
};

struct IpHelper {
  size_t
  operator()(IpEndpoint const &arg) const
  {
    return IpAddr{&arg.sa}.hash();
  }
  bool
  operator()(IpEndpoint const &arg1, IpEndpoint const &arg2) const
  {
    return ats_ip_addr_port_eq(&arg1.sa, &arg2.sa);
  }
};

using ConnectingIpPool = std::unordered_multimap<IpEndpoint, ConnectingEntry *, IpHelper, IpHelper>;

/** Represents the set of connections to an origin. */
class ConnectingPool
{
public:
  ConnectingPool() = default;
  ConnectingIpPool m_ip_pool;
};
