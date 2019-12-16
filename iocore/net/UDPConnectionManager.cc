/** @file

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

#include "UDPConnectionManager.h"

UDP2ConnectionImpl *
UDP2ConnectionManager::create_udp_connection(Continuation *c, EThread *ethread, sockaddr const *local, sockaddr const *peer,
                                             int recv_buf, int send_buf)
{
  ink_release_assert(this->mutex->thread_holding == this_ethread());
  auto hash = ats_ip_port_hash(local) ^ ats_ip_port_hash(peer);
  auto it   = this->_routes.find(hash);
  if (it != this->_routes.end()) {
    for (auto itt : it->second) {
      auto local = itt->from();
      auto peer  = itt->to();
      if (ats_ip_addr_port_eq(&local.sa, local) && ats_ip_addr_port_eq(&peer.sa, peer)) {
        return itt;
      }
    }
  }

  // not found
  ink_assert(local != nullptr);
  ink_assert(peer != nullptr);
  auto con = new UDP2ConnectionImpl(*this, c, ethread);
  if (con->create_socket(local, recv_buf, send_buf) != 0) {
    delete con;
    return nullptr;
  }

  if (con->connect(peer) < 0) {
    delete con;
    return nullptr;
  }

  ++this->_size;
  this->_routes.emplace(hash, std::list<UDP2ConnectionImpl *>(1, con));
  return con;
}

AcceptUDP2ConnectionImpl *
UDP2ConnectionManager::create_accept_udp_connection(Continuation *c, EThread *thread, sockaddr *local, int recv_buf, int send_buf)
{
  ink_assert(local != nullptr);
  auto con = new AcceptUDP2ConnectionImpl(*this, c, thread);
  if (con->create_socket(local, recv_buf, send_buf) != 0) {
    delete con;
    return nullptr;
  }

  ink_assert(con->start_io() >= 0);
  return con;
}

UDP2ConnectionImpl *
UDP2ConnectionManager::find_connection(sockaddr const *local, sockaddr const *peer)
{
  auto hash = ats_ip_port_hash(local) ^ ats_ip_port_hash(peer);

  auto it = this->_routes.find(hash);
  if (it != this->_routes.end()) {
    for (auto itt : it->second) {
      auto local = itt->from();
      auto peer  = itt->to();
      if (ats_ip_addr_port_eq(&local.sa, local) && ats_ip_addr_port_eq(&peer.sa, peer)) {
        return itt;
      }
    }
  }
  return nullptr;
}

void
UDP2ConnectionManager::close_connection(UDP2Connection *c, const char *line)
{
  this->_closed_queue.push(c);
}

int
UDP2ConnectionManager::mainEvent(int event, void *data)
{
  // main routine for closed connections cleaning
  SList(UDP2Connection, closed_link) aq(this->_closed_queue.popall());
  UDP2Connection *c;
  while ((c = aq.pop())) {
    auto local = c->from();
    auto peer  = c->to();
    auto hash  = ats_ip_port_hash(local) ^ ats_ip_port_hash(peer);
    auto it    = this->_routes.find(hash);
    if (it != this->_routes.end()) {
      for (auto itt = it->second.begin(); itt != it->second.end(); ++itt) {
        if (*itt == c) {
          it->second.erase(itt);
          --this->_size;
          delete c;
          break;
        }
      }

      if (it->second.empty()) {
        this->_routes.erase(it);
      }
    }
  }

  return 0;
}
