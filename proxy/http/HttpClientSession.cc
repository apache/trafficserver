/** @file

  A brief file description

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

/****************************************************************************

   HttpClientSession.cc

   Description:


 ****************************************************************************/

#include "ink_config.h"
#include "Allocator.h"
#include "HttpClientSession.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"
#include "HttpServerSession.h"

#define DebugSsn(tag, ...) DebugSpecific(debug_on, tag, __VA_ARGS__)
#define STATE_ENTER(state_name, event, vio) { \
    /*ink_assert (magic == HTTP_SM_MAGIC_ALIVE);  REMEMBER (event, NULL, reentrancy_count); */ \
        DebugSsn("http_cs", "[%" PRId64 "] [%s, %s]", con_id, \
        #state_name, HttpDebugNames::get_event_name(event)); }

enum
{
  HTTP_CS_MAGIC_ALIVE = 0x0123F00D,
  HTTP_CS_MAGIC_DEAD = 0xDEADF00D
};

// We have debugging list that we can use to find stuck
//  client sessions
DLL<HttpClientSession> debug_cs_list;
ink_mutex debug_cs_list_mutex;

static int64_t next_cs_id = (int64_t) 0;
ClassAllocator<HttpClientSession> httpClientSessionAllocator("httpClientSessionAllocator");

HttpClientSession::HttpClientSession()
  : VConnection(NULL), con_id(0), client_vc(NULL), magic(HTTP_CS_MAGIC_DEAD),
    tcp_init_cwnd_set(false),
    transact_count(0), half_close(false), conn_decrease(false), bound_ss(NULL),
    read_buffer(NULL), current_reader(NULL), read_state(HCS_INIT),
    ka_vio(NULL), slave_ka_vio(NULL),
    cur_hook_id(TS_HTTP_LAST_HOOK), cur_hook(NULL),
    cur_hooks(0), proxy_allocated(false), backdoor_connect(false),
    hooks_set(0),
    outbound_port(0), f_outbound_transparent(false),
    host_res_style(HOST_RES_IPV4), acl_method_mask(0),
    m_active(false), debug_on(false)
{
  memset(user_args, 0, sizeof(user_args));
}

void
HttpClientSession::cleanup()
{
  DebugSsn("http_cs", "[%" PRId64 "] session destroy", con_id);

  ink_release_assert(client_vc == NULL);
  ink_release_assert(bound_ss == NULL);
  ink_assert(read_buffer);
  magic = HTTP_CS_MAGIC_DEAD;
  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = NULL;
  }
#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_acquire(&debug_cs_list_mutex);
  debug_cs_list.remove(this, this->debug_link);
  ink_mutex_release(&debug_cs_list_mutex);
#endif

  ink_assert(client_vc == 0);
  api_hooks.clear();
  mutex.clear();
  debug_on = false;

  if (conn_decrease) {
    HTTP_DECREMENT_DYN_STAT(http_current_client_connections_stat);
    conn_decrease = false;
  }
}

void
HttpClientSession::destroy()
{
  this->cleanup();
  if (proxy_allocated)
    THREAD_FREE(this, httpClientSessionAllocator, this_thread());
  else
    httpClientSessionAllocator.free(this);
}

HttpClientSession *
HttpClientSession::allocate()
{
  ink_assert(0);
  return NULL;
}

void
HttpClientSession::ssn_hook_append(TSHttpHookID id, INKContInternal * cont)
{
  api_hooks.append(id, cont);
  hooks_set = 1;

  if (current_reader) {
    current_reader->hooks_set = 1;
  }
}

void
HttpClientSession::ssn_hook_prepend(TSHttpHookID id, INKContInternal * cont)
{
  api_hooks.prepend(id, cont);
  hooks_set = 1;

  if (current_reader) {
    current_reader->hooks_set = 1;
  }
}


void
HttpClientSession::new_transaction()
{
  ink_assert(current_reader == NULL);

  read_state = HCS_ACTIVE_READER;
  current_reader = HttpSM::allocate();
  current_reader->init();
  transact_count++;
  DebugSsn("http_cs", "[%" PRId64 "] Starting transaction %d using sm [%" PRId64 "]", con_id, transact_count, current_reader->sm_id);

  current_reader->attach_client_session(this, sm_reader);
}

inline void
HttpClientSession::do_api_callout(TSHttpHookID id)
{

  cur_hook_id = id;
  ink_assert(cur_hook_id == TS_HTTP_SSN_START_HOOK || cur_hook_id == TS_HTTP_SSN_CLOSE_HOOK);

  if (hooks_set && backdoor_connect == 0) {
    SET_HANDLER(&HttpClientSession::state_api_callout);
    cur_hook = NULL;
    cur_hooks = 0;
    state_api_callout(0, NULL);
  } else {
    handle_api_return(HTTP_API_CONTINUE);
  }
}

