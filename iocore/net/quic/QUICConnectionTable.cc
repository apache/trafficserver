/** @file

  QUICConnectionTable

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

#include "QUICConnectionTable.h"

int
QUICConnectionTable::insert(QUICConnectionId cid, QUICConnection *connection)
{
  this->_connections.put(cid, connection);
  // if (this->_cids.get(connection->endpoint()) == nullptr) {
  //     this->_cids.put(connection->endpoint(), cid);
  // }
  return 0;
}

void
QUICConnectionTable::erase(QUICConnectionId cid, QUICConnection *connection)
{
  QUICConnection *qc = this->_connections.get(cid);
  if (qc == nullptr) {
    return;
  }
  ink_assert(qc == connection);
  Debug("quic_ctable", "ctable erase cid: [%" PRIx64 "] ", static_cast<uint64_t>(cid));
  // if (this->_cids.get(connection->endpoint(), connection->connection_id()) == cid) {
  //   this->_cids.put(connection->endpoint(), nullptr);
  // }
  this->_connections.put(cid, nullptr);
}

QUICConnection *
QUICConnectionTable::lookup(const uint8_t *packet, QUICFiveTuple endpoint)
{
  QUICConnectionId cid;
  if (QUICTypeUtil::has_connection_id(packet)) {
    cid = QUICPacket::connection_id(packet);
  } else {
    // TODO: find cid with five tuples
    // cid = this->_cids.get(endpoint);
    ink_assert(false);
  }
  return this->_connections.get(cid);
}
