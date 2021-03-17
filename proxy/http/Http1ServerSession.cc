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
#include "tscore/ink_config.h"
#include "tscore/BufferWriter.h"
#include "tscore/bwf_std_format.h"
#include "tscore/Allocator.h"
#include "Http1ServerSession.h"
#include "HttpSessionManager.h"
#include "HttpSM.h"

ClassAllocator<Http1ServerSession, true> httpServerSessionAllocator("httpServerSessionAllocator");

void
Http1ServerSession::destroy()
{
  ink_release_assert(_vc == nullptr);
  ink_assert(read_buffer);
  magic = HTTP_SS_MAGIC_DEAD;
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

  magic = HTTP_SS_MAGIC_ALIVE;
  HTTP_SUM_GLOBAL_DYN_STAT(http_current_server_connections_stat, 1); // Update the true global stat
  HTTP_INCREMENT_DYN_STAT(http_total_server_connections_stat);

  if (iobuf == nullptr) {
    read_buffer = new_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
    buf_reader  = read_buffer->alloc_reader();
  } else {
    read_buffer = iobuf;
    buf_reader  = reader;
  }
  Debug("http_ss", "[%" PRId64 "] session born, netvc %p", con_id, new_vc);
  state = INIT;

  new_vc->set_tcp_congestion_control(SERVER_SIDE);
}

void
Http1ServerSession::enable_outbound_connection_tracking(OutboundConnTrack::Group *group)
{
  ink_assert(nullptr == conn_track_group);
  conn_track_group = group;
  if (is_debug_tag_set("http_ss")) {
    ts::LocalBufferWriter<256> w;
    w.print("[{}] new connection, ip: {}, group ({}), count: {}\0", con_id, get_server_ip(), *group, group->_count);
    Debug("http_ss", "%s", w.data());
  }
}

void
Http1ServerSession::do_io_close(int alerrno)
{
  ts::LocalBufferWriter<256> w;
  bool debug_p = is_debug_tag_set("http_ss");

  if (state == SSN_IN_USE) {
    HTTP_DECREMENT_DYN_STAT(http_current_server_transactions_stat);
  }

  if (debug_p) {
    w.print("[{}] session close: nevtc {:x}", con_id, _vc);
  }

  HTTP_SUM_GLOBAL_DYN_STAT(http_current_server_connections_stat, -1); // Make sure to work on the global stat
  HTTP_SUM_DYN_STAT(http_transactions_per_server_con, transact_count);

  // Update upstream connection tracking data if present.
  if (conn_track_group) {
    if (conn_track_group->_count >= 0) {
      auto n = (conn_track_group->_count)--;
      if (debug_p) {
        w.print(" conn track group ({}) count {}", conn_track_group->_key, n);
      }
    } else {
      // A bit dubious, as there's no guarantee it's still negative, but even that would be interesting to know.
      Error("[http_ss] [%" PRId64 "] number of connections should be greater than or equal to zero: %u", con_id,
            conn_track_group->_count.load());
    }
  }
  if (debug_p) {
    Debug("http_ss", "%.*s", static_cast<int>(w.size()), w.data());
  }

  if (_vc) {
    _vc->do_io_close(alerrno);
  }
  _vc = nullptr;

  if (to_parent_proxy) {
    HTTP_DECREMENT_DYN_STAT(http_current_parent_proxy_connections_stat);
  }
  destroy();
}

// void Http1ServerSession::release()
//
//   Releases the session for K-A reuse
//
void
Http1ServerSession::release(ProxyTransaction *trans)
{
  Debug("http_ss", "Releasing session, private_session=%d, sharing_match=%d", this->is_private(), sharing_match);
  // Set our state to KA for stat issues
  state = KA_POOLED;

  _vc->control_flags.set_flags(0);

  // Private sessions are never released back to the shared pool
  if (this->is_private() || sharing_match == 0) {
    this->do_io_close();
    return;
  }

  // do not change the read/write cont and mutex yet
  // release_session() will either swap them with the
  // pool continuation with a valid read buffer or if
  // it fails, do_io_close() will clear the cont anyway

  HSMresult_t r = httpSessionManager.release_session(this);

  if (r == HSM_RETRY) {
    // Session could not be put in the session manager
    //  due to lock contention
    // FIX:  should retry instead of closing
    this->do_io_close();
    HTTP_INCREMENT_DYN_STAT(http_origin_shutdown_pool_lock_contention);
  } else {
    // The session was successfully put into the session
    //    manager and it will manage it
    // (Note: should never get HSM_NOT_FOUND here)
    ink_assert(r == HSM_DONE);
  }
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