void
HttpClientSession::new_connection(NetVConnection * new_vc, bool backdoor)
{

  ink_assert(new_vc != NULL);
  ink_assert(client_vc == NULL);
  client_vc = new_vc;
  magic = HTTP_CS_MAGIC_ALIVE;
  mutex = new_vc->mutex;
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  ink_assert(!!lock);
  this->backdoor_connect = backdoor;

  // Unique client session identifier.
  con_id = ink_atomic_increment((int64_t *) (&next_cs_id), 1);

  HTTP_INCREMENT_DYN_STAT(http_current_client_connections_stat);
  conn_decrease = true;
  HTTP_INCREMENT_DYN_STAT(http_total_client_connections_stat);
  /* inbound requests stat should be incremented here, not after the
   * header has been read */
  HTTP_INCREMENT_DYN_STAT(http_total_incoming_connections_stat);

  // check what type of socket address we just accepted
  // by looking at the address family value of sockaddr_storage
  // and logging to stat system
  switch(new_vc->get_remote_addr()->sa_family) {
    case AF_INET:
      HTTP_INCREMENT_DYN_STAT(http_total_client_connections_ipv4_stat);
    break;
    case AF_INET6:
      HTTP_INCREMENT_DYN_STAT(http_total_client_connections_ipv6_stat);
    break;
    default:
      // don't do anything if the address family is not ipv4 or ipv6
      // (there are many other address families in <sys/socket.h>
      // but we don't have a need to report on all the others today)
    break;
  }

  // Record api hook set state
  hooks_set = http_global_hooks->has_hooks();

#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_acquire(&debug_cs_list_mutex);
  debug_cs_list.push(this, this->debug_link);
  ink_mutex_release(&debug_cs_list_mutex);
#endif

  DebugSsn("http_cs", "[%" PRId64 "] session born, netvc %p", con_id, new_vc);

  read_buffer = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  sm_reader = read_buffer->alloc_reader();

  // INKqa11186: Use a local pointer to the mutex as
  // when we return from do_api_callout, the ClientSession may
  // have already been deallocated.
  EThread *ethis = this_ethread();
  Ptr<ProxyMutex> lmutex = this->mutex;
  MUTEX_TAKE_LOCK(lmutex, ethis);
  do_api_callout(TS_HTTP_SSN_START_HOOK);
  MUTEX_UNTAKE_LOCK(lmutex, ethis);
  lmutex.clear();
}

VIO *
HttpClientSession::do_io_read(Continuation * c, int64_t nbytes, MIOBuffer * buf)
{
  return client_vc->do_io_read(c, nbytes, buf);
}

VIO *
HttpClientSession::do_io_write(Continuation * c, int64_t nbytes, IOBufferReader * buf, bool owner)
{
  /* conditionally set the tcp initial congestion window
     before our first write. */
  DebugSsn("http_cs", "tcp_init_cwnd_set %d\n", (int)tcp_init_cwnd_set);
  if(!tcp_init_cwnd_set) {
    tcp_init_cwnd_set = true;
    set_tcp_init_cwnd();
  }
  return client_vc->do_io_write(c, nbytes, buf, owner);
}

void
HttpClientSession::set_tcp_init_cwnd()
{
  int desired_tcp_init_cwnd = current_reader->t_state.txn_conf->server_tcp_init_cwnd;
  DebugSsn("http_cs", "desired TCP congestion window is %d\n", desired_tcp_init_cwnd);
  if(desired_tcp_init_cwnd == 0) return;
  if(get_netvc()->set_tcp_init_cwnd(desired_tcp_init_cwnd) != 0)
    DebugSsn("http_cs", "set_tcp_init_cwnd(%d) failed", desired_tcp_init_cwnd);
}

void
HttpClientSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  client_vc->do_io_shutdown(howto);
}

