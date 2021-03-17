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

#include "HttpConfig.h"
#include "HttpDebugNames.h"
#include "ProxySession.h"
#include "P_SSLNetVConnection.h"

ProxySession::ProxySession() : VConnection(nullptr) {}

ProxySession::ProxySession(NetVConnection *vc) : VConnection(nullptr), _vc(vc) {}

ProxySession::~ProxySession()
{
  if (schedule_event) {
    schedule_event->cancel();
    schedule_event = nullptr;
  }
  this->api_hooks.clear();
  this->mutex.clear();
  this->acl.clear();
  this->_ssl.reset();
}

void
ProxySession::set_session_active()
{
  if (!m_active) {
    m_active = true;
    this->increment_current_active_connections_stat();
  }
}

void
ProxySession::clear_session_active()
{
  if (m_active) {
    m_active = false;
    this->decrement_current_active_connections_stat();
  }
}

static const TSEvent eventmap[TS_HTTP_LAST_HOOK + 1] = {
  TS_EVENT_HTTP_READ_REQUEST_HDR,      // TS_HTTP_READ_REQUEST_HDR_HOOK
  TS_EVENT_HTTP_OS_DNS,                // TS_HTTP_OS_DNS_HOOK
  TS_EVENT_HTTP_SEND_REQUEST_HDR,      // TS_HTTP_SEND_REQUEST_HDR_HOOK
  TS_EVENT_HTTP_READ_CACHE_HDR,        // TS_HTTP_READ_CACHE_HDR_HOOK
  TS_EVENT_HTTP_READ_RESPONSE_HDR,     // TS_HTTP_READ_RESPONSE_HDR_HOOK
  TS_EVENT_HTTP_SEND_RESPONSE_HDR,     // TS_HTTP_SEND_RESPONSE_HDR_HOOK
  TS_EVENT_HTTP_REQUEST_TRANSFORM,     // TS_HTTP_REQUEST_TRANSFORM_HOOK
  TS_EVENT_HTTP_RESPONSE_TRANSFORM,    // TS_HTTP_RESPONSE_TRANSFORM_HOOK
  TS_EVENT_HTTP_SELECT_ALT,            // TS_HTTP_SELECT_ALT_HOOK
  TS_EVENT_HTTP_TXN_START,             // TS_HTTP_TXN_START_HOOK
  TS_EVENT_HTTP_TXN_CLOSE,             // TS_HTTP_TXN_CLOSE_HOOK
  TS_EVENT_HTTP_SSN_START,             // TS_HTTP_SSN_START_HOOK
  TS_EVENT_HTTP_SSN_CLOSE,             // TS_HTTP_SSN_CLOSE_HOOK
  TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE, // TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK
  TS_EVENT_HTTP_PRE_REMAP,             // TS_HTTP_PRE_REMAP_HOOK
  TS_EVENT_HTTP_POST_REMAP,            // TS_HTTP_POST_REMAP_HOOK
  TS_EVENT_NONE,                       // TS_HTTP_RESPONSE_CLIENT_HOOK
  TS_EVENT_NONE,                       // TS_HTTP_LAST_HOOK
};

int
ProxySession::state_api_callout(int event, void *data)
{
  Event *e = static_cast<Event *>(data);
  if (e == schedule_event) {
    schedule_event = nullptr;
  }

  switch (event) {
  case EVENT_NONE:
  case EVENT_INTERVAL:
  case TS_EVENT_HTTP_CONTINUE:
    if (nullptr == cur_hook) {
      /// Get the next hook to invoke from HttpHookState
      cur_hook = hook_state.getNext();
    }
    if (nullptr != cur_hook) {
      APIHook const *hook = cur_hook;

      WEAK_MUTEX_TRY_LOCK(lock, hook->m_cont->mutex, mutex->thread_holding);

      // Have a mutex but didn't get the lock, reschedule
      if (!lock.is_locked()) {
        SET_HANDLER(&ProxySession::state_api_callout);
        if (!schedule_event) { // Don't bother if there is already one
          schedule_event = mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(10));
        }
        return -1;
      }

      cur_hook = nullptr; // mark current callback at dispatched.
      hook->invoke(eventmap[hook_state.id()], this);

      return 0;
    }

    handle_api_return(event);
    break;

  case TS_EVENT_HTTP_ERROR:
    this->handle_api_return(event);
    break;

  // coverity[unterminated_default]
  default:
    ink_release_assert(false);
  }

  return 0;
}

