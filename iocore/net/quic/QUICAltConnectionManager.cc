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

#include "QUICAltConnectionManager.h"
#include "QUICConnectionTable.h"
#include "QUICConfig.h"

QUICAltConnectionManager::QUICAltConnectionManager(QUICConnection *qc, QUICConnectionTable &ctable) : _qc(qc), _ctable(ctable)
{
  QUICConfig::scoped_config params;

  this->_nids                    = params->max_alt_connection_ids();
  this->_alt_quic_connection_ids = static_cast<AltConnectionInfo *>(ats_malloc(sizeof(AltConnectionInfo) * this->_nids));
  this->_update_alt_connection_ids(-1);
}

QUICAltConnectionManager::~QUICAltConnectionManager()
{
  ats_free(this->_alt_quic_connection_ids);
}

void
QUICAltConnectionManager::_update_alt_connection_ids(int8_t chosen)
{
  if (this->_nids == 0) {
    // Connection migration is disabled
    return;
  }

  if (chosen == -1) {
    chosen = (this->_nids - 1) & 0xff;
  }

  QUICConfig::scoped_config params;
  int n       = this->_nids;
  int current = this->_alt_quic_connection_id_seq_num % n;
  int delta   = chosen - current;
  int count   = (n + delta) % n + 1;

  for (int i = 0; i < count; ++i) {
    int index = (current + i) % n;
    QUICConnectionId conn_id;
    QUICStatelessResetToken token;

    conn_id.randomize();
    token.generate(conn_id, params->server_id());
    this->_alt_quic_connection_ids[index] = {this->_alt_quic_connection_id_seq_num + i, conn_id, token, false};
    this->_ctable.insert(conn_id, this->_qc);
  }
  this->_alt_quic_connection_id_seq_num += count;
  this->_need_advertise = true;
}

bool
QUICAltConnectionManager::migrate_to(QUICConnectionId cid, QUICStatelessResetToken &new_reset_token)
{
  for (unsigned int i = 0; i < this->_nids; ++i) {
    AltConnectionInfo &info = this->_alt_quic_connection_ids[i];
    if (info.id == cid) {
      // Migrate connection
      // TODO Unregister the old connection id (Should we wait for a while?)
      new_reset_token = info.token;
      this->_update_alt_connection_ids(i);
      return true;
    }
  }
  return false;
}

void
QUICAltConnectionManager::invalidate_alt_connections()
{
  for (unsigned int i = 0; i < this->_nids; ++i) {
    this->_ctable.erase(this->_alt_quic_connection_ids[i].id, this->_qc);
  }
}

bool
QUICAltConnectionManager::will_generate_frame()
{
  return this->_need_advertise;
}

QUICFrameUPtr
QUICAltConnectionManager::generate_frame(uint64_t connection_credit, uint16_t maximum_frame_size)
{
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  int count           = this->_nids;
  for (int i = 0; i < count; ++i) {
    if (!this->_alt_quic_connection_ids[i].advertised) {
      this->_alt_quic_connection_ids[i].advertised = true;
      return QUICFrameFactory::create_new_connection_id_frame(
        this->_alt_quic_connection_ids[i].seq_num, this->_alt_quic_connection_ids[i].id, this->_alt_quic_connection_ids[i].token);
    }
  }
  this->_need_advertise = false;
  return frame;
}
