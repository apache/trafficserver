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

ClassAllocator<Http1ServerSession> httpServerSessionAllocator("httpServerSessionAllocator");

void
Http1ServerSession::destroy()
{
  ink_release_assert(server_vc == nullptr);
  ink_assert(read_buffer);
  ink_assert(server_trans_stat == 0);
  magic = HTTP_MAGIC_DEAD;
  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = nullptr;
  }

  mutex.clear();
  if (TS_SERVER_SESSION_SHARING_POOL_THREAD == sharing_pool) {
    THREAD_FREE(this, httpServerSessionAllocator, this_thread());
  } else {
    httpServerSessionAllocator.free(this);
  }
}

void
Http1ServerSession::new_connection(NetVConnection *new_vc)
{
  ink_assert(new_vc != nullptr);
  server_vc    = new_vc;
  _is_outbound = true;

  // Used to do e.g. mutex = new_vc->thread->mutex; when per-thread pools enabled
  mutex = new_vc->mutex;

  // Unique client session identifier.
  _id = ProxySession::next_id();

  magic = HTTP_MAGIC_ALIVE;
  HTTP_SUM_GLOBAL_DYN_STAT(http_current_server_connections_stat, 1); // Update the true global stat
  HTTP_INCREMENT_DYN_STAT(http_total_server_connections_stat);

  read_buffer = new_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);

  buf_reader = read_buffer->alloc_reader();
  Debug("http_ss", "[%" PRId64 "] session born, netvc %p", get_id(), new_vc);
  state = HSS_INIT;

  new_vc->set_tcp_congestion_control(SERVER_SIDE);
}

void
Http1ServerSession::enable_outbound_connection_tracking(OutboundConnTrack::Group *group)
{
  ink_assert(nullptr == conn_track_group);
  conn_track_group = group;
  if (is_debug_tag_set("http_ss")) {
    ts::LocalBufferWriter<256> w;
    w.print("[{}] new connection, ip: {}, group ({}), count: {}\0", get_id(), get_server_ip(), *group, group->_count);
    Debug("http_ss", "%s", w.data());
  }
}

VIO *
Http1ServerSession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return server_vc ? server_vc->do_io_read(c, nbytes, buf) : nullptr;
}

VIO *
Http1ServerSession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return server_vc ? server_vc->do_io_write(c, nbytes, buf, owner) : nullptr;
}

void
Http1ServerSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  server_vc->do_io_shutdown(howto);
}

void
Http1ServerSession::do_io_close(int alerrno)
{
  ts::LocalBufferWriter<256> w;
  bool debug_p = is_debug_tag_set("http_ss");

  if (state == HSS_ACTIVE) {
    HTTP_DECREMENT_DYN_STAT(http_current_server_transactions_stat);
    this->server_trans_stat--;
  }

  if (debug_p) {
    w.print("[{}] session close: nevtc {:x}", get_id(), server_vc);
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
      Error("[http_ss] [%" PRId64 "] number of connections should be greater than or equal to zero: %u", get_id(),
            conn_track_group->_count.load());
    }
  }
  if (debug_p) {
    Debug("http_ss", "%.*s", static_cast<int>(w.size()), w.data());
  }

  if (server_vc) {
    server_vc->do_io_close(alerrno);
  }
  server_vc = nullptr;

  if (to_parent_proxy) {
    HTTP_DECREMENT_DYN_STAT(http_current_parent_proxy_connections_stat);
  }
  this->destroy();
}

void
Http1ServerSession::reenable(VIO *vio)
{
  server_vc->reenable(vio);
}

// void Http1ServerSession::release()
//
//   Releases the session for K-A reuse
//
void
Http1ServerSession::release()
{
  Debug("http_ss", "Releasing session, private_session=%d, sharing_match=%d", private_session, sharing_match);
  // Set our state to KA for stat issues
  state = HSS_KA_SHARED;

  server_vc->control_flags.set_flags(0);

  // Private sessions are never released back to the shared pool
  if (private_session || TS_SERVER_SESSION_SHARING_MATCH_NONE == sharing_match) {
    this->do_io_close();
    return;
  }

  // Make sure the vios for the current SM are cleared
  server_vc->do_io_read(nullptr, 0, nullptr);
  server_vc->do_io_write(nullptr, 0, nullptr);

  HSMresult_t r = httpSessionManager.release_session(this);

  if (r == HSM_RETRY) {
    // Session could not be put in the session manager
    //  due to lock contention
    // FIX:  should retry instead of closing
    this->do_io_close();
  } else {
    // The session was successfully put into the session
    //    manager and it will manage it
    // (Note: should never get HSM_NOT_FOUND here)
    ink_assert(r == HSM_DONE);
  }
}

NetVConnection *
Http1ServerSession::get_netvc() const
{
  return server_vc;
};

void
Http1ServerSession::set_netvc(NetVConnection *new_vc)
{
  server_vc = new_vc;
}

// Keys for matching hostnames
IpEndpoint const &
Http1ServerSession::get_server_ip() const
{
  ink_release_assert(server_vc != nullptr);
  return server_vc->get_remote_endpoint();
}

int
Http1ServerSession::populate_protocol(std::string_view *result, int size) const
{
  auto vc = this->get_netvc();
  return vc ? vc->populate_protocol(result, size) : 0;
}

const char *
Http1ServerSession::protocol_contains(std::string_view tag_prefix) const
{
  auto vc = this->get_netvc();
  return vc ? vc->protocol_contains(tag_prefix) : nullptr;
}

const char *
Http1ServerSession::get_protocol_string() const
{
  return "http";
};
