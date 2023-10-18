/** @file

  Http1ClientTransaction.h - The Client Transaction class for Http1*

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

#include "Http1Transaction.h"

class Http1ClientTransaction : public Http1Transaction
{
public:
  using super_type = Http1Transaction;

  Http1ClientTransaction() {}
  Http1ClientTransaction(ProxySession *session) : super_type(session) {}

  ////////////////////
  // Methods
  void release() override;

  bool allow_half_open() const override;
  void transaction_done() override;
  void increment_transactions_stat() override;
  void decrement_transactions_stat() override;

  ////////////////////
  // Variables

protected:
  bool outbound_transparent{false};
};
