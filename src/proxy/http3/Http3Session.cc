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

#include "proxy/http3/Http3Session.h"
#include "iocore/net/QUICSupport.h"

#include "proxy/http3/Http3.h"
#include "proxy/http3/Http3Types.h"

//
// HQSession
//
HQSession::HQSession(NetVConnection *vc) : ProxySession(vc)
{
  auto app_name = vc->get_service<QUICSupport>()->get_quic_connection()->negotiated_application_name();
  memcpy(this->_protocol_string, app_name.data(), std::min(app_name.length(), sizeof(this->_protocol_string)));
  this->_protocol_string[app_name.length()] = '\0';
}

HQSession::~HQSession()
{
  // Transactions should be deleted first before HQSesson gets deleted.
  ink_assert(this->_transaction_list.head == nullptr);
}

void
HQSession::add_transaction(HQTransaction *trans)
{
  this->_transaction_list.enqueue(trans);

  return;
}

void
HQSession::remove_transaction(HQTransaction *trans)
{
  this->_transaction_list.remove(trans);

  return;
}

const char *
HQSession::get_protocol_string() const
{
  return "";
}

int
HQSession::populate_protocol(std::string_view *result, int size) const
{
  int retval = 0;
  if (size > retval) {
    result[retval++] = this->_vc->get_service<QUICSupport>()->get_quic_connection()->negotiated_application_name();
    if (size > retval) {
      retval += super::populate_protocol(result + retval, size - retval);
    }
  }
  return retval;
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

void
HQSession::do_io_close(int /* lerrno ATS_UNUSED */)
{
  // TODO
  return;
}

void
HQSession::do_io_shutdown(ShutdownHowTo_t /* howto ATS_UNUSED */)
{
  ink_assert(false);
  return;
}

void
HQSession::reenable(VIO * /* vio ATS_UNUSED */)
{
  ink_assert(false);
  return;
}

void
HQSession::new_connection(NetVConnection *new_vc, MIOBuffer * /* iobuf ATS_UNUSED */, IOBufferReader * /* reader ATS_UNUSED */)
{
  this->con_id = new_vc->get_service<QUICSupport>()->get_quic_connection()->connection_id();
  this->_handle_if_ssl(new_vc);

  do_api_callout(TS_HTTP_SSN_START_HOOK);
}

void
HQSession::start()
{
  SET_HANDLER(&HQSession::main_event_handler);
  this->do_io_read(this, 0, nullptr);
  this->do_io_write(this, 0, nullptr);
}

void
HQSession::destroy()
{
  ink_assert(false);
  return;
}

void
HQSession::free()
{
  delete this;
}

void
HQSession::release(ProxyTransaction * /* trans ATS_UNUSED */)
{
  return;
}

int
HQSession::get_transact_count() const
{
  return 0;
}

int
HQSession::main_event_handler(int event, void *edata)
{
  switch (event) {
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    this->do_io_close();
    for (HQTransaction *t = this->_transaction_list.head; t; t = static_cast<HQTransaction *>(t->link.next)) {
      SCOPED_MUTEX_LOCK(lock, t->mutex, this_ethread());
      t->handleEvent(event, edata);
    }
    break;
  }

  return 0;
}

//
// Http3Session
//
Http3Session::Http3Session(NetVConnection *vc) : HQSession(vc)
{
  QUICConnection *qc = vc->get_service<QUICSupport>()->get_quic_connection();
  this->_local_qpack =
    new QPACK(qc, HTTP3_DEFAULT_MAX_FIELD_SECTION_SIZE, HTTP3_DEFAULT_HEADER_TABLE_SIZE, HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS);
  this->_remote_qpack =
    new QPACK(qc, HTTP3_DEFAULT_MAX_FIELD_SECTION_SIZE, HTTP3_DEFAULT_HEADER_TABLE_SIZE, HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS);
}

Http3Session::~Http3Session()
{
  this->_vc = nullptr;
  delete this->_local_qpack;
  delete this->_remote_qpack;
}

HTTPVersion
Http3Session::get_version(HTTPHdr & /* hdr ATS_UNUSED */) const
{
  return HTTP_3_0;
}

void
Http3Session::increment_current_active_connections_stat()
{
  // TODO Implement stats
}

void
Http3Session::decrement_current_active_connections_stat()
{
  // TODO Implement stats
}

const char *
Http3Session::get_protocol_string() const
{
  return "http/3";
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

Http3FrameCounter *
Http3Session::get_received_frame_counter()
{
  return &this->_received_frame_counter;
}

bool
Http3Session::is_protocol_framed() const
{
  return true;
}
uint64_t
Http3Session::get_received_frame_count(uint64_t type) const
{
  return this->_received_frame_counter.get_count(type);
}

//
// Http09Session
//
Http09Session::~Http09Session()
{
  this->_vc = nullptr;
}

HTTPVersion
Http09Session::get_version(HTTPHdr & /* hdr ATS_UNUSED */) const
{
  return HTTP_0_9;
}

void
Http09Session::increment_current_active_connections_stat()
{
  // TODO Implement stats
}

void
Http09Session::decrement_current_active_connections_stat()
{
  // TODO Implement stats
}
