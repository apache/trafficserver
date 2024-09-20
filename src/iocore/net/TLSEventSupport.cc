/** @file

  TLSSEventSupport.cc provides implementations for
  TLSEventSupport methods

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

#include <openssl/ssl.h>
#include "iocore/net/TLSEventSupport.h"
#include "iocore/net/SSLAPIHooks.h"
#include "tscore/Diags.h"
#include "SSLStats.h"

int TLSEventSupport::_ex_data_index = -1;

namespace
{
DbgCtl dbg_ctl_ssl{"ssl"};

/// Callback to get two locks.
/// The lock for this continuation, and for the target continuation.
class ContWrapper : public Continuation
{
public:
  /** Constructor.
      This takes the secondary @a mutex and the @a target continuation
      to invoke, along with the arguments for that invocation.
  */
  ContWrapper(ProxyMutex   *mutex,                     ///< Mutex for this continuation (primary lock).
              Continuation *target,                    ///< "Real" continuation we want to call.
              int           eventId = EVENT_IMMEDIATE, ///< Event ID for invocation of @a target.
              void         *edata   = nullptr          ///< Data for invocation of @a target.
              )
    : Continuation(mutex), _target(target), _eventId(eventId), _edata(edata)
  {
    SET_HANDLER(&ContWrapper::event_handler);
  }

  /// Required event handler method.
  int
  event_handler(int, void *)
  {
    EThread *eth = this_ethread();

    MUTEX_TRY_LOCK(lock, _target->mutex, eth);
    if (lock.is_locked()) { // got the target lock, we can proceed.
      _target->handleEvent(_eventId, _edata);
      delete this;
    } else { // can't get both locks, try again.
      eventProcessor.schedule_imm(this, ET_NET);
    }
    return 0;
  }

  /** Convenience static method.

      This lets a client make one call and not have to (accurately)
      copy the invocation logic embedded here. We duplicate it near
      by textually so it is easier to keep in sync.

      This takes the same arguments as the constructor but, if the
      lock can be obtained immediately, does not construct an
      instance but simply calls the @a target.
  */
  static void
  wrap(ProxyMutex   *mutex,                     ///< Mutex for this continuation (primary lock).
       Continuation *target,                    ///< "Real" continuation we want to call.
       int           eventId = EVENT_IMMEDIATE, ///< Event ID for invocation of @a target.
       void         *edata   = nullptr          ///< Data for invocation of @a target.
  )
  {
    EThread *eth = this_ethread();
    if (!target->mutex) {
      // If there's no mutex, plugin doesn't care about locking so why should we?
      target->handleEvent(eventId, edata);
    } else {
      MUTEX_TRY_LOCK(lock, target->mutex, eth);
      if (lock.is_locked()) {
        target->handleEvent(eventId, edata);
      } else {
        eventProcessor.schedule_imm(new ContWrapper(mutex, target, eventId, edata), ET_NET);
      }
    }
  }

private:
  Continuation *_target;  ///< Continuation to invoke.
  int           _eventId; ///< with this event
  void         *_edata;   ///< and this data
};

} // end anonymous namespace

void
TLSEventSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSEventSupport index", nullptr, nullptr, nullptr);
  }
}

