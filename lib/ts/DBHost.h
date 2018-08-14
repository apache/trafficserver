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

/**
 * @file
 *
 * @brief this file defines structures to store data about each host.
 *
 * DBHost - stores a concurrnet table of Extendible data indexed by FQDN.
 *    @see DBTable - allows concurrent row access.
 *    @see Extendible - allows concurrent column access.
 */
class DBHost : public MT::Extendible<DBHost>
{
public:
  using KeyType   = std::string_view; ///< the FQDN of a host, used as the table's unique key.
  using TableType = DBTable<KeyType, DBHost>;

  static TableType table;

  // Add TSCore Variables below or use DBHost.schema.addField() to extend the structure dynamically.

protected:
  // Restrict lifetime management
  DBHost(){};
  DBHost(DBHost &) = delete;
  // Note, this uses Extendible::new and delete to manage allocations.
  // see obtain and destroy

  friend TableType;
};
