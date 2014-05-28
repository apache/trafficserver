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

#ifndef __PROXY_CLIENT_SESSION_H__
#define __PROXY_CLIENT_SESSION_H__

#include "libts.h"
#include "P_Net.h"
#include "InkAPIInternal.h"

// Emit a debug message conditional on whether this particular client session
// has debugging enabled. This should only be called from within a client session
// member function.
#define DebugSsn(ssn, tag, ...) DebugSpecific((ssn)->debug(), tag, __VA_ARGS__)

class ProxyClientSession : public VConnection
{
public:
  ProxyClientSession();

  virtual void destroy() = 0;
  virtual void start() = 0;

  virtual void new_connection(NetVConnection * new_vc, MIOBuffer * iobuf, IOBufferReader * reader, bool backdoor) = 0;

  virtual void ssn_hook_append(TSHttpHookID id, INKContInternal * cont) {
    this->api_hooks.prepend(id, cont);
  }

  virtual void ssn_hook_prepend(TSHttpHookID id, INKContInternal * cont) {
    this->api_hooks.prepend(id, cont);
  }

  APIHook * ssn_hook_get(TSHttpHookID id) const {
    return this->api_hooks.get(id);
  }

  void * get_user_arg(unsigned ix) const {
    ink_assert(ix < countof(user_args));
    return this->user_args[ix];
  }

  void set_user_arg(unsigned ix, void * arg) {
    ink_assert(ix < countof(user_args));
    user_args[ix] = arg;
  }

  // Return whether debugging is enabled for this session.
  bool debug() const {
    return this->debug_on;
  }

  bool hooks_enabled() const {
    return this->hooks_on;
  }

  bool has_hooks() const {
    return this->api_hooks.has_hooks() || http_global_hooks->has_hooks();
  }

  // Initiate an API hook invocation.
  void do_api_callout(TSHttpHookID id);

  void cleanup();

  static int64_t next_connection_id();

protected:

  // XXX Consider using a bitwise flags variable for the following flags, so that we can make the best
  // use of internal alignment padding.

  // Session specific debug flag.
  bool debug_on;
  bool hooks_on;

private:
  APIHookScope  api_scope;
  TSHttpHookID  api_hookid;
  APIHook *     api_current;
  HttpAPIHooks  api_hooks;
  void *        user_args[HTTP_SSN_TXN_MAX_USER_ARG];

  ProxyClientSession(ProxyClientSession &); // noncopyable
  ProxyClientSession& operator=(const ProxyClientSession &); // noncopyable

  int state_api_callout(int event, void * edata);
  void handle_api_return(int event);

  friend void TSHttpSsnDebugSet(TSHttpSsn, int);
};

#endif // __PROXY_CLIENT_SESSION_H__
