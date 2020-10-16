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

#include "http/HttpSM.h"
#include "http/Http1ServerSession.h"
#include "Plugin.h"

#define HttpTxnDebug(fmt, ...) SsnDebug(this, "http_txn", fmt, __VA_ARGS__)

ProxyTransaction::ProxyTransaction() : VConnection(nullptr) {}

void
ProxyTransaction::new_transaction(bool from_early_data)
{
  ink_release_assert(_sm == nullptr);

  // Defensive programming, make sure nothing persists across
  // connection re-use

  ink_release_assert(_proxy_ssn != nullptr);
  _sm = HttpSM::allocate();
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

  this->increment_client_transactions_stat();
  _sm->attach_client_session(this, _reader);
}

bool
ProxyTransaction::attach_server_session(Http1ServerSession *ssession, bool transaction_done)
{
  return _proxy_ssn->attach_server_session(ssession, transaction_done);
}

void
ProxyTransaction::destroy()
{
  _sm = nullptr;
  this->mutex.clear();
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
  return upstream_outbound_options.outbound_ip4;
}

IpAddr
ProxyTransaction::get_outbound_ip6() const
{
  return upstream_outbound_options.outbound_ip6;
}

void
ProxyTransaction::set_outbound_ip(const IpAddr &new_addr)
{
  if (new_addr.isIp4()) {
    upstream_outbound_options.outbound_ip4 = new_addr;
  } else if (new_addr.isIp6()) {
    upstream_outbound_options.outbound_ip6 = new_addr;
  } else {
    upstream_outbound_options.outbound_ip4.invalidate();
    upstream_outbound_options.outbound_ip6.invalidate();
  }
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

void
ProxyTransaction::set_h2c_upgrade_flag()
{
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
  this->decrement_client_transactions_stat();
}
