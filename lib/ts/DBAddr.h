/** @file

  @brief this file defines structures to store data about each ip.

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

#pragma once
#include "DBTable.h"
#include "Extendible.h"
#include "ts/ink_inet.h"

/** DBAddr - stores a concurrnet table of Extendible data indexed by sockaddr.
 *    @see DBTable - allows concurrent row access.
 *    @see Extendible - allows concurrent column access.
 *
 * Extend by calling DBAddr::schema.addField()
 */
class DBAddr : public MT::Extendible<DBAddr>
{
public:
  using KeyType   = socksaddr; ///< an Ip address of a host (1 of many)
  using TableType = DBTable<KeyType, DBAddr, CustomHasher<socksaddr, ats_ip_hash>>;

  static TableType table;

  // restrict lifetime management
protected:
  // Note, this uses Extendible::new and delete to manage allocations.
  DBAddr();
  DBAddr(DBAddr &) = delete;

  // thread safe map: addr -> Extendible
  friend TableType; // allow the map to allocate
};

/// Concurrent map: addr+port -> Extendible
class DBAddrPort : public MT::Extendible<DBAddrPort>
{
public:
  using KeyType   = socksaddr; ///< an Ip:Port address of a host (1 of many)
  using TableType = DBTable<KeyType, DBAddr, CustomHasher<socksaddr, ats_ip_port_hash>>> ;

  static TableType table;

  // restrict lifetime management
protected:
  // Note, this uses Extendible::new and delete to manage allocations.
  DBAddrPort();
  DBAddrPort(DBAddr &) = delete;

  // thread safe map: addr -> Extendible
  friend TableType; // allow the map to allocate
};