void
HttpClientSession::do_io_close(int alerrno)
{

  if (read_state == HCS_ACTIVE_READER) {
    HTTP_DECREMENT_DYN_STAT(http_current_client_transactions_stat);
    if (m_active) {
      m_active = false;
      HTTP_DECREMENT_DYN_STAT(http_current_active_client_connections_stat);
    }
  }

  // Prevent double closing
  ink_release_assert(read_state != HCS_CLOSED);

  // If we have an attached server session, release
  //   it back to our shared pool
  if (bound_ss) {
    bound_ss->release();
    bound_ss = NULL;
    slave_ka_vio = NULL;
  }

  if (half_close) {
    read_state = HCS_HALF_CLOSED;
    SET_HANDLER(&HttpClientSession::state_wait_for_close);
    DebugSsn("http_cs", "[%" PRId64 "] session half close", con_id);

    // We want the client to know that that we're finished
    //  writing.  The write shutdown accomplishes this.  Unfortuantely,
    //  the IO Core symnatics don't stop us from getting events
    //  on the write side of the connection like timeouts so we
    //  need to zero out the write of the continuation with
    //  the do_io_write() call (INKqa05309)
    client_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);

    ka_vio = client_vc->do_io_read(this, INT64_MAX, read_buffer);
    ink_assert(slave_ka_vio != ka_vio);

    // [bug 2610799] Drain any data read.
    // If the buffer is full and the client writes again, we will not receive a
    // READ_READY event.
    sm_reader->consume(sm_reader->read_avail());

    // Set the active timeout to the same as the inactive time so
    //   that this connection does not hang around forever if
    //   the ua hasn't closed
    client_vc->set_active_timeout(HRTIME_SECONDS(current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_out));
  } else {
    read_state = HCS_CLOSED;
    client_vc->do_io_close(alerrno);
    DebugSsn("http_cs", "[%" PRId64 "] session closed", con_id);
    client_vc = NULL;
    HTTP_SUM_DYN_STAT(http_transactions_per_client_con, transact_count);
    HTTP_DECREMENT_DYN_STAT(http_current_client_connections_stat);
    conn_decrease = false;
    do_api_callout(TS_HTTP_SSN_CLOSE_HOOK);
  }
}

