/** @file

  ProxyTransaction - Base class for protocol client transactions.

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

#include "proxy/http/HttpSM.h"
#include "proxy/Plugin.h"

namespace
{
DbgCtl dbg_ctl_http_txn{"http_txn"};

} // end anonymous namespace

#define HttpTxnDebug(fmt, ...) SsnDbg(this, dbg_ctl_http_txn, fmt, __VA_ARGS__)

extern ClassAllocator<HttpSM> httpSMAllocator;

ProxyTransaction::ProxyTransaction(ProxySession *session) : VConnection(nullptr), _proxy_ssn(session) {}

ProxyTransaction::~ProxyTransaction()
{
  this->_sm = nullptr;
  this->mutex.clear();
}

void
ProxyTransaction::new_transaction(bool from_early_data)
{
  ink_release_assert(_sm == nullptr);

  // Defensive programming, make sure nothing persists across
  // connection re-use

  ink_release_assert(_proxy_ssn != nullptr);
  _sm = THREAD_ALLOC(httpSMAllocator, this_thread());
  _sm->init(from_early_data);
  HttpTxnDebug("[%" PRId64 "] Starting transaction %d using sm [%" PRId64 "]", _proxy_ssn->connection_id(),
               _proxy_ssn->get_transact_count(), _sm->sm_id);

  // PI tag valid only for internal requests
  if (this->get_netvc()->get_is_internal_request()) {
    PluginIdentity *pi = dynamic_cast<PluginIdentity *>(this->get_netvc());
    if (pi) {
      _sm->plugin_tag = pi->getPluginTag();
      _sm->plugin_id  = pi->getPluginId();
    }
  }

  this->increment_transactions_stat();
  _sm->attach_client_session(this);
}

bool
ProxyTransaction::attach_server_session(PoolableSession *ssession, bool transaction_done)
{
  return _proxy_ssn->attach_server_session(ssession, transaction_done);
}

void
ProxyTransaction::set_rx_error_code(ProxyError e)
{
  if (this->_sm) {
    this->_sm->t_state.client_info.rx_error_code = e;
  }
}

void
ProxyTransaction::set_tx_error_code(ProxyError e)
{
  if (this->_sm) {
    this->_sm->t_state.client_info.tx_error_code = e;
  }
}

NetVConnection *
ProxyTransaction::get_netvc() const
{
  return (_proxy_ssn) ? _proxy_ssn->get_netvc() : nullptr;
}

bool
ProxyTransaction::is_first_transaction() const
{
  return _proxy_ssn->get_transact_count() == 1;
}

void
ProxyTransaction::set_session_active()
{
  if (_proxy_ssn) {
    _proxy_ssn->set_session_active();
  }
}

void
ProxyTransaction::clear_session_active()
{
  if (_proxy_ssn) {
    _proxy_ssn->clear_session_active();
  }
}

const IpAllow::ACL &
ProxyTransaction::get_acl() const
{
  return _proxy_ssn ? _proxy_ssn->acl : IpAllow::DENY_ALL_ACL;
}

// outbound values Set via the server port definition.  Really only used for Http1 at the moment
in_port_t
ProxyTransaction::get_outbound_port() const
{
  return upstream_outbound_options.outbound_port;
}
void
ProxyTransaction::set_outbound_port(in_port_t port)
{
  upstream_outbound_options.outbound_port = port;
}

IpAddr
ProxyTransaction::get_outbound_ip4() const
{
  if (upstream_outbound_options.outbound.has_ip4()) {
    return IpAddr(upstream_outbound_options.outbound.ip4().network_order());
  }
  return IpAddr();
}

IpAddr
ProxyTransaction::get_outbound_ip6() const
{
  if (upstream_outbound_options.outbound.has_ip6()) {
    return IpAddr(upstream_outbound_options.outbound.ip6().network_order());
  }
  return IpAddr();
}

void
ProxyTransaction::set_outbound_ip(swoc::IPAddr const &addr)
{
  upstream_outbound_options.outbound = addr;
}

bool
ProxyTransaction::is_outbound_transparent() const
{
  return upstream_outbound_options.f_outbound_transparent;
}

void
ProxyTransaction::set_outbound_transparent(bool flag)
{
  upstream_outbound_options.f_outbound_transparent = flag;
}

int
ProxyTransaction::get_transaction_priority_weight() const
{
  return 0;
}

int
ProxyTransaction::get_transaction_priority_dependence() const
{
  return 0;
}

void
ProxyTransaction::transaction_done()
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  this->decrement_transactions_stat();
}

// Implement VConnection interface.
VIO *
ProxyTransaction::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return _proxy_ssn->do_io_read(c, nbytes, buf);
}
VIO *
ProxyTransaction::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return _proxy_ssn->do_io_write(c, nbytes, buf, owner);
}

void
ProxyTransaction::do_io_close(int lerrno)
{
  _proxy_ssn->do_io_close(lerrno);
  // this->destroy(); Parent owns this data structure.  No need for separate destroy.
}

void
ProxyTransaction::do_io_shutdown(ShutdownHowTo_t howto)
{
  _proxy_ssn->do_io_shutdown(howto);
}

void
ProxyTransaction::reenable(VIO *vio)
{
  _proxy_ssn->reenable(vio);
}

bool
ProxyTransaction::has_request_body(int64_t request_content_length, bool is_chunked) const
{
  return request_content_length > 0 || is_chunked;
}

void
ProxyTransaction::attach_transaction(HttpSM *attach_sm)
{
  _sm = attach_sm;
}

HTTPVersion
ProxyTransaction::get_version(HTTPHdr &hdr) const
{
  return hdr.version_get();
}

bool
ProxyTransaction::is_read_closed() const
{
  return false;
}

bool
ProxyTransaction::expect_send_trailer() const
{
  return false;
}

void
ProxyTransaction::set_expect_send_trailer()
{
}

bool
ProxyTransaction::expect_receive_trailer() const
{
  return false;
}

void
ProxyTransaction::set_expect_receive_trailer()
{
}

bool
ProxyTransaction::allow_half_open() const
{
  return false;
}

// Most protocols will not want to set the Connection: header
// For H2 it will initiate the drain logic.  So we make do nothing
// the default action.
void
ProxyTransaction::set_close_connection(HTTPHdr & /* hdr ATS_UNUSED */) const
{
}

void
ProxyTransaction::mark_as_tunnel_endpoint()
{
  auto nvc = get_netvc();
  ink_assert(nvc != nullptr);
  nvc->mark_as_tunnel_endpoint();
}
