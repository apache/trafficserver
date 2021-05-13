/** @file

  Http1ServerTransaction.h - The Server Transaction class for Http1*

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

class Http1ServerTransaction : public Http1Transaction
{
public:
  using super_type = Http1Transaction;

  Http1ServerTransaction() {}
  Http1ServerTransaction(ProxySession *session) : super_type(session) {}
  ~Http1ServerTransaction() override {}

  ////////////////////
  // Methods
  void release() override;
  // void destroy() override; // todo make ~Http1Transaction()

  void increment_transactions_stat() override;
  void decrement_transactions_stat() override;
  void transaction_done() override;

  void force_close();

  ////////////////////
  // Variables

protected:
  bool outbound_transparent{false};
};