int
HttpClientSession::state_wait_for_close(int event, void *data)
{

  STATE_ENTER(&HttpClientSession::state_wait_for_close, event, data);

  ink_assert(data == ka_vio);
  ink_assert(read_state == HCS_HALF_CLOSED);

  switch (event) {
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    half_close = false;
    this->do_io_close();
    break;
  case VC_EVENT_READ_READY:
    // Drain any data read
    sm_reader->consume(sm_reader->read_avail());
    break;
  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

int
HttpClientSession::state_slave_keep_alive(int event, void *data)
{

  STATE_ENTER(&HttpClientSession::state_slave_keep_alive, event, data);

  ink_assert(data == slave_ka_vio);
  ink_assert(bound_ss != NULL);

  switch (event) {
  default:
  case VC_EVENT_READ_COMPLETE:
    // These events are bogus
    ink_assert(0);
    /* Fall Through */
  case VC_EVENT_ERROR:
  case VC_EVENT_READ_READY:
  case VC_EVENT_EOS:
    // The server session closed or something is amiss
    bound_ss->do_io_close();
    bound_ss = NULL;
    slave_ka_vio = NULL;
    break;

  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    // Timeout - place the session on the shared pool
    bound_ss->release();
    bound_ss = NULL;
    slave_ka_vio = NULL;
    break;
  }

  return 0;
}

int
HttpClientSession::state_keep_alive(int event, void *data)
{

  // Route the event.  It is either for client vc or
  //  the origin server slave vc
  if (data && data == slave_ka_vio) {
    return state_slave_keep_alive(event, data);
  } else {
    ink_assert(data && data == ka_vio);
    ink_assert(read_state == HCS_KEEP_ALIVE);
  }

  STATE_ENTER(&HttpClientSession::state_keep_alive, event, data);

  switch (event) {
  case VC_EVENT_READ_READY:
    // New transaction, need to spawn of new sm to process
    // request
    new_transaction();
    break;

  case VC_EVENT_EOS:
    // If there is data in the buffer, start a new
    //  transaction, otherwise the client gave up
    if (sm_reader->read_avail() > 0) {
      new_transaction();
    } else {
      this->do_io_close();
    }
    break;

  case VC_EVENT_READ_COMPLETE:
  default:
    // These events are bogus
    ink_assert(0);
    // Fall through
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    // Keep-alive timed out
    this->do_io_close();
    break;
  }

  return 0;
}

int
HttpClientSession::state_api_callout(int event, void * /* data ATS_UNUSED */)
{
  switch (event) {
  case EVENT_NONE:
  case EVENT_INTERVAL:
  case HTTP_API_CONTINUE:
    if ((cur_hook_id >= 0) && (cur_hook_id < TS_HTTP_LAST_HOOK)) {
      if (!cur_hook) {
        if (cur_hooks == 0) {
          cur_hook = http_global_hooks->get(cur_hook_id);
          cur_hooks++;
        }
      }
      if (!cur_hook) {
        if (cur_hooks == 1) {
          cur_hook = api_hooks.get(cur_hook_id);
          cur_hooks++;
        }
      }

      if (cur_hook) {
        bool plugin_lock;
        Ptr<ProxyMutex> plugin_mutex;
        if (cur_hook->m_cont->mutex) {
          plugin_mutex = cur_hook->m_cont->mutex;
          plugin_lock = MUTEX_TAKE_TRY_LOCK(cur_hook->m_cont->mutex, mutex->thread_holding);
          if (!plugin_lock) {
            SET_HANDLER(&HttpClientSession::state_api_callout);
            mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(10), ET_NET);
            return 0;
          }
        } else {
          plugin_lock = false;
        }

        APIHook *hook = cur_hook;
        cur_hook = cur_hook->next();

        hook->invoke(TS_EVENT_HTTP_READ_REQUEST_HDR + cur_hook_id, this);

        if (plugin_lock) {
          // BZ 51246
          Mutex_unlock(plugin_mutex, this_ethread());
        }

        return 0;
      }
    }

    handle_api_return(event);
    break;

  default:
    ink_assert(false);
  case HTTP_API_ERROR:
    handle_api_return(event);
    break;
  }

  return 0;
}

void
HttpClientSession::handle_api_return(int event)
{
  SET_HANDLER(&HttpClientSession::state_api_callout);

  cur_hook = NULL;
  cur_hooks = 0;

  switch (cur_hook_id) {
  case TS_HTTP_SSN_START_HOOK:
    if (event != HTTP_API_ERROR) {
      new_transaction();
    } else {
      do_io_close();
    }
    break;
  case TS_HTTP_SSN_CLOSE_HOOK:
    destroy();
    break;
  default:
    ink_release_assert(0);
    break;
  }
}

void
HttpClientSession::reenable(VIO * vio)
{
  client_vc->reenable(vio);
}

void
HttpClientSession::attach_server_session(HttpServerSession * ssession, bool transaction_done)
{
  if (ssession) {
    ink_assert(bound_ss == NULL);
    ssession->state = HSS_KA_CLIENT_SLAVE;
    bound_ss = ssession;
    DebugSsn("http_cs", "[%" PRId64 "] attaching server session [%" PRId64 "] as slave", con_id, ssession->con_id);
    ink_assert(ssession->get_reader()->read_avail() == 0);
    ink_assert(ssession->get_netvc() != client_vc);

    // handling potential keep-alive here
    if (m_active) {
      m_active = false;
      HTTP_DECREMENT_DYN_STAT(http_current_active_client_connections_stat);
    }
    // Since this our slave, issue an IO to detect a close and
    //  have it call the client session back.  This IO also prevent
    //  the server net conneciton from calling back a dead sm
    SET_HANDLER(&HttpClientSession::state_keep_alive);
    slave_ka_vio = ssession->do_io_read(this, INT64_MAX, ssession->read_buffer);
    ink_assert(slave_ka_vio != ka_vio);

    // Transfer control of the write side as well
    ssession->do_io_write(this, 0, NULL);

    if (transaction_done) {
      ssession->get_netvc()->
        set_inactivity_timeout(HRTIME_SECONDS(current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_out));
      ssession->get_netvc()->cancel_active_timeout();
    } else {
      // we are serving from the cache - this could take a while.
      ssession->get_netvc()->cancel_inactivity_timeout();
      ssession->get_netvc()->cancel_active_timeout();
    }
  } else {
    ink_assert(bound_ss != NULL);
    bound_ss = NULL;
    slave_ka_vio = NULL;
  }
}

void
HttpClientSession::release(IOBufferReader * r)
{
  ink_assert(read_state == HCS_ACTIVE_READER);
  ink_assert(current_reader != NULL);
  MgmtInt ka_in = current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_in;

  DebugSsn("http_cs", "[%" PRId64 "] session released by sm [%" PRId64 "]", con_id, current_reader->sm_id);
  current_reader = NULL;

  // handling potential keep-alive here
  if (m_active) {
    m_active = false;
    HTTP_DECREMENT_DYN_STAT(http_current_active_client_connections_stat);
  }
  // Make sure that the state machine is returning
  //  correct buffer reader
  ink_assert(r == sm_reader);
  if (r != sm_reader) {
    this->do_io_close();
    return;
  }

  HTTP_DECREMENT_DYN_STAT(http_current_client_transactions_stat);

  // Check to see there is remaining data in the
  //  buffer.  If there is, spin up a new state
  //  machine to process it.  Otherwise, issue an
  //  IO to wait for new data
  if (sm_reader->read_avail() > 0) {
    DebugSsn("http_cs", "[%" PRId64 "] data already in buffer, starting new transaction", con_id);
    new_transaction();
  } else {
    DebugSsn("http_cs", "[%" PRId64 "] initiating io for next header", con_id);
    read_state = HCS_KEEP_ALIVE;
    SET_HANDLER(&HttpClientSession::state_keep_alive);
    ka_vio = this->do_io_read(this, INT64_MAX, read_buffer);
    ink_assert(slave_ka_vio != ka_vio);
    client_vc->set_inactivity_timeout(HRTIME_SECONDS(ka_in));
    client_vc->cancel_active_timeout();
  }
}

HttpServerSession *
HttpClientSession::get_bound_ss()
{
  return bound_ss;
}
