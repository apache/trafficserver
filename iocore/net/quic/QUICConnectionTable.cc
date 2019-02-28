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

QUICConnectionTable::~QUICConnectionTable()
{
  // TODO: clear all values.
}

QUICConnection *
QUICConnectionTable::insert(QUICConnectionId cid, QUICConnection *connection)
{
  Ptr<ProxyMutex> m = _connections.lock_for_key(cid);
  SCOPED_MUTEX_LOCK(lock, m, this_ethread());
  // To check whether the return value is nullptr by caller in case memory leak.
  // The return value isn't nullptr, the new value will take up the slot and return old value.
  return _connections.insert_entry(cid, connection);
}

void
QUICConnectionTable::erase(QUICConnectionId cid, QUICConnection *connection)
{
  Ptr<ProxyMutex> m = _connections.lock_for_key(cid);
  SCOPED_MUTEX_LOCK(lock, m, this_ethread());
  QUICConnection *ret_connection = _connections.remove_entry(cid);
  if (ret_connection) {
    ink_assert(ret_connection == connection);
  }
}

QUICConnection *
QUICConnectionTable::erase(QUICConnectionId cid)
{
  Ptr<ProxyMutex> m = _connections.lock_for_key(cid);
  SCOPED_MUTEX_LOCK(lock, m, this_ethread());
  return _connections.remove_entry(cid);
}

QUICConnection *
QUICConnectionTable::lookup(QUICConnectionId cid)
{
  Ptr<ProxyMutex> m = _connections.lock_for_key(cid);
  SCOPED_MUTEX_LOCK(lock, m, this_ethread());
  return _connections.lookup_entry(cid);
}