TLSEventSupport *
TLSEventSupport::getInstance(SSL *ssl)
{
  return static_cast<TLSEventSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSEventSupport::bind(SSL *ssl, TLSEventSupport *es)
{
  SSL_set_ex_data(ssl, _ex_data_index, es);
  es->_ssl = ssl;
}

void
TLSEventSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
TLSEventSupport::clear()
{
  curHook = nullptr;
}

bool
TLSEventSupport::callHooks(TSEvent eventId)
{
  // Only dealing with the SNI/CERT hook so far.
  ink_assert(eventId == TS_EVENT_SSL_CLIENT_HELLO || eventId == TS_EVENT_SSL_CERT || eventId == TS_EVENT_SSL_SERVERNAME ||
             eventId == TS_EVENT_SSL_VERIFY_SERVER || eventId == TS_EVENT_SSL_VERIFY_CLIENT || eventId == TS_EVENT_VCONN_CLOSE ||
             eventId == TS_EVENT_VCONN_OUTBOUND_CLOSE);
  Dbg(dbg_ctl_ssl, "sslHandshakeHookState=%s eventID=%d", get_ssl_handshake_hook_state_name(this->sslHandshakeHookState), eventId);

  // Move state if it is appropriate
  if (eventId == TS_EVENT_VCONN_CLOSE) {
    // Regardless of state, if the connection is closing, then transition to
    // the DONE state. This will trigger us to call the appropriate cleanup
    // routines.
    this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE;
  } else {
    switch (this->sslHandshakeHookState) {
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE:
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE:
      if (eventId == TS_EVENT_SSL_CLIENT_HELLO) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO;
      } else if (eventId == TS_EVENT_SSL_SERVERNAME) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI;
      } else if (eventId == TS_EVENT_SSL_VERIFY_SERVER) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER;
      } else if (eventId == TS_EVENT_SSL_CERT) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT;
      }
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO:
      if (eventId == TS_EVENT_SSL_SERVERNAME) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI;
      } else if (eventId == TS_EVENT_SSL_CERT) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT;
      }
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI:
      if (eventId == TS_EVENT_SSL_CERT) {
        this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT;
      }
      break;
    default:
      break;
    }
  }

  // Look for hooks associated with the event
  switch (this->sslHandshakeHookState) {
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    if (!curHook) {
      curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_CLIENT_HELLO_HOOK));
    } else {
      curHook = curHook->next();
    }
    if (curHook == nullptr) {
      this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI;
    } else {
      this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE;
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER:
    // The server verify event addresses ATS to origin handshake
    // All the other events are for client to ATS
    if (!curHook) {
      curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_VERIFY_SERVER_HOOK));
    } else {
      curHook = curHook->next();
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI:
    if (!curHook) {
      curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_SERVERNAME_HOOK));
    } else {
      curHook = curHook->next();
    }
    if (!curHook) {
      this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT;
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE:
    if (!curHook) {
      curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_CERT_HOOK));
    } else {
      curHook = curHook->next();
    }
    if (curHook == nullptr) {
      this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT;
    } else {
      this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE;
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
    if (!curHook) {
      curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_SSL_VERIFY_CLIENT_HOOK));
    } else {
      curHook = curHook->next();
    }
    [[fallthrough]];
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE:
    if (eventId == TS_EVENT_VCONN_CLOSE) {
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE;
      if (curHook == nullptr) {
        curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_VCONN_CLOSE_HOOK));
      } else {
        curHook = curHook->next();
      }
    } else if (eventId == TS_EVENT_VCONN_OUTBOUND_CLOSE) {
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE;
      if (curHook == nullptr) {
        curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_VCONN_OUTBOUND_CLOSE_HOOK));
      } else {
        curHook = curHook->next();
      }
    }
    break;
  default:
    curHook                     = nullptr;
    this->sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE;
    return true;
  }

  Dbg(dbg_ctl_ssl, "iterated to curHook=%p", curHook);

  bool reenabled = true;

  if (this->_is_tunneling_requested()) {
    this->_switch_to_tunneling_mode();
    // Don't mark the handshake as complete yet,
    // Will be checking for that flag not being set after
    // we get out of this callback, and then will shuffle
    // over the buffered handshake packets to the O.S.
    // sslHandShakeComplete = 1;
    return reenabled;
  }

  if (curHook != nullptr) {
    WEAK_SCOPED_MUTEX_LOCK(lock, curHook->m_cont->mutex, this_ethread());
    curHook->invoke(eventId, this->getContinuationForTLSEvents());
    reenabled = (this->sslHandshakeHookState != SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE &&
                 this->sslHandshakeHookState != SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE &&
                 this->sslHandshakeHookState != SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE);
    Dbg(dbg_ctl_ssl, "Called hook on state=%s reenabled=%d", get_ssl_handshake_hook_state_name(sslHandshakeHookState), reenabled);
  }

  return reenabled;
}