int
ProxySession::do_api_callout(TSHttpHookID id)
{
  ink_assert(id == TS_HTTP_SSN_START_HOOK || id == TS_HTTP_SSN_CLOSE_HOOK);
  hook_state.init(id, http_global_hooks, &api_hooks);
  /// Verify if there is any hook to invoke
  cur_hook = hook_state.getNext();
  if (nullptr != cur_hook) {
    SET_HANDLER(&ProxySession::state_api_callout);
    return this->state_api_callout(EVENT_NONE, nullptr);
  } else {
    this->handle_api_return(TS_EVENT_HTTP_CONTINUE);
  }
  return 0;
}

void
ProxySession::handle_api_return(int event)
{
  TSHttpHookID hookid = hook_state.id();

  SET_HANDLER(&ProxySession::state_api_callout);

  cur_hook = nullptr;

  switch (hookid) {
  case TS_HTTP_SSN_START_HOOK:
    if (event == TS_EVENT_HTTP_ERROR) {
      this->do_io_close();
    } else {
      this->start();
    }
    break;
  case TS_HTTP_SSN_CLOSE_HOOK: {
    free(); // You can now clean things up
    break;
  }
  default:
    Fatal("received invalid session hook %s (%d)", HttpDebugNames::get_api_hook_name(hookid), hookid);
    break;
  }
}

bool
ProxySession::is_chunked_encoding_supported() const
{
  return false;
}

// Override if your session protocol cares.
void
ProxySession::set_half_close_flag(bool flag)
{
}

bool
ProxySession::get_half_close_flag() const
{
  return false;
}

int64_t
ProxySession::connection_id() const
{
  return con_id;
}

bool
ProxySession::attach_server_session(PoolableSession *ssession, bool transaction_done)
{
  return false;
}

PoolableSession *
ProxySession::get_server_session() const
{
  return nullptr;
}

void
ProxySession::set_active_timeout(ink_hrtime timeout_in)
{
  if (_vc) {
    _vc->set_active_timeout(timeout_in);
  }
}

void
ProxySession::set_inactivity_timeout(ink_hrtime timeout_in)
{
  if (_vc) {
    _vc->set_inactivity_timeout(timeout_in);
  }
}

void
ProxySession::cancel_inactivity_timeout()
{
  if (_vc) {
    _vc->cancel_inactivity_timeout();
  }
}

void
ProxySession::cancel_active_timeout()
{
  if (_vc) {
    _vc->cancel_active_timeout();
  }
}

int
ProxySession::populate_protocol(std::string_view *result, int size) const
{
  return _vc ? _vc->populate_protocol(result, size) : 0;
}

const char *
ProxySession::protocol_contains(std::string_view tag_prefix) const
{
  return _vc ? _vc->protocol_contains(tag_prefix) : nullptr;
}

sockaddr const *
ProxySession::get_remote_addr() const
{
  return _vc ? _vc->get_remote_addr() : nullptr;
}

sockaddr const *
ProxySession::get_local_addr()
{
  return _vc ? _vc->get_local_addr() : nullptr;
}

void
ProxySession::_handle_if_ssl(NetVConnection *new_vc)
{
  auto ssl_vc = dynamic_cast<SSLNetVConnection *>(new_vc);
  if (ssl_vc) {
    _ssl = std::make_unique<SSLProxySession>();
    _ssl.get()->init(*ssl_vc);
  }
}

VIO *
ProxySession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return _vc ? this->_vc->do_io_read(c, nbytes, buf) : nullptr;
}

VIO *
ProxySession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return _vc ? this->_vc->do_io_write(c, nbytes, buf, owner) : nullptr;
}

void
ProxySession::do_io_shutdown(ShutdownHowTo_t howto)
{
  this->_vc->do_io_shutdown(howto);
}

void
ProxySession::reenable(VIO *vio)
{
  this->_vc->reenable(vio);
}

bool
ProxySession::support_sni() const
{
  return _vc ? _vc->support_sni() : false;
}
