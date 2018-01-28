/** @file
 *
 *  QUICConnectionTable
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

#include "QUICTypes.h"
#include "QUICConnection.h"
#include "ts/Map.h"

class QUICFiveTuple
{
public:
  QUICFiveTuple(IpEndpoint src, IpEndpoint dst, int protocol)
  {
    // FIXME Generate a hash code
    this->_hash_code = src.port() + dst.port() + protocol;
  }

private:
  uint64_t _hash_code = 0;
};

class QUICConnectionTable
{
public:
  /*
   * Insert an entry
   *
   * Return 1 if it is the only connection or the first connection from the endpoint.
   */
  int insert(QUICConnectionId cid, QUICConnection *connection);

  /*
   * Remove an entry
   *
   * Fails if CID is not associated to a specified connection
   */
  void erase(QUICConnectionId cid, QUICConnection *connection);

  /*
   *  Lookup QUICConnection
   *
   *  If packet doesn't have CID, 5-tuple(endpoint) will be used to lookup CID.
   */
  QUICConnection *lookup(const uint8_t *packet, QUICFiveTuple endpoint);

private:
  // FIXME Use another map impl. that has good support for concurrent access
  Map<int64_t, QUICConnection *> _connections;
  // Map<QUICFiveTuple, QUICConnectionId> _cids;
};
