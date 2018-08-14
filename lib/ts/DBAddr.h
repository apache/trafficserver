#pragma once
#include "DBTable.h"
#include "Extendible.h"
#include "ts/ink_inet.h"

/**
 * @file
 *
 * @brief this file defines structures to store data about each ip.
 *
 * DBAddr - stores a concurrnet table of Extendible data indexed by sockaddr.
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
