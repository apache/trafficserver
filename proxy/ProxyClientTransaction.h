/** @file

  ProxyClientTransaction - Base class for protocol client transactions.

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

#include "ProxyClientSession.h"
#include <string_view>

class HttpSM;
class HttpServerSession;
class ProxyClientTransaction : public VConnection
{
public:
  ProxyClientTransaction();

  // do_io methods implemented by subclasses

  virtual void new_transaction();

  virtual NetVConnection *
  get_netvc() const
  {
    return (parent) ? parent->get_netvc() : nullptr;
  }

  virtual void set_active_timeout(ink_hrtime timeout_in)     = 0;
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;
  virtual void cancel_inactivity_timeout()                   = 0;

  virtual bool attach_server_session(HttpServerSession *ssession, bool transaction_done = true);

  // See if we need to schedule on the primary thread for the transaction or change the thread that is associated with the VC.
  // If we reschedule, the scheduled action is returned.  Otherwise, NULL is returned
  Action *adjust_thread(Continuation *cont, int event, void *data);

  int
  get_transact_count() const
  {
    return parent ? parent->get_transact_count() : 0;
  }

  virtual bool
  is_first_transaction() const
  {
    return get_transact_count() == 1;
  }

  // Ask your session if this is allowed
  bool
  is_transparent_passthrough_allowed()
  {
    return parent ? parent->is_transparent_passthrough_allowed() : false;
  }

  virtual bool
  is_chunked_encoding_supported() const
  {
    return parent ? parent->is_chunked_encoding_supported() : false;
  }

  void
  set_half_close_flag(bool flag)
  {
    if (parent) {
      parent->set_half_close_flag(flag);
    }
  }
  virtual bool
  get_half_close_flag() const
  {
    return parent ? parent->get_half_close_flag() : false;
  }

  // What are the debug and hooks_enabled used for?  How are they set?
  // Just calling through to parent session for now
  bool
  debug() const
  {
    return parent ? parent->debug() : false;
  }
  bool
  hooks_enabled() const
  {
    return parent ? parent->hooks_enabled() : false;
  }

  APIHook *
  ssn_hook_get(TSHttpHookID id) const
  {
    return parent ? parent->ssn_hook_get(id) : nullptr;
  }

  bool
  has_hooks() const
  {
    return parent->has_hooks();
  }

  virtual void
  set_session_active()
  {
    if (parent) {
      parent->set_session_active();
    }
  }

  virtual void
  clear_session_active()
  {
    if (parent) {
      parent->clear_session_active();
    }
  }

  /// DNS resolution preferences.
  HostResStyle
  get_host_res_style() const
  {
    return host_res_style;
  }
  void
  set_host_res_style(HostResStyle style)
  {
    host_res_style = style;
  }

  // Indicate we are done with this transaction
  virtual void release(IOBufferReader *r);

  // outbound values Set via the server port definition.  Really only used for Http1 at the moment
  virtual in_port_t
  get_outbound_port() const
  {
    return outbound_port;
  }
  virtual IpAddr
  get_outbound_ip4() const
  {
    return outbound_ip4;
  }
  virtual IpAddr
  get_outbound_ip6() const
  {
    return outbound_ip6;
  }
  virtual void
  set_outbound_port(in_port_t port)
  {
    outbound_port = port;
  }
  virtual void
  set_outbound_ip(const IpAddr &new_addr)
  {
    if (new_addr.isIp4()) {
      outbound_ip4 = new_addr;
    } else if (new_addr.isIp6()) {
      outbound_ip6 = new_addr;
    } else {
      outbound_ip4.invalidate();
      outbound_ip6.invalidate();
    }
  }
  virtual bool
  is_outbound_transparent() const
  {
    return false;
  }
  virtual void
  set_outbound_transparent(bool flag)
  {
  }

  virtual void destroy();

  virtual void transaction_done() = 0;

  ProxyClientSession *
  get_parent()
  {
    return parent;
  }

  virtual void
  set_parent(ProxyClientSession *new_parent)
  {
    parent         = new_parent;
    host_res_style = parent->host_res_style;
  }
  virtual void
  set_h2c_upgrade_flag()
  {
  }

  HttpServerSession *
  get_server_session() const
  {
    return parent ? parent->get_server_session() : nullptr;
  }

  HttpSM *
  get_sm() const
  {
    return current_reader;
  }

  virtual int get_transaction_priority_weight() const;
  virtual int get_transaction_priority_dependence() const;
  virtual bool allow_half_open() const = 0;

  // Returns true if there is a request body for this request
  virtual bool has_request_body(int64_t content_length, bool is_chunked_set) const;

  virtual const char *
  get_protocol_string()
  {
    return parent ? parent->get_protocol_string() : nullptr;
  }

  void
  set_restart_immediate(bool val)
  {
    restart_immediate = true;
  }
  bool
  get_restart_immediate() const
  {
    return restart_immediate;
  }

  virtual int
  populate_protocol(std::string_view *result, int size) const
  {
    return parent ? parent->populate_protocol(result, size) : 0;
  }

  virtual const char *
  protocol_contains(std::string_view tag_prefix) const
  {
    return parent ? parent->protocol_contains(tag_prefix) : nullptr;
  }

  // This function must return a non-negative number that is different for two in-progress transactions with the same parent
  // session.
  //
  virtual int get_transaction_id() const = 0;
  void set_rx_error_code(ProxyError e);
  void set_tx_error_code(ProxyError e);

protected:
  ProxyClientSession *parent;
  HttpSM *current_reader;
  IOBufferReader *sm_reader;

  /// DNS resolution preferences.
  HostResStyle host_res_style;
  /// Local outbound address control.
  in_port_t outbound_port{0};
  IpAddr outbound_ip4;
  IpAddr outbound_ip6;

  bool restart_immediate;

private:
};
