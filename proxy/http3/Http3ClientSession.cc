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

#include "Http3ClientSession.h"

Http3ClientSession::Http3ClientSession(NetVConnection *vc) : _client_vc(vc)
{
  this->_local_qpack  = new QPACK(static_cast<QUICNetVConnection *>(vc));
  this->_remote_qpack = new QPACK(static_cast<QUICNetVConnection *>(vc));
}

Http3ClientSession::~Http3ClientSession()
{
  this->_client_vc = nullptr;
  for (Http3ClientTransaction *t = this->_transaction_list.head; t; t = static_cast<Http3ClientTransaction *>(t->link.next)) {
    delete t;
  }
  delete this->_local_qpack;
  delete this->_remote_qpack;
}

VIO *
Http3ClientSession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  ink_assert(false);
  return nullptr;
}

VIO *
Http3ClientSession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  ink_assert(false);
  return nullptr;
}

void
Http3ClientSession::do_io_close(int lerrno)
{
  // TODO
  return;
}

void
Http3ClientSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(false);
  return;
}

void
Http3ClientSession::reenable(VIO *vio)
{
  ink_assert(false);
  return;
}

void
Http3ClientSession::destroy()
{
  ink_assert(false);
  return;
}

void
Http3ClientSession::start()
{
  ink_assert(false);
  return;
}

void
Http3ClientSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor)
{
  this->con_id = static_cast<QUICConnection *>(reinterpret_cast<QUICNetVConnection *>(new_vc))->connection_id();

  return;
}

NetVConnection *
Http3ClientSession::get_netvc() const
{
  return this->_client_vc;
}

int
Http3ClientSession::get_transact_count() const
{
  return 0;
}

const char *
Http3ClientSession::get_protocol_string() const
{
  return IP_PROTO_TAG_HTTP_QUIC.data();
}

void
Http3ClientSession::release(ProxyClientTransaction *trans)
{
  return;
}

int
Http3ClientSession::populate_protocol(std::string_view *result, int size) const
{
  int retval = 0;
  if (size > retval) {
    result[retval++] = IP_PROTO_TAG_HTTP_QUIC;
    if (size > retval) {
      retval += super::populate_protocol(result + retval, size - retval);
    }
  }
  return retval;
}

void
Http3ClientSession::increment_current_active_client_connections_stat()
{
  // TODO Implement stats
}

void
Http3ClientSession::decrement_current_active_client_connections_stat()
{
  // TODO Implement stats
}

void
Http3ClientSession::add_transaction(Http3ClientTransaction *trans)
{
  this->_transaction_list.enqueue(trans);
  return;
}

// this->_transaction_list should be map?
Http3ClientTransaction *
Http3ClientSession::get_transaction(QUICStreamId id)
{
  for (Http3ClientTransaction *t = this->_transaction_list.head; t; t = static_cast<Http3ClientTransaction *>(t->link.next)) {
    if (t->get_transaction_id() == static_cast<int>(id)) {
      return t;
    }
  }
  return nullptr;
}

QPACK *
Http3ClientSession::local_qpack()
{
  return this->_local_qpack;
}

QPACK *
Http3ClientSession::remote_qpack()
{
  return this->_remote_qpack;
}
