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

#pragma once

#include <inttypes.h>
#include "QUICFrameGenerator.h"
#include "QUICTypes.h"
#include "QUICConnection.h"

class QUICConnectionTable;

class QUICAltConnectionManager : public QUICFrameGenerator
{
public:
  QUICAltConnectionManager(QUICConnection *qc, QUICConnectionTable &ctable);
  ~QUICAltConnectionManager();
  bool migrate_to(QUICConnectionId cid, QUICStatelessResetToken &new_reset_token);
  void invalidate_alt_connections();

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level);
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size);

private:
  class AltConnectionInfo
  {
  public:
    int seq_num;
    QUICConnectionId id;
    QUICStatelessResetToken token;
    bool advertised;
  };

  QUICConnection *_qc = nullptr;
  QUICConnectionTable &_ctable;
  AltConnectionInfo *_alt_quic_connection_ids;
  uint8_t _nids                          = 0;
  int8_t _alt_quic_connection_id_seq_num = 0;
  bool _need_advertise                   = false;

  void _update_alt_connection_ids(int8_t chosen = -1);
};
