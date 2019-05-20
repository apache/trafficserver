/** @file

  ProxyClientTransaction - Base class for protocol client transactions.

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

#include "http/HttpSM.h"
#include "http/Http1ServerSession.h"
#include "Plugin.h"

#define HttpTxnDebug(fmt, ...) SsnDebug(this, "http_txn", fmt, __VA_ARGS__)

ProxyClientTransaction::ProxyClientTransaction() : VConnection(nullptr) {}

void
ProxyClientTransaction::new_transaction()
{
  ink_assert(current_reader == nullptr);

  // Defensive programming, make sure nothing persists across
  // connection re-use

  ink_release_assert(parent != nullptr);
  current_reader = HttpSM::allocate();
  current_reader->init();
  HttpTxnDebug("[%" PRId64 "] Starting transaction %d using sm [%" PRId64 "]", parent->connection_id(),
               parent->get_transact_count(), current_reader->sm_id);

  PluginIdentity *pi = dynamic_cast<PluginIdentity *>(this->get_netvc());
  if (pi) {
    current_reader->plugin_tag = pi->getPluginTag();
    current_reader->plugin_id  = pi->getPluginId();
  }

  this->increment_client_transactions_stat();
  current_reader->attach_client_session(this, sm_reader);
}

void
ProxyClientTransaction::release(IOBufferReader *r)
{
  HttpTxnDebug("[%" PRId64 "] session released by sm [%" PRId64 "]", parent ? parent->connection_id() : 0,
               current_reader ? current_reader->sm_id : 0);

  this->decrement_client_transactions_stat();

  // Pass along the release to the session
  if (parent) {
    parent->release(this);
  }
}

void
ProxyClientTransaction::attach_server_session(HttpServerSession *ssession, bool transaction_done)
{
  parent->attach_server_session(ssession, transaction_done);
}

void
ProxyClientTransaction::destroy()
{
  current_reader = nullptr;
  this->mutex.clear();
}

Action *
ProxyClientTransaction::adjust_thread(Continuation *cont, int event, void *data)
{
  NetVConnection *vc   = this->get_netvc();
  EThread *this_thread = this_ethread();
  if (vc && vc->thread != this_thread) {
    if (vc->thread->is_event_type(ET_NET)) {
      return vc->thread->schedule_imm(cont, event, data);
    } else { // Not a net thread, take over this thread
      vc->thread = this_thread;
    }
  }
  return nullptr;
}

void
ProxyClientTransaction::set_rx_error_code(ProxyError e)
{
  if (this->current_reader) {
    this->current_reader->t_state.client_info.rx_error_code = e;
  }
}

void
ProxyClientTransaction::set_tx_error_code(ProxyError e)
{
  if (this->current_reader) {
    this->current_reader->t_state.client_info.tx_error_code = e;
  }
}