// Returns true if we have already called at
// least some of the hooks
bool
TLSEventSupport::calledHooks(TSEvent eventId) const
{
  bool retval = false;
  switch (this->sslHandshakeHookState) {
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE:
    if (eventId == TS_EVENT_VCONN_START) {
      if (curHook) {
        retval = true;
      }
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    if (eventId == TS_EVENT_VCONN_START) {
      retval = true;
    } else if (eventId == TS_EVENT_SSL_CLIENT_HELLO) {
      if (curHook) {
        retval = true;
      }
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI:
    if (eventId == TS_EVENT_VCONN_START || eventId == TS_EVENT_SSL_CLIENT_HELLO) {
      retval = true;
    } else if (eventId == TS_EVENT_SSL_SERVERNAME) {
      if (curHook) {
        retval = true;
      }
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE:
    if (eventId == TS_EVENT_VCONN_START || eventId == TS_EVENT_SSL_CLIENT_HELLO || eventId == TS_EVENT_SSL_SERVERNAME) {
      retval = true;
    } else if (eventId == TS_EVENT_SSL_CERT) {
      if (curHook) {
        retval = true;
      }
    }
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
    if (eventId == TS_EVENT_SSL_VERIFY_CLIENT || eventId == TS_EVENT_VCONN_START) {
      retval = true;
    }
    break;

  case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
    if (eventId == TS_EVENT_VCONN_OUTBOUND_START) {
      if (curHook) {
        retval = true;
      }
    }
    break;

  case SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER:
    retval = (eventId == TS_EVENT_SSL_VERIFY_SERVER);
    break;

  case SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE:
    retval = true;
    break;
  }
  return retval;
}

char const *
TLSEventSupport::get_ssl_handshake_hook_state_name(SSLHandshakeHookState state)
{
  switch (state) {
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE:
    return "TS_SSL_HOOK_PRE_ACCEPT";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE:
    return "TS_SSL_HOOK_PRE_ACCEPT_INVOKE";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO:
    return "TS_SSL_HOOK_CLIENT_HELLO";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    return "TS_SSL_HOOK_CLIENT_HELLO_INVOKE";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI:
    return "TS_SSL_HOOK_SERVERNAME";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT:
    return "TS_SSL_HOOK_CERT";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE:
    return "TS_SSL_HOOK_CERT_INVOKE";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT:
    return "TS_SSL_HOOK_CLIENT_CERT";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
    return "TS_SSL_HOOK_CLIENT_CERT_INVOKE";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE:
    return "TS_SSL_HOOK_PRE_CONNECT";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
    return "TS_SSL_HOOK_PRE_CONNECT_INVOKE";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER:
    return "TS_SSL_HOOK_VERIFY_SERVER";
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE:
    return "TS_SSL_HOOKS_DONE";
  }
  return "unknown handshake hook name";
}

TLSEventSupport::SSLHandshakeHookState
TLSEventSupport::get_handshake_hook_state()
{
  return this->sslHandshakeHookState;
}

void
TLSEventSupport::set_handshake_hook_state(TLSEventSupport::SSLHandshakeHookState state)
{
  this->sslHandshakeHookState = state;
}

bool
TLSEventSupport::is_invoked_state() const
{
  return sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE ||
         sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE ||
         sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE ||
         sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE ||
         sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE;
}

int
TLSEventSupport::invoke_tls_event()
{
  if (curHook != nullptr) {
    curHook = curHook->next();
    Dbg(dbg_ctl_ssl, "iterate from reenable curHook=%p", curHook);
  }
  if (curHook != nullptr) {
    // Invoke the hook and return, wait for next reenable
    if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO) {
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE;
      curHook->invoke(TS_EVENT_SSL_CLIENT_HELLO, this->getContinuationForTLSEvents());
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT) {
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE;
      curHook->invoke(TS_EVENT_SSL_VERIFY_CLIENT, this->getContinuationForTLSEvents());
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT) {
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE;
      curHook->invoke(TS_EVENT_SSL_CERT, this->getContinuationForTLSEvents());
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI) {
      curHook->invoke(TS_EVENT_SSL_SERVERNAME, this->getContinuationForTLSEvents());
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE) {
      Dbg(dbg_ctl_ssl, "Reenable preaccept");
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE;
      ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_VCONN_START,
                        this->getContinuationForTLSEvents());
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE) {
      Dbg(dbg_ctl_ssl, "Reenable outbound connect");
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE;
      ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_START,
                        this->getContinuationForTLSEvents());
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE) {
      if (SSL_is_server(this->_ssl)) {
        ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_VCONN_CLOSE,
                          this->getContinuationForTLSEvents());
      } else {
        ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_CLOSE,
                          this->getContinuationForTLSEvents());
      }
    } else if (sslHandshakeHookState == SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER) {
      Dbg(dbg_ctl_ssl, "ServerVerify");
      ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_SSL_VERIFY_SERVER,
                        this->getContinuationForTLSEvents());
    }
    return 1;
  } else {
    // Move onto the "next" state
    switch (this->sslHandshakeHookState) {
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE:
      if (this->_first_handshake_hooks_pre) {
        this->_first_handshake_hooks_pre = false;
        Metrics::Counter::increment(ssl_rsb.total_attempts_handshake_count_in);
        Dbg(dbg_ctl_ssl, "Initialize preaccept curHook from NULL");
        curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_VCONN_START_HOOK));
        if (curHook) {
          sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE;
          ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_VCONN_START,
                            this->getContinuationForTLSEvents());
          return 1;
        }
      }
      [[fallthrough]];
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO;
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO:
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI;
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_SNI:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT;
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT:
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT;
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE:
      if (this->_first_handshake_hooks_outbound_pre) {
        this->_first_handshake_hooks_outbound_pre = false;
        Metrics::Counter::increment(ssl_rsb.total_attempts_handshake_count_out);
        Dbg(dbg_ctl_ssl, "Initialize outbound connect curHook from NULL");
        curHook = SSLAPIHooks::instance()->get(TSSslHookInternalID(TS_VCONN_OUTBOUND_START_HOOK));
        if (curHook) {
          sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE;
          ContWrapper::wrap(this->getMutexForTLSEvents().get(), curHook->m_cont, TS_EVENT_VCONN_OUTBOUND_START,
                            this->getContinuationForTLSEvents());
          return 1;
        }
      }
      [[fallthrough]];
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE;
      return 2;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT:
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE;
      break;
    case SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER:
      sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_DONE;
      break;
    default:
      break;
    }
    Dbg(dbg_ctl_ssl, "iterate from reenable curHook=%p %s", curHook,
        TLSEventSupport::get_ssl_handshake_hook_state_name(sslHandshakeHookState));
  }
  return 0;
}

void
TLSEventSupport::resume_tls_event()
{
  switch (this->sslHandshakeHookState) {
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE_INVOKE:
    sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_PRE;
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE_INVOKE:
    sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_OUTBOUND_PRE;
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO_INVOKE:
    sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_HELLO;
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT_INVOKE:
    sslHandshakeHookState = SSLHandshakeHookState::HANDSHAKE_HOOKS_CERT;
    break;
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_VERIFY_SERVER:
  case SSLHandshakeHookState::HANDSHAKE_HOOKS_CLIENT_CERT:
    break;
  default:
    break;
  }
}
