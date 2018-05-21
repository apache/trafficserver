/** @file

  A brief file description

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

#include "ProxyClientSession.h"
#include "HQClientTransaction.h"

class HQClientSession : public ProxyClientSession
{
public:
  typedef ProxyClientSession super; ///< Parent type.

  HQClientSession(NetVConnection *vc);
  ~HQClientSession();

  // Implement VConnection interface
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  // Implement ProxyClienSession interface
  void start() override;
  void destroy() override;
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor) override;
  NetVConnection *get_netvc() const override;
  void release_netvc() override;
  int get_transact_count() const override;
  const char *get_protocol_string() const override;
  void release(ProxyClientTransaction *trans) override;
  int populate_protocol(std::string_view *result, int size) const override;

  // HQClientSession specific methods
  void add_transaction(HQClientTransaction *);
  HQClientTransaction *get_transaction(QUICStreamId);

private:
  NetVConnection *_client_vc = nullptr;
  Queue<HQClientTransaction> _transaction_list;
};
