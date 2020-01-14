/** @file

  ProxyTransaction - Base class for protocol client/server transactions.

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

#pragma once

#include "ProxySession.h"
#include <string_view>

class HttpSM;
class Http1ServerSession;

// Abstract Class for any transaction with-in the HttpSM
class ProxyTransaction : public VConnection
{
public:
  ProxyTransaction();

  /// Virtual Methods
  //
  virtual void new_transaction(bool from_early_data = false);
  virtual void attach_server_session(Http1ServerSession *ssession, bool transaction_done = true);
  Action *adjust_thread(Continuation *cont, int event, void *data);
  virtual void release(IOBufferReader *r);
  virtual void transaction_done() = 0;
  virtual void destroy();

  /// Virtual Accessors
  //
  virtual void set_active_timeout(ink_hrtime timeout_in)     = 0;
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;
  virtual void cancel_inactivity_timeout()                   = 0;
  virtual int get_transaction_id() const                     = 0;
  virtual int get_transaction_priority_weight() const;
  virtual int get_transaction_priority_dependence() const;
  virtual bool allow_half_open() const              = 0;
  virtual void increment_client_transactions_stat() = 0;
  virtual void decrement_client_transactions_stat() = 0;

  virtual NetVConnection *get_netvc() const;
  virtual bool is_first_transaction() const;
  virtual in_port_t get_outbound_port() const;
  virtual IpAddr get_outbound_ip4() const;
  virtual IpAddr get_outbound_ip6() const;
  virtual void set_outbound_port(in_port_t port);
  virtual void set_outbound_ip(const IpAddr &new_addr);
  virtual bool is_outbound_transparent() const;
  virtual void set_outbound_transparent(bool flag);

  virtual void set_session_active();
  virtual void clear_session_active();

  virtual bool get_half_close_flag() const;
  virtual bool is_chunked_encoding_supported() const;

  virtual void set_proxy_ssn(ProxySession *set_proxy_ssn);
  virtual void set_h2c_upgrade_flag();

  /// Non-Virtual Methods
  //
  const char *get_protocol_string();
  int populate_protocol(std::string_view *result, int size) const;
  const char *protocol_contains(std::string_view tag_prefix) const;

  /// Non-Virtual Accessors
  //
  bool is_transparent_passthrough_allowed();
  void set_half_close_flag(bool flag);

  bool debug() const;

  APIHook *hook_get(TSHttpHookID id) const;
  HttpAPIHooks const *feature_hooks() const;
  bool has_hooks() const;

  HostResStyle get_host_res_style() const;
  void set_host_res_style(HostResStyle style);

  const IpAllow::ACL &get_acl() const;

  ProxySession *get_proxy_ssn();
  Http1ServerSession *get_server_session() const;
  HttpSM *get_sm() const;

  // This function must return a non-negative number that is different for two in-progress transactions with the same proxy_ssn
  // session.
  //
  void set_rx_error_code(ProxyError e);
  void set_tx_error_code(ProxyError e);

  /// Variables
  //
  HttpSessionAccept::Options upstream_outbound_options; // overwritable copy of options

protected:
  ProxySession *_proxy_ssn = nullptr;
  HttpSM *_sm              = nullptr;
  IOBufferReader *_reader  = nullptr;

private:
};

////////////////////////////////////////////////////////////
// INLINE

inline bool
ProxyTransaction::is_transparent_passthrough_allowed()
{
  return upstream_outbound_options.f_transparent_passthrough;
}
inline bool
ProxyTransaction::is_chunked_encoding_supported() const
{
  return _proxy_ssn ? _proxy_ssn->is_chunked_encoding_supported() : false;
}
inline void
ProxyTransaction::set_half_close_flag(bool flag)
{
  if (_proxy_ssn) {
    _proxy_ssn->set_half_close_flag(flag);
  }
}

inline bool
ProxyTransaction::get_half_close_flag() const
{
  return _proxy_ssn ? _proxy_ssn->get_half_close_flag() : false;
}

// What are the debug and hooks_enabled used for?  How are they set?
// Just calling through to proxy session for now
inline bool
ProxyTransaction::debug() const
{
  return _proxy_ssn ? _proxy_ssn->debug() : false;
}

inline APIHook *
ProxyTransaction::hook_get(TSHttpHookID id) const
{
  return _proxy_ssn ? _proxy_ssn->hook_get(id) : nullptr;
}

inline HttpAPIHooks const *
ProxyTransaction::feature_hooks() const
{
  return _proxy_ssn ? _proxy_ssn->feature_hooks() : nullptr;
}

inline bool
ProxyTransaction::has_hooks() const
{
  return _proxy_ssn->has_hooks();
}

inline ProxySession *
ProxyTransaction::get_proxy_ssn()
{
  return _proxy_ssn;
}

inline void
ProxyTransaction::set_proxy_ssn(ProxySession *new_proxy_ssn)
{
  _proxy_ssn = new_proxy_ssn;
}

inline Http1ServerSession *
ProxyTransaction::get_server_session() const
{
  return _proxy_ssn ? _proxy_ssn->get_server_session() : nullptr;
}

inline HttpSM *
ProxyTransaction::get_sm() const
{
  return _sm;
}

inline const char *
ProxyTransaction::get_protocol_string()
{
  return _proxy_ssn ? _proxy_ssn->get_protocol_string() : nullptr;
}

inline int
ProxyTransaction::populate_protocol(std::string_view *result, int size) const
{
  return _proxy_ssn ? _proxy_ssn->populate_protocol(result, size) : 0;
}

inline const char *
ProxyTransaction::protocol_contains(std::string_view tag_prefix) const
{
  return _proxy_ssn ? _proxy_ssn->protocol_contains(tag_prefix) : nullptr;
}
