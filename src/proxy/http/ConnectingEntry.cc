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

#include "../../iocore/net/P_UnixNetVConnection.h"
#include "tsutil/DbgCtl.h"
#include "proxy/http/ConnectingEntry.h"
#include "proxy/http/HttpSM.h"

namespace
{

DbgCtl dbg_ctl_http_connect{"http_connect"};

} // end anonymous namespace

ConnectingEntry::~ConnectingEntry()
{
  if (_netvc_read_buffer != nullptr) {
    free_MIOBuffer(_netvc_read_buffer);
    _netvc_read_buffer = nullptr;
  }
}

int
ConnectingEntry::state_http_server_open(int event, void *data)
{
  Dbg(dbg_ctl_http_connect, "entered inside ConnectingEntry::state_http_server_open");

  switch (event) {
  case NET_EVENT_OPEN: {
    netvc                  = static_cast<NetVConnection *>(data);
    UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(netvc);
    ink_release_assert(_pending_action == nullptr || _pending_action->continuation == vc->get_action()->continuation);
    _pending_action = nullptr;
    Dbg(dbg_ctl_http_connect, "ConnectingEntrysetting handler for connection handshake");
    // Just want to get a write-ready event so we know that the connection handshake is complete.
    // The buffer we create will be handed over to the eventually created server session
    _netvc_read_buffer = new_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
    _netvc_reader      = _netvc_read_buffer->alloc_reader();
    ink_release_assert(!connect_sms.empty());
    HttpSM *prime_connect_sm = *(connect_sms.begin());

    // Perform a zero-byte read to ensure this function can be called back for
    // VC_EVENT_READ_COMPLETE after the handshake is complete.
    netvc->do_io_read(this, 0, _netvc_reader->mbuf);
    int64_t nbytes = 1;
    if (is_no_plugin_tunnel && prime_connect_sm->t_state.txn_conf->proxy_protocol_out >= 0) {
      nbytes = do_outbound_proxy_protocol(_netvc_reader->mbuf, vc, ua_txn->get_netvc(),
                                          prime_connect_sm->t_state.txn_conf->proxy_protocol_out);
    }
    netvc->do_io_write(this, nbytes, _netvc_reader);
    netvc->set_inactivity_timeout(prime_connect_sm->get_server_connect_timeout());
    ink_release_assert(_pending_action == nullptr);
    return 0;
  }
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    Dbg(dbg_ctl_http_connect, "Kick off %zd state machines waiting for origin", connect_sms.size());
    this->remove_entry();
    netvc->do_io_write(nullptr, 0, nullptr);
    if (!connect_sms.empty()) {
      auto prime_iter = connect_sms.rbegin();
      ink_release_assert(prime_iter != connect_sms.rend());
      PoolableSession *new_session = (*prime_iter)->create_server_session(*netvc, _netvc_read_buffer, _netvc_reader);
      netvc                        = nullptr;
      _netvc_read_buffer           = nullptr;

      // Did we end up with a multiplexing session?
      int count = 0;
      if (new_session->is_multiplexing()) {
        // Hand off to all queued up ConnectSM's.
        while (!connect_sms.empty()) {
          Dbg(dbg_ctl_http_connect, "ConnectingEntry Pass along CONNECT_EVENT_TXN %d", count++);
          auto entry = connect_sms.begin();

          SCOPED_MUTEX_LOCK(lock, (*entry)->mutex, this_ethread());
          (*entry)->handleEvent(CONNECT_EVENT_TXN, new_session);
          connect_sms.erase(entry);
        }
      } else {
        // Hand off to one and tell all of the others to connect directly
        Dbg(dbg_ctl_http_connect, "ConnectingEntry send CONNECT_EVENT_TXN to first %d", count++);
        {
          SCOPED_MUTEX_LOCK(lock, (*prime_iter)->mutex, this_ethread());
          (*prime_iter)->handleEvent(CONNECT_EVENT_TXN, new_session);
          connect_sms.erase((++prime_iter).base());
        }
        while (!connect_sms.empty()) {
          auto entry = connect_sms.begin();
          Dbg(dbg_ctl_http_connect, "ConnectingEntry Pass along CONNECT_EVENT_DIRECT %d", count++);
          SCOPED_MUTEX_LOCK(lock, (*entry)->mutex, this_ethread());
          (*entry)->handleEvent(CONNECT_EVENT_DIRECT, nullptr);
          connect_sms.erase(entry);
        }
      }
    } else {
      ink_release_assert(!"There should be some sms on the connect_entry");
    }
    delete this;

    // ConnectingEntry should remove itself from the tables and delete itself
    return 0;
  }
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
  case NET_EVENT_OPEN_FAILED: {
    Dbg(dbg_ctl_http_connect, "Stop %zd state machines waiting for failed origin", connect_sms.size());
    this->remove_entry();
    int vc_provided_cert = 0;
    int lerrno           = EIO;
    if (netvc != nullptr) {
      vc_provided_cert = netvc->provided_cert();
      lerrno           = netvc->lerrno == 0 ? lerrno : netvc->lerrno;
      netvc->do_io_close();
    }
    while (!connect_sms.empty()) {
      auto entry = connect_sms.begin();
      SCOPED_MUTEX_LOCK(lock, (*entry)->mutex, this_ethread());
      (*entry)->t_state.set_connect_fail(lerrno);
      (*entry)->server_connection_provided_cert = vc_provided_cert;
      (*entry)->handleEvent(event, data);
      connect_sms.erase(entry);
    }
    // ConnectingEntry should remove itself from the tables and delete itself
    delete this;

    return 0;
  }
  default:
    Error("[ConnectingEntry::state_http_server_open] Unknown event: %d", event);
    ink_release_assert(0);
    return 0;
  }

  return 0;
}

void
ConnectingEntry::remove_entry()
{
  EThread *ethread            = this_ethread();
  auto [iter_start, iter_end] = ethread->connecting_pool->m_ip_pool.equal_range(this->ipaddr);
  for (auto ip_iter = iter_start; ip_iter != iter_end; ++ip_iter) {
    if (ip_iter->second == this) {
      ethread->connecting_pool->m_ip_pool.erase(ip_iter);
      break;
    }
  }
}
