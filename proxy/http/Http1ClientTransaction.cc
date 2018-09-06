/** @file

  Http1ClientTransaction.cc - The Transaction class for Http1*

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

#include "Http1ClientTransaction.h"
#include "Http1ClientSession.h"
#include "HttpSM.h"

void
Http1ClientTransaction::release(IOBufferReader *r)
{
  // Must set this inactivity count here rather than in the session because the state machine
  // is not availble then
  MgmtInt ka_in = current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_in;
  set_inactivity_timeout(HRTIME_SECONDS(ka_in));

  parent->clear_session_active();
  parent->ssn_last_txn_time = Thread::get_hrtime();

  // Make sure that the state machine is returning
  //  correct buffer reader
  ink_assert(r == sm_reader);
  if (r != sm_reader) {
    this->do_io_close();
  } else {
    super_type::release(r);
  }
}

void
Http1ClientTransaction::set_parent(ProxyClientSession *new_parent)
{
  parent                           = new_parent;
  Http1ClientSession *http1_parent = dynamic_cast<Http1ClientSession *>(new_parent);
  if (http1_parent) {
    outbound_port        = http1_parent->outbound_port;
    outbound_ip4         = http1_parent->outbound_ip4;
    outbound_ip6         = http1_parent->outbound_ip6;
    outbound_transparent = http1_parent->f_outbound_transparent;
  }
  super_type::set_parent(new_parent);
}

void
Http1ClientTransaction::transaction_done()
{
  if (parent) {
    static_cast<Http1ClientSession *>(parent)->release_transaction();
  }
}

bool
Http1ClientTransaction::allow_half_open() const
{
  bool config_allows_it = (current_reader) ? current_reader->t_state.txn_conf->allow_half_open > 0 : true;
  if (config_allows_it) {
    // Check with the session to make sure the underlying transport allows the half open scenario
    return static_cast<Http1ClientSession *>(parent)->allow_half_open();
  }
  return false;
}
