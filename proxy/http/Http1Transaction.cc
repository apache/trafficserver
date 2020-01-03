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

Http1Transaction::~Http1Transaction()
{
  Note("~Http1Transaction()");
  if (_proxy_ssn) {
    // Must set this inactivity count here rather than in the session because the state machine
    // is not available then
    Note("1");
    ink_assert(_sm);
    ink_assert(_sm->t_state.txn_conf);
    MgmtInt ka_in = _sm->t_state.txn_conf->keep_alive_no_activity_timeout_in;
    set_inactivity_timeout(HRTIME_SECONDS(ka_in));
    Note("2");
    _proxy_ssn->clear_session_active();
    _proxy_ssn->ssn_last_txn_time = Thread::get_hrtime();
    Note("3");
    if (_proxy_ssn->is_client()) {
      HTTP_DECREMENT_DYN_STAT(http_current_client_transactions_stat);
    } else {
      HTTP_DECREMENT_DYN_STAT(http_current_server_transactions_stat);
    }
  }
  Note("5");
}

void
Http1Transaction::transaction_done()
{
  Note("transaction_done()");
  if (_proxy_ssn->get_netvc()) {
    this->do_io_close();
  }

  delete this;
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
Http1Transaction::increment_txn_stat()
{
  ink_assert(_proxy_ssn);
  if (_proxy_ssn->get_netvc()->get_context() == NET_VCONNECTION_IN) {
    HTTP_INCREMENT_DYN_STAT(http_current_client_transactions_stat);
  } else {
    HTTP_INCREMENT_DYN_STAT(http_current_server_transactions_stat);
  }
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
}

void
Http1Transaction::do_io_shutdown(ShutdownHowTo_t howto)
{
  _proxy_ssn->do_io_shutdown(howto);
}

void
Http1Transaction::set_active_timeout(ink_hrtime timeout_in)
{
  if (_proxy_ssn)
    _proxy_ssn->set_active_timeout(timeout_in);
}
void
Http1Transaction::set_inactivity_timeout(ink_hrtime timeout_in)
{
  if (_proxy_ssn)
    _proxy_ssn->set_inactivity_timeout(timeout_in);
}
void
Http1Transaction::cancel_inactivity_timeout()
{
  if (_proxy_ssn)
    _proxy_ssn->cancel_inactivity_timeout();
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
