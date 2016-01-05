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

#include "ts/ink_config.h"
#include "ts/Allocator.h"
#include "HttpClientSession.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"
#include "HttpServerSession.h"
#include "Plugin.h"
#include "Http2ClientSession.h"

#define DebugHttpSsn(fmt, ...) DebugSsn(this, "http_cs", fmt, __VA_ARGS__)

#define STATE_ENTER(state_name, event, vio)                                                             \
  do {                                                                                                  \
    /*ink_assert (magic == HTTP_SM_MAGIC_ALIVE);  REMEMBER (event, NULL, reentrancy_count); */          \
    DebugHttpSsn("[%" PRId64 "] [%s, %s]", con_id, #state_name, HttpDebugNames::get_event_name(event)); \
  } while (0)

enum {
  HTTP_CS_MAGIC_ALIVE = 0x0123F00D,
  HTTP_CS_MAGIC_DEAD = 0xDEADF00D,
};

// We have debugging list that we can use to find stuck
//  client sessions
DLL<HttpClientSession> debug_cs_list;
ink_mutex debug_cs_list_mutex;

ClassAllocator<HttpClientSession> httpClientSessionAllocator("httpClientSessionAllocator");

HttpClientSession::HttpClientSession()
  : con_id(0), client_vc(NULL), magic(HTTP_CS_MAGIC_DEAD), transact_count(0), tcp_init_cwnd_set(false), half_close(false),
    conn_decrease(false), upgrade_to_h2c(false), bound_ss(NULL), read_buffer(NULL), current_reader(NULL), read_state(HCS_INIT),
    ka_vio(NULL), slave_ka_vio(NULL), outbound_port(0), f_outbound_transparent(false), host_res_style(HOST_RES_IPV4),
    acl_record(NULL), m_active(false)
{
}

void
HttpClientSession::destroy()
{
  DebugHttpSsn("[%" PRId64 "] session destroy", con_id);

  ink_release_assert(upgrade_to_h2c || !client_vc);
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

  if (conn_decrease) {
    HTTP_DECREMENT_DYN_STAT(http_current_client_connections_stat);
    conn_decrease = false;
  }

  super::destroy();
  THREAD_FREE(this, httpClientSessionAllocator, this_thread());
}

void
HttpClientSession::ssn_hook_append(TSHttpHookID id, INKContInternal *cont)
{
  ProxyClientSession::ssn_hook_append(id, cont);
  if (current_reader) {
    current_reader->hooks_set = 1;
  }
}

void
HttpClientSession::ssn_hook_prepend(TSHttpHookID id, INKContInternal *cont)
{
  ProxyClientSession::ssn_hook_prepend(id, cont);
  if (current_reader) {
    current_reader->hooks_set = 1;
  }
}

void
HttpClientSession::new_transaction()
{
  ink_assert(current_reader == NULL);
  PluginIdentity *pi = dynamic_cast<PluginIdentity *>(client_vc);

  if (!pi && client_vc->add_to_active_queue() == false) {
    // no room in the active queue close the connection
    this->do_io_close();
    return;
  }


  // Defensive programming, make sure nothing persists across
  // connection re-use
  half_close = false;

  read_state = HCS_ACTIVE_READER;
  current_reader = HttpSM::allocate();
  current_reader->init();
  transact_count++;
  DebugHttpSsn("[%" PRId64 "] Starting transaction %d using sm [%" PRId64 "]", con_id, transact_count, current_reader->sm_id);

  current_reader->attach_client_session(this, sm_reader);
  if (pi) {
    // it's a plugin VC of some sort with identify information.
    // copy it to the SM.
    current_reader->plugin_tag = pi->getPluginTag();
    current_reader->plugin_id = pi->getPluginId();
  }
}

void
HttpClientSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor)
{
  ink_assert(new_vc != NULL);
  ink_assert(client_vc == NULL);
  client_vc = new_vc;
  magic = HTTP_CS_MAGIC_ALIVE;
  mutex = new_vc->mutex;
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  ink_assert(lock.is_locked());

  // Disable hooks for backdoor connections.
  this->hooks_on = !backdoor;

  // Unique client session identifier.
  con_id = ProxyClientSession::next_connection_id();

  HTTP_INCREMENT_DYN_STAT(http_current_client_connections_stat);
  conn_decrease = true;
  HTTP_INCREMENT_DYN_STAT(http_total_client_connections_stat);
  if (static_cast<HttpProxyPort::TransportType>(new_vc->attributes) == HttpProxyPort::TRANSPORT_SSL) {
    HTTP_INCREMENT_DYN_STAT(https_total_client_connections_stat);
  }

  /* inbound requests stat should be incremented here, not after the
   * header has been read */
  HTTP_INCREMENT_DYN_STAT(http_total_incoming_connections_stat);

  // check what type of socket address we just accepted
  // by looking at the address family value of sockaddr_storage
  // and logging to stat system
  switch (new_vc->get_remote_addr()->sa_family) {
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

#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_acquire(&debug_cs_list_mutex);
  debug_cs_list.push(this, this->debug_link);
  ink_mutex_release(&debug_cs_list_mutex);
#endif

  DebugHttpSsn("[%" PRId64 "] session born, netvc %p", con_id, new_vc);

  RecString congestion_control_in;
  if (REC_ReadConfigStringAlloc(congestion_control_in, "proxy.config.net.tcp_congestion_control_in") == REC_ERR_OKAY) {
    int len = strlen(congestion_control_in);
    if (len > 0) {
      client_vc->set_tcp_congestion_control(congestion_control_in, len);
    }
    ats_free(congestion_control_in);
  }
  if (!iobuf) {
    SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(new_vc);
    if (ssl_vc) {
      iobuf = ssl_vc->get_ssl_iobuf();
      sm_reader = ssl_vc->get_ssl_reader();
    }
  }

  read_buffer = iobuf ? iobuf : new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  sm_reader = reader ? reader : read_buffer->alloc_reader();

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
HttpClientSession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return client_vc->do_io_read(c, nbytes, buf);
}

VIO *
HttpClientSession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  /* conditionally set the tcp initial congestion window
     before our first write. */
  DebugHttpSsn("tcp_init_cwnd_set %d\n", (int)tcp_init_cwnd_set);
  // Checking c to avoid clang detected NULL derference path
  if (c && !tcp_init_cwnd_set) {
    tcp_init_cwnd_set = true;
    set_tcp_init_cwnd();
  }
  return client_vc->do_io_write(c, nbytes, buf, owner);
}

