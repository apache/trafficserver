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

   Http1ClientSession.cc

   Description:


 ****************************************************************************/

#include "tscore/ink_resolver.h"
#include "Http1ClientSession.h"
#include "Http1Transaction.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"
#include "Plugin.h"
#include "PoolableSession.h"

#define HttpSsnDebug(fmt, ...) SsnDebug(this, "http_cs", fmt, __VA_ARGS__)

#define STATE_ENTER(state_name, event, vio)                                                             \
  do {                                                                                                  \
    /*ink_assert (magic == HTTP_SM_MAGIC_ALIVE);  REMEMBER (event, NULL, reentrancy_count); */          \
    HttpSsnDebug("[%" PRId64 "] [%s, %s]", con_id, #state_name, HttpDebugNames::get_event_name(event)); \
  } while (0)

#ifdef USE_HTTP_DEBUG_LISTS

// We have debugging list that we can use to find stuck
//  client sessions
DList(Http1ClientSession, debug_link) debug_cs_list;
ink_mutex debug_cs_list_mutex;

#endif /* USE_HTTP_DEBUG_LISTS */

ClassAllocator<Http1ClientSession, true> http1ClientSessionAllocator("http1ClientSessionAllocator");

Http1ClientSession::Http1ClientSession() : super(), trans(this) {}

void
Http1ClientSession::destroy()
{
  if (read_state != HCS_CLOSED) {
    return;
  }
  if (!in_destroy) {
    in_destroy = true;

    HttpSsnDebug("[%" PRId64 "] session destroy", con_id);
    ink_assert(read_buffer);
    ink_release_assert(transact_count == released_transactions);
    do_api_callout(TS_HTTP_SSN_CLOSE_HOOK);
  } else {
    Warning("http1: Attempt to double ssn close");
  }
}

void
Http1ClientSession::release_transaction()
{
  released_transactions++;
  if (transact_count == released_transactions) {
    // Make sure we previously called release() or do_io_close() on the session
    ink_release_assert(read_state != HCS_INIT);
    if (read_state == HCS_ACTIVE_READER) {
      // (in)active timeout
      do_io_close(HTTP_ERRNO);
    } else {
      destroy();
    }
  }
}

void
Http1ClientSession::free()
{
  magic = HTTP_CS_MAGIC_DEAD;
  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = nullptr;
  }

#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_acquire(&debug_cs_list_mutex);
  debug_cs_list.remove(this);
  ink_mutex_release(&debug_cs_list_mutex);
#endif

  if (conn_decrease) {
    HTTP_DECREMENT_DYN_STAT(http_current_client_connections_stat);
    conn_decrease = false;
  }

  if (_vc) {
    _vc->do_io_close();
    _vc = nullptr;
  }

  THREAD_FREE(this, http1ClientSessionAllocator, this_thread());
}

void
Http1ClientSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  ink_assert(new_vc != nullptr);
  ink_assert(_vc == nullptr);
  _vc         = new_vc;
  magic       = HTTP_CS_MAGIC_ALIVE;
  mutex       = new_vc->mutex;
  trans.mutex = mutex; // Share this mutex with the transaction
  in_destroy  = false;

  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(new_vc);
  if (ssl_vc != nullptr) {
    read_from_early_data = ssl_vc->read_from_early_data;
    Debug("ssl_early_data", "read_from_early_data = %" PRId64, read_from_early_data);
  }

  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  ink_assert(lock.is_locked());

  // Unique client session identifier.
  con_id = ProxySession::next_connection_id();

  schedule_event = nullptr;

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
  debug_cs_list.push(this);
  ink_mutex_release(&debug_cs_list_mutex);
#endif

  HttpSsnDebug("[%" PRId64 "] session born, netvc %p", con_id, new_vc);

  _vc->set_tcp_congestion_control(CLIENT_SIDE);

  read_buffer = iobuf ? iobuf : new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  _reader     = reader ? reader : read_buffer->alloc_reader();

  trans.set_reader(_reader);
  trans.upstream_outbound_options = *accept_options;

  _handle_if_ssl(new_vc);

  // INKqa11186: Use a local pointer to the mutex as
  // when we return from do_api_callout, the ClientSession may
  // have already been deallocated.
  EThread *ethis         = this_ethread();
  Ptr<ProxyMutex> lmutex = this->mutex;
  MUTEX_TAKE_LOCK(lmutex, ethis);
  do_api_callout(TS_HTTP_SSN_START_HOOK);
  MUTEX_UNTAKE_LOCK(lmutex, ethis);
  lmutex.clear();
}

