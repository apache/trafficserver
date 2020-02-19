/** @file

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

#include "tscore/Diags.h"
#include "QUICResetTokenTable.h"
#include "QUICConnection.h"

void
QUICResetTokenTable::insert(const QUICStatelessResetToken token, QUICConnection *connection)
{
  Debug("quic_reset_token_table", "Token:%02x%02x%02x%02x... CID:%08" PRIx32 "...", token.buf()[0], token.buf()[1], token.buf()[2],
        token.buf()[3], connection->connection_id().h32());
  this->_map.emplace(token, connection);
}

QUICConnection *
QUICResetTokenTable::lookup(QUICStatelessResetToken token)
{
  Debug("quic_reset_token_table", "Token:%02x%02x%02x%02x...", token.buf()[0], token.buf()[1], token.buf()[2], token.buf()[3]);
  auto result = this->_map.find(token);
  if (result != this->_map.end()) {
    Debug("quic_reset_token_table", "CID:%08" PRIx32 "...", result->second->connection_id().h32());
    return result->second;
  } else {
    Debug("quic_reset_token_table", "not fouund");
    return nullptr;
  }
}

void
QUICResetTokenTable::erase(const QUICStatelessResetToken token)
{
  Debug("quic_reset_token_table", "Token:%02x%02x%02x%02x...", token.buf()[0], token.buf()[1], token.buf()[2], token.buf()[3]);
  this->_map.erase(token);
}
