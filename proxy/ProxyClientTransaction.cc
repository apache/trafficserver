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
#include "http/HttpServerSession.h"
#include "Plugin.h"

#define DebugHttpTxn(fmt, ...) DebugSsn(this, "http_txn", fmt, __VA_ARGS__)

ProxyClientTransaction::ProxyClientTransaction() : VConnection(NULL), parent(NULL), current_reader(NULL), restart_immediate(false)
{
}

void
ProxyClientTransaction::new_transaction()
{
  ink_assert(current_reader == NULL);

  // Defensive programming, make sure nothing persists across
  // connection re-use

  ink_release_assert(parent != NULL);
  current_reader = HttpSM::allocate();
  current_reader->init();
  DebugHttpTxn("[%" PRId64 "] Starting transaction %d using sm [%" PRId64 "]", parent->connection_id(),
               parent->get_transact_count(), current_reader->sm_id);

  // This is a temporary hack until we get rid of SPDY and can use virutal methods entirely
  // to track protocol
  PluginIdentity *pi = dynamic_cast<PluginIdentity *>(this->get_netvc());
  if (pi) {
    current_reader->plugin_tag = pi->getPluginTag();
    current_reader->plugin_id  = pi->getPluginId();
  } else {
    const char *protocol_str = this->get_protocol_string();
    // We don't set the plugin_tag for http, though in future we should probably log http as protocol
    if (strlen(protocol_str) != 4 || strncmp("http", protocol_str, 4)) {
      current_reader->plugin_tag = protocol_str;
      // Since there is no more plugin, there is no plugin id for http/2
      // We are copying along the plugin_tag as a standin for protocol name for logging
      // and to detect a case in HttpTransaction (TS-3954)
    }
  }
  current_reader->attach_client_session(this, sm_reader);
}

void
ProxyClientTransaction::release(IOBufferReader *r)
{
  ink_assert(current_reader != NULL);

  DebugHttpTxn("[%" PRId64 "] session released by sm [%" PRId64 "]", parent ? parent->connection_id() : 0,
               current_reader ? current_reader->sm_id : 0);

  current_reader = NULL; // Clear reference to SM

  // Pass along the release to the session
  if (parent)
    parent->release(this);
}

void
ProxyClientTransaction::attach_server_session(HttpServerSession *ssession, bool transaction_done)
{
  parent->attach_server_session(ssession, transaction_done);
}

Action *
ProxyClientTransaction::adjust_thread(Continuation *cont, int event, void *data)
{
  NetVConnection *vc   = this->get_netvc();
  EThread *this_thread = this_ethread();
  if (vc && vc->thread != this_thread) {
    if (vc->thread->is_event_type(ET_NET) || vc->thread->is_event_type(SSLNetProcessor::ET_SSL)) {
      return vc->thread->schedule_imm(cont, event, data);
    } else { // Not a net thread, take over this thread
      vc->thread = this_thread;
    }
  }
  return NULL;
}
