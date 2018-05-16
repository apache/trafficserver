/** @file

  ProxyClientSession - Base class for protocol client sessions.

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

#include <ts/ink_platform.h>
#include <ts/ink_resolver.h>
#include <string_view>
#include "P_Net.h"
#include "InkAPIInternal.h"
#include "http/HttpServerSession.h"

// Emit a debug message conditional on whether this particular client session
// has debugging enabled. This should only be called from within a client session
// member function.
#define SsnDebug(ssn, tag, ...) SpecificDebug((ssn)->debug(), tag, __VA_ARGS__)

class ProxyClientTransaction;
struct AclRecord;

enum class ProxyErrorClass {
  NONE,
  SSN,
  TXN,
};

struct ProxyError {
  ProxyError() {}
  ProxyError(ProxyErrorClass cl, uint32_t co) : cls(cl), code(co) {}
  size_t str(char *buf, size_t buf_len);

  ProxyErrorClass cls = ProxyErrorClass::NONE;
  uint32_t code       = 0;
};

class ProxyClientSession : public VConnection
{
public:
  ProxyClientSession();

  virtual void destroy() = 0;
  virtual void free();
  virtual void start() = 0;

  virtual void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor) = 0;

  virtual NetVConnection *get_netvc() const = 0;
  virtual void release_netvc()              = 0;

  virtual int get_transact_count() const = 0;

  virtual const char *get_protocol_string() const = 0;

  virtual void
  ssn_hook_append(TSHttpHookID id, INKContInternal *cont)
  {
    this->api_hooks.append(id, cont);
  }

  virtual void
  ssn_hook_prepend(TSHttpHookID id, INKContInternal *cont)
  {
    this->api_hooks.prepend(id, cont);
  }

  APIHook *
  ssn_hook_get(TSHttpHookID id) const
  {
    return this->api_hooks.get(id);
  }

  void *
  get_user_arg(unsigned ix) const
  {
    ink_assert(ix < countof(user_args));
    return this->user_args[ix];
  }

  void
  set_user_arg(unsigned ix, void *arg)
  {
    ink_assert(ix < countof(user_args));
    user_args[ix] = arg;
  }

  void
  set_debug(bool flag)
  {
    debug_on = flag;
  }

  // Return whether debugging is enabled for this session.
  bool
  debug() const
  {
    return this->debug_on;
  }

  bool
  hooks_enabled() const
  {
    return this->hooks_on;
  }

  bool
  has_hooks() const
  {
    return this->api_hooks.has_hooks() || http_global_hooks->has_hooks();
  }

  bool
  is_active() const
  {
    return m_active;
  }

  bool
  is_draining() const
  {
    RecInt draining;
    if (RecGetRecordInt("proxy.node.config.draining", &draining) != REC_ERR_OKAY) {
      return false;
    }
    return draining != 0;
  }

  // Initiate an API hook invocation.
  void do_api_callout(TSHttpHookID id);

  // Override if your session protocol allows this.
  virtual bool
  is_transparent_passthrough_allowed() const
  {
    return false;
  }

  virtual bool
  is_chunked_encoding_supported() const
  {
    return false;
  }

  // Override if your session protocol cares.
  virtual void
  set_half_close_flag(bool flag)
  {
  }

  virtual bool
  get_half_close_flag() const
  {
    return false;
  }

  // Indicate we are done with a transaction.
  virtual void release(ProxyClientTransaction *trans) = 0;

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

  int64_t
  connection_id() const
  {
    return con_id;
  }

  virtual void
  attach_server_session(HttpServerSession *ssession, bool transaction_done = true)
  {
  }

  virtual HttpServerSession *
  get_server_session() const
  {
    return nullptr;
  }

  TSHttpHookID
  get_hookid() const
  {
    return api_hookid;
  }

  virtual void
  set_active_timeout(ink_hrtime timeout_in)
  {
  }

  virtual void
  set_inactivity_timeout(ink_hrtime timeout_in)
  {
  }

  virtual void
  cancel_inactivity_timeout()
  {
  }

  bool
  is_client_closed() const
  {
    return get_netvc() == nullptr;
  }

  virtual int
  populate_protocol(std::string_view *result, int size) const
  {
    auto vc = this->get_netvc();
    return vc ? vc->populate_protocol(result, size) : 0;
  }

  virtual const char *
  protocol_contains(std::string_view tag_prefix) const
  {
    auto vc = this->get_netvc();
    return vc ? vc->protocol_contains(tag_prefix) : nullptr;
  }

  void set_session_active();
  void clear_session_active();

  static int64_t next_connection_id();

  virtual sockaddr const *
  get_client_addr()
  {
    NetVConnection *netvc = get_netvc();
    return netvc ? netvc->get_remote_addr() : nullptr;
  }
  virtual sockaddr const *
  get_local_addr()
  {
    NetVConnection *netvc = get_netvc();
    return netvc ? netvc->get_local_addr() : nullptr;
  }

  /// acl record - cache IpAllow::match() call
  const AclRecord *acl_record = nullptr;

  /// Local address for outbound connection.
  IpAddr outbound_ip4;
  /// Local address for outbound connection.
  IpAddr outbound_ip6;
  /// Local port for outbound connection.
  in_port_t outbound_port{0};

  /// DNS resolution preferences.
  HostResStyle host_res_style = HOST_RES_IPV4;

  ink_hrtime ssn_start_time    = 0;
  ink_hrtime ssn_last_txn_time = 0;

  // noncopyable
  ProxyClientSession(ProxyClientSession &) = delete;
  ProxyClientSession &operator=(const ProxyClientSession &) = delete;

protected:
  // XXX Consider using a bitwise flags variable for the following flags, so
  // that we can make the best use of internal alignment padding.

  // Session specific debug flag.
  bool debug_on   = false;
  bool hooks_on   = true;
  bool in_destroy = false;

  int64_t con_id        = 0;
  Event *schedule_event = nullptr;

private:
  void handle_api_return(int event);
  int state_api_callout(int event, void *edata);

  APIHookScope api_scope  = API_HOOK_SCOPE_NONE;
  TSHttpHookID api_hookid = TS_HTTP_READ_REQUEST_HDR_HOOK;
  APIHook *api_current    = nullptr;
  HttpAPIHooks api_hooks;
  void *user_args[TS_HTTP_MAX_USER_ARG];

  // for DI. An active connection is one that a request has
  // been successfully parsed (PARSE_DONE) and it remains to
  // be active until the transaction goes through or the client
  // aborts.
  bool m_active = false;
};
