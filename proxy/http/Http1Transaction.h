/** @file

  Http1Transaction.h - The Transaction class for Http1*

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

#pragma once

#include "../ProxyTransaction.h"

class Continuation;

class Http1Transaction : public ProxyTransaction
{
public:
  using super_type = ProxyTransaction;

  Http1Transaction() {}

  ////////////////////
  // Methods
  void release(IOBufferReader *r) override;
  void destroy() override; // todo make ~Http1Transaction()

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                   bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  bool allow_half_open() const override;
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_inactivity_timeout() override;
  void transaction_done() override;
  int get_transaction_id() const override;
  void increment_client_transactions_stat() override;
  void decrement_client_transactions_stat() override;

  void set_reader(IOBufferReader *reader);

  ////////////////////
  // Variables

protected:
  bool outbound_transparent{false};
};

//////////////////////////////////
// INLINE

inline void
Http1Transaction::set_reader(IOBufferReader *reader)
{
  _reader = reader;
}
