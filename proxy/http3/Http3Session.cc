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

#include "Http3Session.h"

#include "Http3.h"

//
// HQSession
//
HQSession ::~HQSession()
{
  for (HQTransaction *t = this->_transaction_list.head; t; t = static_cast<HQTransaction *>(t->link.next)) {
    delete t;
  }
}

void
HQSession::add_transaction(HQTransaction *trans)
{
  this->_transaction_list.enqueue(trans);

  return;
}

HQTransaction *
HQSession::get_transaction(QUICStreamId id)
{
  for (HQTransaction *t = this->_transaction_list.head; t; t = static_cast<HQTransaction *>(t->link.next)) {
    if (t->get_transaction_id() == static_cast<int>(id)) {
      return t;
    }
  }

  return nullptr;
}

VIO *
HQSession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  ink_assert(false);
  return nullptr;
}

VIO *
HQSession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  ink_assert(false);
  return nullptr;
}

void
HQSession::do_io_close(int lerrno)
{
  // TODO
  return;
}

void
HQSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(false);
  return;
}

void
HQSession::reenable(VIO *vio)
{
  ink_assert(false);
  return;
}

void
HQSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reade)
{
  this->con_id = static_cast<QUICConnection *>(reinterpret_cast<QUICNetVConnection *>(new_vc))->connection_id();

  return;
}

void
HQSession::start()
{
  ink_assert(false);
  return;
}

void
HQSession::destroy()
{
  ink_assert(false);
  return;
}

void
HQSession::release(ProxyTransaction *trans)
{
  return;
}

NetVConnection *
HQSession::get_netvc() const
{
  return this->_client_vc;
}

int
HQSession::get_transact_count() const
{
  return 0;
}

bool
HQSession::support_sni() const
{
  return this->_client_vc;
}

//
// Http3Session
//
Http3Session::Http3Session(NetVConnection *vc) : HQSession(vc)
{
  this->_local_qpack  = new QPACK(static_cast<QUICNetVConnection *>(vc), HTTP3_DEFAULT_MAX_HEADER_LIST_SIZE,
                                 HTTP3_DEFAULT_HEADER_TABLE_SIZE, HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS);
  this->_remote_qpack = new QPACK(static_cast<QUICNetVConnection *>(vc), HTTP3_DEFAULT_MAX_HEADER_LIST_SIZE,
                                  HTTP3_DEFAULT_HEADER_TABLE_SIZE, HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS);
}

Http3Session::~Http3Session()
{
  this->_client_vc = nullptr;
  delete this->_local_qpack;
  delete this->_remote_qpack;
}

const char *
Http3Session::get_protocol_string() const
{
  return IP_PROTO_TAG_HTTP_3.data();
}

int
Http3Session::populate_protocol(std::string_view *result, int size) const
{
  int retval = 0;
  if (size > retval) {
    result[retval++] = IP_PROTO_TAG_HTTP_3;
    if (size > retval) {
      retval += super::populate_protocol(result + retval, size - retval);
    }
  }
  return retval;
}

void
Http3Session::increment_current_active_client_connections_stat()
{
  // TODO Implement stats
}

void
Http3Session::decrement_current_active_client_connections_stat()
{
  // TODO Implement stats
}

QPACK *
Http3Session::local_qpack()
{
  return this->_local_qpack;
}

QPACK *
Http3Session::remote_qpack()
{
  return this->_remote_qpack;
}

//
// Http09Session
//
Http09Session::~Http09Session()
{
  this->_client_vc = nullptr;
}

const char *
Http09Session::get_protocol_string() const
{
  return IP_PROTO_TAG_HTTP_QUIC.data();
}

int
Http09Session::populate_protocol(std::string_view *result, int size) const
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
Http09Session::increment_current_active_client_connections_stat()
{
  // TODO Implement stats
}

void
Http09Session::decrement_current_active_client_connections_stat()
{
  // TODO Implement stats
}
