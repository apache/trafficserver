/** @file

  HTTP state machine

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

#include "proxy/http/Http1ClientSession.h"
#include "proxy/http/Http1ClientTransaction.h"
#include "proxy/http/HttpSessionAccept.h"
#include "proxy/http/HttpUserAgent.h"
#include "iocore/eventsystem/VConnection.h"
#include "proxy/Milestones.h"
#include "../../../iocore/net/P_SSLNetVConnection.h"

#include <catch.hpp>

#include <cstring>

class Http1ClientTestSession final : public Http1ClientSession
{
public:
  int get_transact_count() const override;
  void set_transact_count(int count);
  void set_vc(NetVConnection *new_vc);

private:
  int m_transact_count{0};
};

int
Http1ClientTestSession::get_transact_count() const
{
  return m_transact_count;
}

void
Http1ClientTestSession::set_transact_count(int count)
{
  m_transact_count = count;
}

void
Http1ClientTestSession::set_vc(NetVConnection *new_vc)
{
  _vc = new_vc;
};

TEST_CASE("tcp_reused should be set correctly when a session is attached.")
{
  HttpUserAgent user_agent;
  TransactionMilestones milestones;

  Http1ClientTestSession ssn;
  SSLNetVConnection netvc;
  ssn.set_vc(&netvc);
  HttpSessionAccept::Options options;
  ssn.accept_options = &options;
  Http1ClientTransaction txn{&ssn};

  SECTION("When a transaction is the first one, "
          "then tcp_reused should be false.")
  {
    ssn.set_transact_count(1);
    user_agent.set_txn(&txn, milestones);
    CHECK(user_agent.get_client_tcp_reused() == false);
  }

  SECTION("When a transaction is the second one, "
          "then tcp_reused should be true.")
  {
    ssn.set_transact_count(2);
    user_agent.set_txn(&txn, milestones);
    CHECK(user_agent.get_client_tcp_reused() == true);
  }
}
