/** @file

  Http1ClientTransaction.cc - The Client Transaction class for Http1*

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
Http1ClientTransaction::release()
{
  // Turn off reading until we are done with the SM
  // At that point the transaction/session with either be closed
  // or be put into keep alive state to wait from the next transaction
  this->do_io_read(this->_sm, 0, nullptr);
  _proxy_ssn->clear_session_active();
}

void
Http1ClientTransaction::transaction_done()
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  super_type::transaction_done();
  if (_proxy_ssn) {
    static_cast<Http1ClientSession *>(_proxy_ssn)->release_transaction();
  }
}

bool
Http1ClientTransaction::allow_half_open() const
{
  bool config_allows_it = (_sm) ? _sm->t_state.txn_conf->allow_half_open > 0 : true;
  if (config_allows_it) {
    // Check with the session to make sure the underlying transport allows the half open scenario
    return static_cast<Http1ClientSession *>(_proxy_ssn)->allow_half_open();
  }
  return false;
}

void
Http1ClientTransaction::increment_transactions_stat()
{
  HTTP_INCREMENT_DYN_STAT(http_current_client_transactions_stat);
}

void
Http1ClientTransaction::decrement_transactions_stat()
{
  HTTP_DECREMENT_DYN_STAT(http_current_client_transactions_stat);
}
