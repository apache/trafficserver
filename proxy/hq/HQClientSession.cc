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

#include "HQClientSession.h"

HQClientSession::HQClientSession(NetVConnection *vc) : _client_vc(vc)
{
}

HQClientSession::~HQClientSession()
{
  this->_client_vc = nullptr;
  for (HQClientTransaction *t = this->_transaction_list.head; t; t = static_cast<HQClientTransaction *>(t->link.next)) {
    delete t;
  }
}

VIO *
HQClientSession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  ink_assert(false);
  return nullptr;
}

VIO *
HQClientSession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  ink_assert(false);
  return nullptr;
}

void
HQClientSession::do_io_close(int lerrno)
{
  ink_assert(false);
  return;
}

void
HQClientSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(false);
  return;
}

void
HQClientSession::reenable(VIO *vio)
{
  ink_assert(false);
  return;
}

void
HQClientSession::destroy()
{
  ink_assert(false);
  return;
}

void
HQClientSession::start()
{
  ink_assert(false);
  return;
}

void
HQClientSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor)
{
  ink_assert(false);
  return;
}

NetVConnection *
HQClientSession::get_netvc() const
{
  return this->_client_vc;
}

void
HQClientSession::release_netvc()
{
  ink_assert(false);
  return;
}

int
HQClientSession::get_transact_count() const
{
  return 0;
}

const char *
HQClientSession::get_protocol_string() const
{
  return "hq";
}

void
HQClientSession::release(ProxyClientTransaction *trans)
{
  ink_assert(false);
  return;
}

void
HQClientSession::add_transaction(HQClientTransaction *trans)
{
  this->_transaction_list.enqueue(trans);
  return;
}
