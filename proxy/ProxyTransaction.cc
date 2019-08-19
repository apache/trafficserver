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
ProxyTransaction::new_transaction()
{
  ink_assert(current_reader == nullptr);

  // Defensive programming, make sure nothing persists across
  // connection re-use

  ink_release_assert(proxy_ssn != nullptr);
  current_reader = HttpSM::allocate();
  current_reader->init();
  HttpTxnDebug("[%" PRId64 "] Starting transaction %d using sm [%" PRId64 "]", proxy_ssn->connection_id(),
               proxy_ssn->get_transact_count(), current_reader->sm_id);

  PluginIdentity *pi = dynamic_cast<PluginIdentity *>(this->get_netvc());
  if (pi) {
    current_reader->plugin_tag = pi->getPluginTag();
    current_reader->plugin_id  = pi->getPluginId();
  }

  this->increment_client_transactions_stat();
  current_reader->attach_client_session(this, sm_reader);
}

void
ProxyTransaction::release(IOBufferReader *r)
{
  HttpTxnDebug("[%" PRId64 "] session released by sm [%" PRId64 "]", proxy_ssn ? proxy_ssn->connection_id() : 0,
               current_reader ? current_reader->sm_id : 0);

  this->decrement_client_transactions_stat();

  // Pass along the release to the session
  if (proxy_ssn) {
    proxy_ssn->release(this);
  }
}

void
ProxyTransaction::attach_server_session(Http1ServerSession *ssession, bool transaction_done)
{
  proxy_ssn->attach_server_session(ssession, transaction_done);
}

void
ProxyTransaction::destroy()
{
  current_reader = nullptr;
  this->mutex.clear();
}

// See if we need to schedule on the primary thread for the transaction or change the thread that is associated with the VC.
// If we reschedule, the scheduled action is returned.  Otherwise, NULL is returned
Action *
ProxyTransaction::adjust_thread(Continuation *cont, int event, void *data)
{
  NetVConnection *vc   = this->get_netvc();
  EThread *this_thread = this_ethread();
  if (vc && vc->thread != this_thread) {
    if (vc->thread->is_event_type(ET_NET)) {
      return vc->thread->schedule_imm(cont, event, data);
    } else { // Not a net thread, take over this thread
      vc->thread = this_thread;
    }
  }
  return nullptr;
}

void
ProxyTransaction::set_rx_error_code(ProxyError e)
{
  if (this->current_reader) {
    this->current_reader->t_state.client_info.rx_error_code = e;
  }
}

void
ProxyTransaction::set_tx_error_code(ProxyError e)
{
  if (this->current_reader) {
    this->current_reader->t_state.client_info.tx_error_code = e;
  }
}

NetVConnection *
ProxyTransaction::get_netvc() const
{
  return (proxy_ssn) ? proxy_ssn->get_netvc() : nullptr;
}

bool
ProxyTransaction::is_first_transaction() const
{
  return proxy_ssn->get_transact_count() == 1;
}
// Ask your session if this is allowed
bool
ProxyTransaction::is_transparent_passthrough_allowed()
{
  return upstream_outbound_options.f_transparent_passthrough;
}

bool
ProxyTransaction::is_chunked_encoding_supported() const
{
  return proxy_ssn ? proxy_ssn->is_chunked_encoding_supported() : false;
}

void
ProxyTransaction::set_half_close_flag(bool flag)
{
  if (proxy_ssn) {
    proxy_ssn->set_half_close_flag(flag);
  }
}

bool
ProxyTransaction::get_half_close_flag() const
{
  return proxy_ssn ? proxy_ssn->get_half_close_flag() : false;
}

// What are the debug and hooks_enabled used for?  How are they set?
// Just calling through to proxy session for now
bool
ProxyTransaction::debug() const
{
  return proxy_ssn ? proxy_ssn->debug() : false;
}

APIHook *
ProxyTransaction::hook_get(TSHttpHookID id) const
{
  return proxy_ssn ? proxy_ssn->hook_get(id) : nullptr;
}

HttpAPIHooks const *
ProxyTransaction::feature_hooks() const
{
  return proxy_ssn ? proxy_ssn->feature_hooks() : nullptr;
}

bool
ProxyTransaction::has_hooks() const
{
  return proxy_ssn->has_hooks();
}

void
ProxyTransaction::set_session_active()
{
  if (proxy_ssn) {
    proxy_ssn->set_session_active();
  }
}

void
ProxyTransaction::clear_session_active()
{
  if (proxy_ssn) {
    proxy_ssn->clear_session_active();
  }
}

const IpAllow::ACL &
ProxyTransaction::get_acl() const
{
  return proxy_ssn ? proxy_ssn->acl : IpAllow::DENY_ALL_ACL;
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

ProxySession *
ProxyTransaction::get_proxy_ssn()
{
  return proxy_ssn;
}

void
ProxyTransaction::set_proxy_ssn(ProxySession *new_proxy_ssn)
{
  proxy_ssn = new_proxy_ssn;
}

void
ProxyTransaction::set_h2c_upgrade_flag()
{
}

Http1ServerSession *
ProxyTransaction::get_server_session() const
{
  return proxy_ssn ? proxy_ssn->get_server_session() : nullptr;
}

HttpSM *
ProxyTransaction::get_sm() const
{
  return current_reader;
}

const char *
ProxyTransaction::get_protocol_string()
{
  return proxy_ssn ? proxy_ssn->get_protocol_string() : nullptr;
}

int
ProxyTransaction::populate_protocol(std::string_view *result, int size) const
{
  return proxy_ssn ? proxy_ssn->populate_protocol(result, size) : 0;
}

const char *
ProxyTransaction::protocol_contains(std::string_view tag_prefix) const
{
  return proxy_ssn ? proxy_ssn->protocol_contains(tag_prefix) : nullptr;
}
