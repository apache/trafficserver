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

   Http1ServerSession.cc

   Description:

 ****************************************************************************/
#include "iocore/net/NetVConnection.h"
#include "tscore/ink_config.h"
#include "tsutil/ts_bw_format.h"
#include "tscore/Allocator.h"
#include "proxy/http/Http1ServerSession.h"
#include "proxy/http/HttpSessionManager.h"
#include "proxy/http/HttpSM.h"

ClassAllocator<Http1ServerSession, true> httpServerSessionAllocator("httpServerSessionAllocator");

namespace
{
DbgCtl dbg_ctl_http_ss{"http_ss"};

} // end anonymous namespace

Http1ServerSession::Http1ServerSession() : super_type(), trans(this) {}

void
Http1ServerSession::destroy()
{
  if (state != PooledState::SSN_CLOSED) {
    return;
  }
  ink_release_assert(_vc == nullptr);
  ink_assert(read_buffer);
  magic = Http1ServerSessionMagic::DEAD;
  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = nullptr;
  }

  mutex.clear();
  if (httpSessionManager.get_pool_type() == TS_SERVER_SESSION_SHARING_POOL_THREAD) {
    THREAD_FREE(this, httpServerSessionAllocator, this_thread());
  } else {
    httpServerSessionAllocator.free(this);
  }
}

void
Http1ServerSession::free()
{
  // Unlike Http1ClientSession, Http1ServerSession is freed in destroy()
}

void
Http1ServerSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  ink_assert(new_vc != nullptr);
  _vc = new_vc;

  // Used to do e.g. mutex = new_vc->thread->mutex; when per-thread pools enabled
  mutex = new_vc->mutex;

  // Unique session identifier.
  con_id = ProxySession::next_connection_id();

  magic = Http1ServerSessionMagic::ALIVE;
  Metrics::Gauge::increment(http_rsb.current_server_connections);
  Metrics::Counter::increment(http_rsb.total_server_connections);

  if (iobuf == nullptr) {
    read_buffer = new_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
    _reader     = read_buffer->alloc_reader();
  } else {
    read_buffer = iobuf;
    _reader     = reader;
  }
  Dbg(dbg_ctl_http_ss, "[%" PRId64 "] session born, netvc %p", con_id, new_vc);
  state = PooledState::INIT;

  new_vc->set_tcp_congestion_control(NetVConnection::tcp_congestion_control_side::SERVER_SIDE);
}

void
Http1ServerSession::do_io_close(int alerrno)
{
  // Only do the close bookkeeping 1 time
  if (state != PooledState::SSN_CLOSED) {
    swoc::LocalBufferWriter<256> w;
    bool                         debug_p = dbg_ctl_http_ss.on();

    state = PooledState::SSN_CLOSED;

    if (debug_p) {
      w.print("[{}] session close: nevtc {:x}", con_id, _vc);
    }

    Metrics::Gauge::decrement(http_rsb.current_server_connections);

    // Update upstream connection tracking data if present.
    this->release_outbound_connection_tracking();

    if (debug_p) {
      Dbg(dbg_ctl_http_ss, "%.*s", static_cast<int>(w.size()), w.data());
    }

    if (_vc) {
      _vc->do_io_close(alerrno);
    }
    _vc = nullptr;

    if (to_parent_proxy) {
      Metrics::Gauge::decrement(http_rsb.current_parent_proxy_connections);
    }
  }

  if (transact_count == released_transactions) {
    this->destroy();
  }
}

// void Http1ServerSession::release()
//
//   Releases the session for K-A reuse
//
void
Http1ServerSession::release(ProxyTransaction * /* trans ATS_UNUSED */)
{
  Dbg(dbg_ctl_http_ss, "[%" PRId64 "] Releasing session, private_session=%d, sharing_match=%d", con_id, this->is_private(),
      sharing_match);
  if (state == PooledState::SSN_IN_USE) {
    // The caller should have already set the inactive timeout to the keep alive timeout
    // Unfortunately, we do not have access to that value from here.
    // However we can clear the active timeout here.  The active timeout makes no sense
    // in the keep alive state
    cancel_active_timeout();
    state = PooledState::SSN_TO_RELEASE;
    return;
  }
  ink_release_assert(state == PooledState::SSN_TO_RELEASE);
}

// Keys for matching hostnames
IpEndpoint const &
Http1ServerSession::get_server_ip() const
{
  ink_release_assert(_vc != nullptr);
  return _vc->get_remote_endpoint();
}

int
Http1ServerSession::get_transact_count() const
{
  return transact_count;
}

const char *
Http1ServerSession::get_protocol_string() const
{
  return "http";
}
void
Http1ServerSession::increment_current_active_connections_stat()
{
  // TODO: Implement stats
}
void
Http1ServerSession::decrement_current_active_connections_stat()
{
  // TODO: Implement stats
}

void
Http1ServerSession::start()
{
}

bool
Http1ServerSession::is_chunked_encoding_supported() const
{
  return true;
}

void
Http1ServerSession ::release_transaction()
{
  // Must adjust the release count before attempting to hand the session
  // back to the session manager to avoid race conditions in the global
  // pool case
  released_transactions++;

  // Private sessions are never released back to the shared pool
  if (this->is_private() || sharing_match == 0) {
    if (this->is_private()) {
      Metrics::Counter::increment(http_rsb.origin_close_private);
    }
    this->do_io_close();
  } else if (state == PooledState::SSN_TO_RELEASE) {
    _vc->control_flags.set_flags(0);

    // do not change the read/write cont and mutex yet
    // release_session() will either swap them with the
    // pool continuation with a valid read buffer or if
    // it fails, do_io_close() will clear the cont anyway

    HSMresult_t r = httpSessionManager.release_session(this);

    if (r == HSMresult_t::RETRY) {
      // Session could not be put in the session manager
      //  due to lock contention
      // FIX:  should retry instead of closing
      do_io_close(HTTP_ERRNO);
      Metrics::Counter::increment(http_rsb.origin_shutdown_pool_lock_contention);
    } else {
      // The session was successfully put into the session
      //    manager and it will manage it
      // (Note: should never get HSMresult_t::NOT_FOUND here)
      ink_assert(r == HSMresult_t::DONE);
      // If the session got picked up immediately by another thread the transact_count could be greater
      ink_release_assert(transact_count >= released_transactions);
    }
  } else { // Not to be released
    if (transact_count == released_transactions) {
      // Make sure we previously called release() or do_io_close() on the session
      ink_release_assert(state != PooledState::INIT);
      do_io_close(HTTP_ERRNO);
    } else {
      ink_release_assert(transact_count == released_transactions);
    }
  }
}

ProxyTransaction *
Http1ServerSession::new_transaction()
{
  state = PooledState::SSN_IN_USE;
  transact_count++;
  ink_release_assert(transact_count == (released_transactions + 1));
  trans.set_reader(this->get_remote_reader());
  return &trans;
}

std::function<PoolableSession *()> create_h1_server_session = []() -> PoolableSession * {
  return httpServerSessionAllocator.alloc();
};
