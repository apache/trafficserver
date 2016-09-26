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

#ifndef __PROXY_CLIENT_TRANSACTION_H__
#define __PROXY_CLIENT_TRANSACTION_H__

#include "ProxyClientSession.h"

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
    return (parent) ? parent->get_netvc() : NULL;
  }

  virtual void set_active_timeout(ink_hrtime timeout_in)     = 0;
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;
  virtual void cancel_inactivity_timeout()                   = 0;

  virtual void attach_server_session(HttpServerSession *ssession, bool transaction_done = true);

  // See if we need to schedule on the primary thread for the transaction or change the thread that is associated with the VC.
  // If we reschedule, the scheduled action is returned.  Otherwise, NULL is returned
  Action *adjust_thread(Continuation *cont, int event, void *data);

  int
  get_transact_count() const
  {
    return parent ? parent->get_transact_count() : 0;
  }

  // Ask your session if this is allowed
  bool
  is_transparent_passthrough_allowed()
  {
    return parent ? parent->is_transparent_passthrough_allowed() : false;
  }

  void
  set_half_close_flag(bool flag)
  {
    if (parent)
      parent->set_half_close_flag(flag);
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
    return parent ? parent->ssn_hook_get(id) : NULL;
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

  const AclRecord *
  get_acl_record() const
  {
    return parent ? parent->acl_record : NULL;
  }

  // Indicate we are done with this transaction
  virtual void release(IOBufferReader *r);

  // outbound values Set via the server port definition.  Really only used for Http1 at the moment
  virtual uint16_t
  get_outbound_port() const
  {
    return 0;
  }
  virtual IpAddr
  get_outbound_ip4() const
  {
    return IpAddr();
  }
  virtual IpAddr
  get_outbound_ip6() const
  {
    return IpAddr();
  }
  virtual void
  set_outbound_port(uint16_t new_port)
  {
  }
  virtual void
  set_outbound_ip(const IpAddr &new_addr)
  {
  }
  virtual void
  clear_outbound()
  {
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

  virtual bool
  ignore_keep_alive()
  {
    return true;
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
    return parent ? parent->get_server_session() : NULL;
  }

  HttpSM *
  get_sm() const
  {
    return current_reader;
  }

  virtual bool allow_half_open() const = 0;

  virtual const char *get_protocol_string() const = 0;

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

protected:
  ProxyClientSession *parent;
  HttpSM *current_reader;
  IOBufferReader *sm_reader;

  /// DNS resolution preferences.
  HostResStyle host_res_style;

  bool restart_immediate;

private:
};

#endif /* __PROXY_CLIENT_TRANSACTION_H__ */
