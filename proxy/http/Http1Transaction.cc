/** @file

  Http1Transaction.cc - The Transaction class for Http1*

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

#include "Http1Transaction.h"
#include "Http1ClientSession.h"
#include "HttpSM.h"

void
Http1Transaction::release(IOBufferReader *r)
{
  // Must set this inactivity count here rather than in the session because the state machine
  // is not available then
  MgmtInt ka_in = current_reader->t_state.txn_conf->keep_alive_no_activity_timeout_in;
  set_inactivity_timeout(HRTIME_SECONDS(ka_in));

  proxy_ssn->clear_session_active();
  proxy_ssn->ssn_last_txn_time = Thread::get_hrtime();

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
Http1Transaction::set_proxy_ssn(ProxySession *new_proxy_ssn)
{
  Http1ClientSession *http1_proxy_ssn = dynamic_cast<Http1ClientSession *>(new_proxy_ssn);

  if (http1_proxy_ssn) {
    outbound_port        = http1_proxy_ssn->outbound_port;
    outbound_ip4         = http1_proxy_ssn->outbound_ip4;
    outbound_ip6         = http1_proxy_ssn->outbound_ip6;
    outbound_transparent = http1_proxy_ssn->f_outbound_transparent;
    super_type::set_proxy_ssn(new_proxy_ssn);
  } else {
    proxy_ssn = nullptr;
  }
}

void
Http1Transaction::transaction_done()
{
  if (proxy_ssn) {
    static_cast<Http1ClientSession *>(proxy_ssn)->release_transaction();
  }
}

bool
Http1Transaction::allow_half_open() const
{
  bool config_allows_it = (current_reader) ? current_reader->t_state.txn_conf->allow_half_open > 0 : true;
  if (config_allows_it) {
    // Check with the session to make sure the underlying transport allows the half open scenario
    return static_cast<Http1ClientSession *>(proxy_ssn)->allow_half_open();
  }
  return false;
}

void
Http1Transaction::increment_client_transactions_stat()
{
  HTTP_INCREMENT_DYN_STAT(http_current_client_transactions_stat);
}

void
Http1Transaction::decrement_client_transactions_stat()
{
  HTTP_DECREMENT_DYN_STAT(http_current_client_transactions_stat);
}
