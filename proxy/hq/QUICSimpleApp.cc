/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "QUICSimpleApp.h"

#include "P_Net.h"
#include "P_VConnection.h"
#include "QUICDebugNames.h"

#include "HQClientSession.h"
#include "HQClientTransaction.h"
#include "../IPAllow.h"

static constexpr char tag[] = "quic_simple_app";

QUICSimpleApp::QUICSimpleApp(QUICNetVConnection *client_vc) : QUICApplication(client_vc)
{
  sockaddr const *client_ip           = client_vc->get_remote_addr();
  const AclRecord *session_acl_record = SessionAccept::testIpAllowPolicy(client_ip);

  this->_client_session             = new HQClientSession(client_vc);
  this->_client_session->acl_record = session_acl_record;
  this->_client_session->new_connection(client_vc, nullptr, nullptr, false);

  this->_client_qc->stream_manager()->set_default_application(this);

  SET_HANDLER(&QUICSimpleApp::main_event_handler);
}

QUICSimpleApp::~QUICSimpleApp()
{
  delete this->_client_session;
}

int
QUICSimpleApp::main_event_handler(int event, Event *data)
{
  Debug(tag, "%s (%d)", get_vc_event_name(event), event);

  VIO *vio                = reinterpret_cast<VIO *>(data);
  QUICStreamIO *stream_io = this->_find_stream_io(vio);

  if (stream_io == nullptr) {
    Debug(tag, "Unknown Stream");
    return -1;
  }

  QUICStreamId stream_id   = stream_io->get_transaction_id();
  HQClientTransaction *txn = this->_client_session->get_transaction(stream_id);

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    if (stream_io->is_read_avail_more_than(0)) {
      if (txn == nullptr) {
        txn = new HQClientTransaction(this->_client_session, stream_io);
        SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());

        txn->new_transaction();
      } else {
        txn->handleEvent(event);
      }
      break;
    }
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    if (txn != nullptr) {
      txn->handleEvent(event);
    }

    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    ink_assert(false);
    break;
  }
  default:
    break;
  }

  return EVENT_CONT;
}