void
Http1ClientSession::do_io_close(int alerrno)
{
  if (read_state == HCS_CLOSED) {
    return; // Don't double call session close
  }
  if (read_state == HCS_ACTIVE_READER) {
    clear_session_active();
  }

  // Prevent double closing
  ink_release_assert(read_state != HCS_CLOSED);

  // If we have an attached server session, release
  //   it back to our shared pool
  if (bound_ss) {
    bound_ss->release(nullptr);
    bound_ss     = nullptr;
    slave_ka_vio = nullptr;
  }
  // Completed the last transaction.  Just shutdown already
  // Or the do_io_close is due to a network error
  if (transact_count == released_transactions || alerrno == HTTP_ERRNO) {
    half_close = false;
  }

  if (half_close && this->trans.get_sm()) {
    read_state = HCS_HALF_CLOSED;
    SET_HANDLER(&Http1ClientSession::state_wait_for_close);
    HttpSsnDebug("[%" PRId64 "] session half close", con_id);

    if (_vc) {
      _vc->do_io_shutdown(IO_SHUTDOWN_WRITE);

      ka_vio = _vc->do_io_read(this, INT64_MAX, read_buffer);
      ink_assert(slave_ka_vio != ka_vio);

      // Set the active timeout to the same as the inactive time so
      //   that this connection does not hang around forever if
      //   the ua hasn't closed
      _vc->set_active_timeout(HRTIME_SECONDS(trans.get_sm()->t_state.txn_conf->keep_alive_no_activity_timeout_in));
    }

    // [bug 2610799] Drain any data read.
    // If the buffer is full and the client writes again, we will not receive a
    // READ_READY event.
    _reader->consume(_reader->read_avail());
  } else {
    HttpSsnDebug("[%" PRId64 "] session closed", con_id);
    HTTP_SUM_DYN_STAT(http_transactions_per_client_con, transact_count);
    read_state = HCS_CLOSED;

    // Can go ahead and close the netvc now, but keeping around the session object
    // until all the transactions are closed
    if (_vc) {
      _vc->do_io_close();
      _vc = nullptr;
    }
  }
  if (transact_count == released_transactions) {
    this->destroy();
  }
}

