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
}

void
Http1Transaction::destroy() // todo make ~Http1Transaction()
{
  _sm = nullptr;
}

void
Http1Transaction::transaction_done()
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  super_type::transaction_done();
  if (_proxy_ssn) {
    static_cast<Http1ClientSession *>(_proxy_ssn)->release_transaction();
  }
}

void
Http1Transaction::reenable(VIO *vio)
{
  _proxy_ssn->reenable(vio);
}

bool
Http1Transaction::allow_half_open() const
{
  bool config_allows_it = (_sm) ? _sm->t_state.txn_conf->allow_half_open > 0 : true;
  if (config_allows_it) {
    // Check with the session to make sure the underlying transport allows the half open scenario
    return static_cast<Http1ClientSession *>(_proxy_ssn)->allow_half_open();
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

// Implement VConnection interface.
VIO *
Http1Transaction::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return _proxy_ssn->do_io_read(c, nbytes, buf);
}
VIO *
Http1Transaction::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return _proxy_ssn->do_io_write(c, nbytes, buf, owner);
}

void
Http1Transaction::do_io_close(int lerrno)
{
  _proxy_ssn->do_io_close(lerrno);
  // this->destroy(); Parent owns this data structure.  No need for separate destroy.
}

void
Http1Transaction::do_io_shutdown(ShutdownHowTo_t howto)
{
  _proxy_ssn->do_io_shutdown(howto);
}

void
Http1Transaction::set_active_timeout(ink_hrtime timeout_in)
{
  if (_proxy_ssn) {
    _proxy_ssn->set_active_timeout(timeout_in);
  }
}
void
Http1Transaction::set_inactivity_timeout(ink_hrtime timeout_in)
{
  if (_proxy_ssn) {
    _proxy_ssn->set_inactivity_timeout(timeout_in);
  }
}
void
Http1Transaction::cancel_inactivity_timeout()
{
  if (_proxy_ssn) {
    _proxy_ssn->cancel_inactivity_timeout();
  }
}
//
int
Http1Transaction::get_transaction_id() const
{
  // For HTTP/1 there is only one on-going transaction at a time per session/connection.  Therefore, the transaction count can be
  // presumed not to increase during the lifetime of a transaction, thus this function will return a consistent unique transaction
  // identifier.
  //
  return _proxy_ssn->get_transact_count();
}
