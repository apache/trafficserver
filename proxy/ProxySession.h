/** @file

  ProxySession - Base class for protocol client sessions.

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

#include "tscore/ink_platform.h"
#include "tscore/ink_resolver.h"
#include "tscore/TSSystemState.h"
#include <string_view>
#include <memory>
#include "P_Net.h"
#include "InkAPIInternal.h"
#include "http/Http1ServerSession.h"
#include "http/HttpSessionAccept.h"
#include "IPAllow.h"
#include "private/SSLProxySession.h"

// Emit a debug message conditional on whether this particular client session
// has debugging enabled. This should only be called from within a client session
// member function.
#define SsnDebug(ssn, tag, ...) SpecificDebug((ssn)->debug(), tag, __VA_ARGS__)

class ProxyTransaction;

enum class ProxyErrorClass {
  NONE,
  SSN,
  TXN,
};

struct ProxyError {
  ProxyError() {}
  ProxyError(ProxyErrorClass cl, uint32_t co) : cls(cl), code(co) {}
  size_t
  str(char *buf, size_t buf_len) const
  {
    size_t len = 0;

    if (this->cls == ProxyErrorClass::NONE) {
      buf[0] = '-';
      return 1;
    }

    buf[0] = (this->cls == ProxyErrorClass::SSN) ? 'S' : 'T';
    ++len;

    len += snprintf(buf + len, buf_len - len, "%" PRIx32, this->code);

    return len;
  }

  ProxyErrorClass cls = ProxyErrorClass::NONE;
  uint32_t code       = 0;
};

/// Abstract class for HttpSM to interface with any session
class ProxySession : public VConnection
{
public:
  ProxySession();

  // noncopyable
  ProxySession(ProxySession &) = delete;
  ProxySession &operator=(const ProxySession &) = delete;

  static int64_t next_connection_id();

  // Virtual Methods
  virtual void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) = 0;
  virtual void start()                                                                          = 0;
  virtual void attach_server_session(Http1ServerSession *ssession, bool transaction_done = true);

  virtual void release(ProxyTransaction *trans) = 0;

  virtual void destroy() = 0;
  virtual void free();

  virtual void increment_current_active_client_connections_stat() = 0;
  virtual void decrement_current_active_client_connections_stat() = 0;

  // Virtual Accessors
  virtual NetVConnection *get_netvc() const       = 0;
  virtual int get_transact_count() const          = 0;
  virtual const char *get_protocol_string() const = 0;

  virtual void hook_add(TSHttpHookID id, INKContInternal *cont);

  virtual bool is_chunked_encoding_supported() const;

  virtual void set_half_close_flag(bool flag);
  virtual bool get_half_close_flag() const;

  virtual Http1ServerSession *get_server_session() const;

  // Replicate NetVConnection API
  virtual sockaddr const *get_client_addr();
  virtual sockaddr const *get_local_addr();

  virtual void set_active_timeout(ink_hrtime timeout_in);
  virtual void set_inactivity_timeout(ink_hrtime timeout_in);
  virtual void cancel_inactivity_timeout();

  virtual int populate_protocol(std::string_view *result, int size) const;
  virtual const char *protocol_contains(std::string_view tag_prefix) const;

  // Non-Virtual Methods
  int do_api_callout(TSHttpHookID id);

  // Non-Virtual Accessors
  void *get_user_arg(unsigned ix) const;
  void set_user_arg(unsigned ix, void *arg);

  void set_debug(bool flag);
  bool debug() const;

  void set_session_active();
  void clear_session_active();
  bool is_active() const;
  bool is_draining() const;
  bool is_client_closed() const;

  int64_t connection_id() const;
  TSHttpHookID get_hookid() const;
  bool has_hooks() const;

  APIHook *hook_get(TSHttpHookID id) const;
  HttpAPIHooks const *feature_hooks() const;

  // Returns null pointer if session does not use a TLS connection.
  SSLProxySession const *ssl() const;

  ////////////////////
  // Members

  IpAllow::ACL acl; ///< IpAllow based method ACL.

  HttpSessionAccept::Options const *accept_options; ///< connection info // L7R TODO: set in constructor

  ink_hrtime ssn_start_time    = 0;
  ink_hrtime ssn_last_txn_time = 0;

protected:
  // Hook dispatching state
  HttpHookState hook_state;

  // XXX Consider using a bitwise flags variable for the following flags, so
  // that we can make the best use of internal alignment padding.

  // Session specific debug flag.
  bool debug_on   = false;
  bool in_destroy = false;

  int64_t con_id        = 0;
  Event *schedule_event = nullptr;

  // This function should be called in all overrides of new_connection() where
  // the new_vc may be an SSLNetVConnection object.
  void _handle_if_ssl(NetVConnection *new_vc);

private:
  void handle_api_return(int event);
  int state_api_callout(int event, void *edata);

  APIHook const *cur_hook = nullptr;
  HttpAPIHooks api_hooks;
  void *user_args[TS_HTTP_MAX_USER_ARG];

  // for DI. An active connection is one that a request has
  // been successfully parsed (PARSE_DONE) and it remains to
  // be active until the transaction goes through or the client
  // aborts.
  bool m_active = false;

  std::unique_ptr<SSLProxySession> _ssl;
};

///////////////////
// INLINE

static inline int64_t next_cs_id = 0;

inline int64_t
ProxySession::next_connection_id()
{
  return ink_atomic_increment(&next_cs_id, 1);
}

inline void *
ProxySession::get_user_arg(unsigned ix) const
{
  ink_assert(ix < countof(user_args));
  return this->user_args[ix];
}

inline void
ProxySession::set_user_arg(unsigned ix, void *arg)
{
  ink_assert(ix < countof(user_args));
  user_args[ix] = arg;
}

inline void
ProxySession::set_debug(bool flag)
{
  debug_on = flag;
}

// Return whether debugging is enabled for this session.
inline bool
ProxySession::debug() const
{
  return this->debug_on;
}

inline bool
ProxySession::is_active() const
{
  return m_active;
}

inline bool
ProxySession::is_draining() const
{
  return TSSystemState::is_draining();
}

inline bool
ProxySession::is_client_closed() const
{
  return get_netvc() == nullptr;
}

inline TSHttpHookID
ProxySession::get_hookid() const
{
  return hook_state.id();
}

inline void
ProxySession::hook_add(TSHttpHookID id, INKContInternal *cont)
{
  this->api_hooks.append(id, cont);
}

inline APIHook *
ProxySession::hook_get(TSHttpHookID id) const
{
  return this->api_hooks.get(id);
}

inline HttpAPIHooks const *
ProxySession::feature_hooks() const
{
  return &api_hooks;
}

inline bool
ProxySession::has_hooks() const
{
  return this->api_hooks.has_hooks() || http_global_hooks->has_hooks();
}

inline SSLProxySession const *
ProxySession::ssl() const
{
  return _ssl.get();
}
