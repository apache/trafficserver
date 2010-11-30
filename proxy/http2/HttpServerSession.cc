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

   HttpServerSession.cc

   Description:


 ****************************************************************************/

#include "ink_config.h"
#include "Allocator.h"
#include "HttpServerSession.h"
#include "HttpSessionManager.h"
#include "HttpSM.h"

static int64 next_ss_id = (int64) 0;
ClassAllocator<HttpServerSession> httpServerSessionAllocator("httpServerSessionAllocator");

void
HttpServerSession::destroy()
{
  ink_release_assert(server_vc == NULL);
  ink_assert(read_buffer);
  ink_assert(server_trans_stat == 0);
  magic = HTTP_SS_MAGIC_DEAD;
  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = NULL;
  }

  mutex.clear();

  httpServerSessionAllocator.free(this);
}

HttpServerSession *
HttpServerSession::allocate()
{
  ink_assert(0);
  return NULL;
}

void
HttpServerSession::new_connection(NetVConnection * new_vc)
{
  ink_assert(new_vc != NULL);
  server_vc = new_vc;

#ifdef TRANSACTION_ON_A_THREAD
  mutex = new_vc->thread->mutex;
#else
  mutex = new_vc->mutex;
#endif

  // Unique client session identifier.
  con_id = ink_atomic_increment64((int64 *) (&next_ss_id), 1);

  magic = HTTP_SS_MAGIC_ALIVE;
  HTTP_SUM_GLOBAL_DYN_STAT(http_current_server_connections_stat, 1); // Update the true global stat
  HTTP_INCREMENT_DYN_STAT(http_total_server_connections_stat);
  // Check to see if we are limiting the number of connections
  // per host
  if (enable_origin_connection_limiting == true) {
    if(connection_count == NULL)
      connection_count = ConnectionCount::getInstance();
    connection_count->incrementCount(server_ip);
    Debug("http_ss", "[%lld] new connection, ip: %u, count: %u", con_id, server_ip, connection_count->getCount(server_ip));
  }
#ifdef LAZY_BUF_ALLOC
  read_buffer = new_empty_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
#else
  read_buffer = new_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
#endif
  buf_reader = read_buffer->alloc_reader();
  Debug("http_ss", "[%lld] session born, netvc %p", con_id, new_vc);
  state = HSS_INIT;
}

VIO *
HttpServerSession::do_io_read(Continuation * c, int64 nbytes, MIOBuffer * buf)
{
  return server_vc->do_io_read(c, nbytes, buf);
}

VIO *
HttpServerSession::do_io_write(Continuation * c, int64 nbytes, IOBufferReader * buf, bool owner)
{
  return server_vc->do_io_write(c, nbytes, buf, owner);
}

void
HttpServerSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  server_vc->do_io_shutdown(howto);
}

void
HttpServerSession::do_io_close(int alerrno)
{
  if (state == HSS_ACTIVE) {
    HTTP_DECREMENT_DYN_STAT(http_current_server_transactions_stat);
    this->server_trans_stat--;
  }

  server_vc->do_io_close(alerrno);
  Debug("http_ss", "[%lld] session closed", con_id);
  server_vc = NULL;

  HTTP_SUM_GLOBAL_DYN_STAT(http_current_server_connections_stat, -1); // Make sure to work on the global stat
  HTTP_SUM_DYN_STAT(http_transactions_per_server_con, transact_count);

  // Check to see if we are limiting the number of connections
  // per host
  if (enable_origin_connection_limiting == true) {
    if (connection_count->getCount(server_ip) > 0) {
      connection_count->incrementCount(server_ip, -1);
      Debug("http_ss", "[%lld] connection closed, ip: %u, count: %u",
            con_id, server_ip, connection_count->getCount(server_ip));
    } else {
      Error("http_ss",
            "[%lld] number of connections should be greater then zero: %u",
            con_id, connection_count->getCount(server_ip));
    }
  }

  if (to_parent_proxy) {
    HTTP_DECREMENT_DYN_STAT(http_current_parent_proxy_connections_stat);
  }
  destroy();
}

void
HttpServerSession::reenable(VIO * vio)
{
  server_vc->reenable(vio);
}

// void HttpServerSession::release()
//
//   Releases the session for K-A reuse
//
void
HttpServerSession::release()
{

  // Set our state to KA for stat issues
  state = HSS_KA_SHARED;

  // Private sessions are never released back to the shared
  //  pool
  if (private_session || HttpConfig::m_master.share_server_sessions == 0) {
    this->do_io_close();
    return;
  }

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