void
HttpClientSession::set_tcp_init_cwnd()
{
  int desired_tcp_init_cwnd = current_reader->t_state.txn_conf->server_tcp_init_cwnd;
  DebugHttpSsn("desired TCP congestion window is %d\n", desired_tcp_init_cwnd);
  if (desired_tcp_init_cwnd == 0)
    return;
  if (get_netvc()->set_tcp_init_cwnd(desired_tcp_init_cwnd) != 0)
    DebugHttpSsn("set_tcp_init_cwnd(%d) failed", desired_tcp_init_cwnd);
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

  if (half_close && this->current_reader) {
    read_state = HCS_HALF_CLOSED;
    SET_HANDLER(&HttpClientSession::state_wait_for_close);
    DebugHttpSsn("[%" PRId64 "] session half close", con_id);

    // We want the client to know that that we're finished
    //  writing.  The write shutdown accomplishes this.  Unfortuantely,
    //  the IO Core semantics don't stop us from getting events
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
    // clean up ssl's first byte iobuf
    SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(client_vc);
    if (ssl_vc) {
      ssl_vc->set_ssl_iobuf(NULL);
    }
    if (upgrade_to_h2c && this->current_reader) {
      Http2ClientSession *h2_session = http2ClientSessionAllocator.alloc();

      h2_session->set_upgrade_context(&current_reader->t_state.hdr_info.client_request);
      h2_session->new_connection(client_vc, NULL, NULL, false /* backdoor */);
      // Handed over control of the VC to the new H2 session, don't clean it up
      this->release_netvc();
      // TODO Consider about handling HTTP/1 hooks and stats
    } else {
      DebugHttpSsn("[%" PRId64 "] session closed", con_id);
    }
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
void
HttpClientSession::reenable(VIO *vio)
{
  client_vc->reenable(vio);
}

void
HttpClientSession::attach_server_session(HttpServerSession *ssession, bool transaction_done)
{
  if (ssession) {
    ink_assert(bound_ss == NULL);
    ssession->state = HSS_KA_CLIENT_SLAVE;
    bound_ss = ssession;
    DebugHttpSsn("[%" PRId64 "] attaching server session [%" PRId64 "] as slave", con_id, ssession->con_id);
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
      ssession->get_netvc()->set_inactivity_timeout(
        HRTIME_SECONDS(current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_out));
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
HttpClientSession::release(IOBufferReader *r)
{
  ink_assert(read_state == HCS_ACTIVE_READER);
  ink_assert(current_reader != NULL);
  MgmtInt ka_in = current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_in;

  DebugHttpSsn("[%" PRId64 "] session released by sm [%" PRId64 "]", con_id, current_reader->sm_id);
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

  // Clean up the write VIO in case of inactivity timeout
  this->do_io_write(NULL, 0, NULL);

  // Check to see there is remaining data in the
  //  buffer.  If there is, spin up a new state
  //  machine to process it.  Otherwise, issue an
  //  IO to wait for new data
  if (sm_reader->read_avail() > 0) {
    DebugHttpSsn("[%" PRId64 "] data already in buffer, starting new transaction", con_id);
    new_transaction();
  } else {
    DebugHttpSsn("[%" PRId64 "] initiating io for next header", con_id);
    read_state = HCS_KEEP_ALIVE;
    SET_HANDLER(&HttpClientSession::state_keep_alive);
    ka_vio = this->do_io_read(this, INT64_MAX, read_buffer);
    ink_assert(slave_ka_vio != ka_vio);
    client_vc->set_inactivity_timeout(HRTIME_SECONDS(ka_in));
    client_vc->cancel_active_timeout();
    client_vc->add_to_keep_alive_queue();
  }
}

HttpServerSession *
HttpClientSession::get_bound_ss()
{
  return bound_ss;
}
