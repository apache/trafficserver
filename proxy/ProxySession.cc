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

static int64_t next_cs_id = 0;

ProxySession::ProxySession() : VConnection(nullptr)
{
  ink_zero(this->user_args);
}

void
ProxySession::set_session_active()
{
  if (!m_active) {
    m_active = true;
    this->increment_current_active_client_connections_stat();
  }
}

void
ProxySession::clear_session_active()
{
  if (m_active) {
    m_active = false;
    this->decrement_current_active_client_connections_stat();
  }
}

int64_t
ProxySession::next_connection_id()
{
  return ink_atomic_increment(&next_cs_id, 1);
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

void
ProxySession::free()
{
  if (schedule_event) {
    schedule_event->cancel();
    schedule_event = nullptr;
  }
  this->api_hooks.clear();
  this->mutex.clear();
  this->acl.clear();
}

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

      MUTEX_TRY_LOCK(lock, hook->m_cont->mutex, mutex->thread_holding);

      // Have a mutex but didn't get the lock, reschedule
      if (!lock.is_locked()) {
        SET_HANDLER(&ProxySession::state_api_callout);
        if (!schedule_event) { // Don't bother if there is already one
          schedule_event = mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(10));
        }
        return 0;
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

void
ProxySession::do_api_callout(TSHttpHookID id)
{
  ink_assert(id == TS_HTTP_SSN_START_HOOK || id == TS_HTTP_SSN_CLOSE_HOOK);
  hook_state.init(id, http_global_hooks, &api_hooks);
  /// Verify if there is any hook to invoke
  cur_hook = hook_state.getNext();
  if (nullptr != cur_hook) {
    SET_HANDLER(&ProxySession::state_api_callout);
    this->state_api_callout(EVENT_NONE, nullptr);
  } else {
    this->handle_api_return(TS_EVENT_HTTP_CONTINUE);
  }
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

void *

ProxySession::get_user_arg(unsigned ix) const
{
  ink_assert(ix < countof(user_args));
  return this->user_args[ix];
}

void
ProxySession::set_user_arg(unsigned ix, void *arg)
{
  ink_assert(ix < countof(user_args));
  user_args[ix] = arg;
}

void
ProxySession::set_debug(bool flag)
{
  debug_on = flag;
}

// Return whether debugging is enabled for this session.
bool
ProxySession::debug() const
{
  return this->debug_on;
}

bool
ProxySession::is_active() const
{
  return m_active;
}

bool
ProxySession::is_draining() const
{
  return TSSystemState::is_draining();
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

void
ProxySession::attach_server_session(Http1ServerSession *ssession, bool transaction_done)
{
}

Http1ServerSession *
ProxySession::get_server_session() const
{
  return nullptr;
}

TSHttpHookID
ProxySession::get_hookid() const
{
  return hook_state.id();
}

void
ProxySession::set_active_timeout(ink_hrtime timeout_in)
{
}

void
ProxySession::set_inactivity_timeout(ink_hrtime timeout_in)
{
}

void
ProxySession::cancel_inactivity_timeout()
{
}

bool
ProxySession::is_client_closed() const
{
  return get_netvc() == nullptr;
}

int
ProxySession::populate_protocol(std::string_view *result, int size) const
{
  auto vc = this->get_netvc();
  return vc ? vc->populate_protocol(result, size) : 0;
}

const char *
ProxySession::protocol_contains(std::string_view tag_prefix) const
{
  auto vc = this->get_netvc();
  return vc ? vc->protocol_contains(tag_prefix) : nullptr;
}

sockaddr const *
ProxySession::get_client_addr()
{
  NetVConnection *netvc = get_netvc();
  return netvc ? netvc->get_remote_addr() : nullptr;
}
sockaddr const *
ProxySession::get_local_addr()
{
  NetVConnection *netvc = get_netvc();
  return netvc ? netvc->get_local_addr() : nullptr;
}

void
ProxySession::hook_add(TSHttpHookID id, INKContInternal *cont)
{
  this->api_hooks.append(id, cont);
}

APIHook *
ProxySession::hook_get(TSHttpHookID id) const
{
  return this->api_hooks.get(id);
}

HttpAPIHooks const *
ProxySession::feature_hooks() const
{
  return &api_hooks;
}

bool
ProxySession::has_hooks() const
{
  return this->api_hooks.has_hooks() || http_global_hooks->has_hooks();
}
