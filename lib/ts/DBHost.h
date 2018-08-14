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