int
Http1ClientSession::state_wait_for_close(int event, void *data)
{
  STATE_ENTER(&Http1ClientSession::state_wait_for_close, event, data);

  ink_assert(data == ka_vio);
  ink_assert(read_state == HCS_HALF_CLOSED);

  Event *e = static_cast<Event *>(data);
  if (e == schedule_event) {
    schedule_event = nullptr;
  }

  switch (event) {
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    half_close = false;
    this->do_io_close(EHTTP_ERROR);
    break;
  case VC_EVENT_READ_READY:
    // Drain any data read
    _reader->consume(_reader->read_avail());
    break;

  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

int
Http1ClientSession::state_slave_keep_alive(int event, void *data)
{
  STATE_ENTER(&Http1ClientSession::state_slave_keep_alive, event, data);

  ink_assert(data == slave_ka_vio);

  Event *e = static_cast<Event *>(data);
  if (e == schedule_event) {
    schedule_event = nullptr;
  }

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
    bound_ss     = nullptr;
    slave_ka_vio = nullptr;
    break;

  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    // Timeout - place the session on the shared pool
    bound_ss->release(nullptr);
    bound_ss     = nullptr;
    slave_ka_vio = nullptr;
    break;
  }

  return 0;
}

int
Http1ClientSession::state_keep_alive(int event, void *data)
{
  // Route the event.  It is either for vc or
  //  the origin server slave vc
  if (data && data == slave_ka_vio) {
    return state_slave_keep_alive(event, data);
  } else {
    ink_assert(data && data == ka_vio);
    ink_assert(read_state == HCS_KEEP_ALIVE);
  }

  STATE_ENTER(&Http1ClientSession::state_keep_alive, event, data);

  switch (event) {
  case VC_EVENT_READ_READY:
    // New transaction, need to spawn of new sm to process
    // request
    new_transaction();
    break;

  case VC_EVENT_EOS:
    this->do_io_close(EHTTP_ERROR);
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
    this->do_io_close(EHTTP_ERROR);
    break;
  }

  return 0;
}

// Called from the Http1Transaction::release
void
Http1ClientSession::release(ProxyTransaction *trans)
{
  ink_assert(read_state == HCS_ACTIVE_READER || read_state == HCS_INIT);

  // When release is called from start() to read the first transaction, get_sm()
  // will return null.
  HttpSM *sm                = trans->get_sm();
  Http1Transaction *h1trans = static_cast<Http1Transaction *>(trans);
  if (sm) {
    MgmtInt ka_in = trans->get_sm()->t_state.txn_conf->keep_alive_no_activity_timeout_in;
    set_inactivity_timeout(HRTIME_SECONDS(ka_in));

    this->clear_session_active();

    // Timeout events should be delivered to the session
    this->do_io_write(this, 0, nullptr);
  }

  h1trans->reset();

  // Check to see there is remaining data in the
  //  buffer.  If there is, spin up a new state
  //  machine to process it.  Otherwise, issue an
  //  IO to wait for new data
  bool more_to_read = this->_reader->is_read_avail_more_than(0);
  if (more_to_read) {
    HttpSsnDebug("[%" PRId64 "] data already in buffer, starting new transaction", con_id);
    new_transaction();
  } else {
    HttpSsnDebug("[%" PRId64 "] initiating io for next header", con_id);
    read_state = HCS_KEEP_ALIVE;
    SET_HANDLER(&Http1ClientSession::state_keep_alive);
    ka_vio = this->do_io_read(this, INT64_MAX, read_buffer);
    ink_assert(slave_ka_vio != ka_vio);

    if (_vc) {
      // Under heavy traffic ( - e.g. hitting proxy.config.net.max_connections_in limit), calling add_to_keep_alive_queue()
      // could free this _vc, session, and transaction.
      _vc->cancel_active_timeout();
      _vc->add_to_keep_alive_queue();
    }
  }
}

void
Http1ClientSession::new_transaction()
{
  // If the client connection terminated during API callouts we're done.
  if (nullptr == _vc) {
    this->do_io_close(); // calls the SSN_CLOSE hooks to match the SSN_START hooks.
    return;
  }

  if (!_vc->add_to_active_queue()) {
    // no room in the active queue close the connection
    this->do_io_close();
    return;
  }

  // Defensive programming, make sure nothing persists across
  // connection re-use
  half_close = false;

  read_state = HCS_ACTIVE_READER;

  transact_count++;

  trans.new_transaction(read_from_early_data > 0 ? true : false);
}

bool
Http1ClientSession::attach_server_session(PoolableSession *ssession, bool transaction_done)
{
  if (ssession) {
    ink_assert(bound_ss == nullptr);
    ssession->state = PoolableSession::KA_RESERVED;
    bound_ss        = ssession;
    HttpSsnDebug("[%" PRId64 "] attaching server session [%" PRId64 "] as slave", con_id, ssession->connection_id());
    ink_assert(ssession->get_netvc() != this->get_netvc());

    // handling potential keep-alive here
    clear_session_active();

    // Since this our slave, issue an IO to detect a close and
    //  have it call the client session back.  This IO also prevent
    //  the server net connection from calling back a dead sm
    SET_HANDLER(&Http1ClientSession::state_keep_alive);
    slave_ka_vio = ssession->do_io_read(this, INT64_MAX, ssession->get_reader()->mbuf);
    ink_assert(slave_ka_vio != ka_vio);

    // Transfer control of the write side as well
    ssession->do_io_write(this, 0, nullptr);

    if (transaction_done) {
      ssession->set_inactivity_timeout(HRTIME_SECONDS(trans.get_sm()->t_state.txn_conf->keep_alive_no_activity_timeout_out));
      ssession->cancel_active_timeout();
    } else {
      // we are serving from the cache - this could take a while.
      ssession->cancel_inactivity_timeout();
      ssession->cancel_active_timeout();
    }
  } else {
    ink_assert(bound_ss != nullptr);
    bound_ss     = nullptr;
    slave_ka_vio = nullptr;
  }
  return true;
}

void
Http1ClientSession::increment_current_active_connections_stat()
{
  HTTP_INCREMENT_DYN_STAT(http_current_active_client_connections_stat);
}
void
Http1ClientSession::decrement_current_active_connections_stat()
{
  HTTP_DECREMENT_DYN_STAT(http_current_active_client_connections_stat);
}

void
Http1ClientSession::start()
{
  // Troll for data to get a new transaction
  this->release(&trans);
}

bool
Http1ClientSession::allow_half_open() const
{
  // Only allow half open connections if the not over TLS
  return (_vc && dynamic_cast<SSLNetVConnection *>(_vc) == nullptr);
}

void
Http1ClientSession::set_half_close_flag(bool flag)
{
  half_close = flag;
}

bool
Http1ClientSession::get_half_close_flag() const
{
  return half_close;
}

bool
Http1ClientSession::is_chunked_encoding_supported() const
{
  return true;
}

int
Http1ClientSession::get_transact_count() const
{
  return transact_count;
}

bool
Http1ClientSession::is_outbound_transparent() const
{
  return f_outbound_transparent;
}

PoolableSession *
Http1ClientSession::get_server_session() const
{
  return bound_ss;
}

const char *
Http1ClientSession::get_protocol_string() const
{
  return "http";
}
