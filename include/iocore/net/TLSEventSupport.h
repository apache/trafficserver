/** @file

  TLSEventSupport implements common methods and members to
  support TLS related events

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

#include "iocore/eventsystem/Lock.h"

class Continuation;

class TLSEventSupport
{
public:
  enum class SSLHandshakeHookState {
    HANDSHAKE_HOOKS_PRE,
    HANDSHAKE_HOOKS_PRE_INVOKE,
    HANDSHAKE_HOOKS_CLIENT_HELLO,
    HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE,
    HANDSHAKE_HOOKS_SNI,
    HANDSHAKE_HOOKS_CERT,
    HANDSHAKE_HOOKS_CERT_INVOKE,
    HANDSHAKE_HOOKS_CLIENT_CERT,
    HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE,
    HANDSHAKE_HOOKS_OUTBOUND_PRE,
    HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE,
    HANDSHAKE_HOOKS_VERIFY_SERVER,
    HANDSHAKE_HOOKS_DONE
  };

  static char const *get_ssl_handshake_hook_state_name(SSLHandshakeHookState state);

  virtual ~TLSEventSupport() = default;

  static void             initialize();
  static TLSEventSupport *getInstance(SSL *ssl);
  static void             bind(SSL *ssl, TLSEventSupport *es);
  static void             unbind(SSL *ssl);

  bool                    callHooks(TSEvent eventId);
  bool                    calledHooks(TSEvent eventId) const;
  virtual Continuation   *getContinuationForTLSEvents() = 0;
  virtual EThread        *getThreadForTLSEvents()       = 0;
  virtual Ptr<ProxyMutex> getMutexForTLSEvents()        = 0;
  virtual void            reenable(int event)           = 0;

protected:
  void clear();

  SSLHandshakeHookState get_handshake_hook_state();
  void                  set_handshake_hook_state(SSLHandshakeHookState state);
  bool                  is_invoked_state() const;
  int                   invoke_tls_event();
  void                  resume_tls_event();

  virtual bool _is_tunneling_requested()   = 0;
  virtual void _switch_to_tunneling_mode() = 0;

private:
  static int _ex_data_index;
  SSL       *_ssl;

  bool _first_handshake_hooks_pre          = true;
  bool _first_handshake_hooks_outbound_pre = true;

  /// The current hook.
  /// @note For @C SSL_HOOKS_INVOKE, this is the hook to invoke.
  class APIHook        *curHook               = nullptr;
  SSLHandshakeHookState sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE;
};
