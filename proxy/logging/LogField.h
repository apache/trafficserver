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

#pragma once

#include "ts/ink_platform.h"
#include "ts/List.h"
#include <ts/TsBuffer.h>
#include "LogFieldAliasMap.h"
#include "Milestones.h"

class LogAccess;

struct LogSlice {
  bool m_enable;
  int m_start;
  int m_end;

  LogSlice()
  {
    m_enable = false;
    m_start  = 0;
    m_end    = INT_MAX;
  }

  //
  // Initialize LogSlice by slice notation,
  // the str looks like: "xxx[0:30]".
  //
  LogSlice(char *str);

  //
  // Convert slice notation to target string's offset,
  // return the available length belongs to this slice.
  //
  // Use the offset and return value, we can locate the
  // string content indicated by this slice.
  //
  int toStrOffset(int strlen, int *offset);
};

/*-------------------------------------------------------------------------
  LogField

  This class represents a field that can be logged.  To construct a new
  field, we need to know its name, its symbol, its datatype, and the
  pointer to its LogAccess marshalling routine.  Example:

      LogField ("client_host_ip", "chi", LogField::INT,
                &LogAccess::marshal_client_host_ip);
  -------------------------------------------------------------------------*/

class LogField
{
public:
  typedef int (LogAccess::*MarshalFunc)(char *buf);
  typedef int (*UnmarshalFunc)(char **buf, char *dest, int len);
  typedef int (*UnmarshalFuncWithSlice)(char **buf, char *dest, int len, LogSlice *slice);
  typedef int (*UnmarshalFuncWithMap)(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);
  typedef void (LogAccess::*SetFunc)(char *buf, int len);

  enum Type {
    sINT = 0,
    dINT,
    STRING,
    IP, ///< IP Address.
    N_TYPES
  };

  enum Container {
    NO_CONTAINER = 0,
    CQH,
    PSH,
    PQH,
    SSH,
    CSSH,
    ECQH,
    EPSH,
    EPQH,
    ESSH,
    ECSSH,
    ICFG,
    SCFG,
    RECORD,
    MS,
    MSDMS,
    N_CONTAINERS,
  };

  enum Aggregate {
    NO_AGGREGATE = 0,
    eCOUNT,
    eSUM,
    eAVG,
    eFIRST,
    eLAST,
    N_AGGREGATES,
  };

  LogField(const char *name, const char *symbol, Type type, MarshalFunc marshal, UnmarshalFunc unmarshal,
           SetFunc _setFunc = nullptr);

  LogField(const char *name, const char *symbol, Type type, MarshalFunc marshal, UnmarshalFuncWithMap unmarshal,
           const Ptr<LogFieldAliasMap> &map, SetFunc _setFunc = nullptr);

  LogField(const char *field, Container container, SetFunc _setFunc = nullptr);
  LogField(const LogField &rhs);
  ~LogField();

  unsigned marshal_len(LogAccess *lad);
  unsigned marshal(LogAccess *lad, char *buf);
  unsigned marshal_agg(char *buf);
  unsigned unmarshal(char **buf, char *dest, int len);
  void display(FILE *fd = stdout);
  bool operator==(LogField &rhs);
  void updateField(LogAccess *lad, char *val, int len);

  const char *
  name() const
  {
    return m_name;
  }

  const char *
  symbol() const
  {
    return m_symbol;
  }

  Type
  type() const
  {
    return m_type;
  }

  Ptr<LogFieldAliasMap>
  map()
  {
    return m_alias_map;
  }

  Aggregate
  aggregate() const
  {
    return m_agg_op;
  }

  bool
  is_time_field() const
  {
    return m_time_field;
  }

  void set_aggregate_op(Aggregate agg_op);
  void update_aggregate(int64_t val);

  static void init_milestone_container(void);
  static Container valid_container_name(char *name);
  static Aggregate valid_aggregate_name(char *name);
  static bool fieldlist_contains_aggregates(const char *fieldlist);

private:
  char *m_name;
  char *m_symbol;
  Type m_type;
  Container m_container;
  MarshalFunc m_marshal_func;     // place data into buffer
  UnmarshalFunc m_unmarshal_func; // create a string of the data
  UnmarshalFuncWithMap m_unmarshal_func_map;
  Aggregate m_agg_op;
  int64_t m_agg_cnt;
  int64_t m_agg_val;
  TSMilestonesType m_milestone1; ///< Used for MS and MSDMS as the first (or only) milestone.
  TSMilestonesType m_milestone2; ///< Second milestone for MSDMS
  bool m_time_field;
  Ptr<LogFieldAliasMap> m_alias_map; // map sINT <--> string
  SetFunc m_set_func;
  TSMilestonesType milestone_from_m_name();
  int milestones_from_m_name(TSMilestonesType *m1, TSMilestonesType *m2);

public:
  LINK(LogField, link);
  LogSlice m_slice;

  // noncopyable
  // -- member functions that are not allowed --
  LogField &operator=(const LogField &rhs) = delete;

private:
  // luis, check where this is used and what it does
  //    void init (char *name, char *symbol, Type type);
  LogField();
};

/*-------------------------------------------------------------------------
  LogFieldList

  This class maintains a list of LogField objects (tah-dah).
  -------------------------------------------------------------------------*/

class LogFieldList
{
public:
  LogFieldList();
  ~LogFieldList();

  void clear();
  void add(LogField *field, bool copy = true);
  LogField *find_by_name(const char *name) const;
  LogField *find_by_symbol(const char *symbol) const;
  unsigned marshal_len(LogAccess *lad);
  unsigned marshal(LogAccess *lad, char *buf);
  unsigned marshal_agg(char *buf);

  LogField *
  first() const
  {
    return m_field_list.head;
  }
  LogField *
  next(LogField *here) const
  {
    return (here->link).next;
  }
  unsigned count();
  void display(FILE *fd = stdout);

  // noncopyable
  // -- member functions that are not allowed --
  LogFieldList(const LogFieldList &rhs) = delete;
  LogFieldList &operator=(const LogFieldList &rhs) = delete;

private:
  unsigned m_marshal_len;
  Queue<LogField> m_field_list;
};

/** Base IP address data.
    To unpack an IP address, the generic memory is first cast to
    this type to get the family. That pointer can then be static_cast
    to the appropriate subtype to get the actual address data.

    @note We don't use our own enum for the family. Instead we use
    @c AF_INET and @c AF_INET6.
*/
struct LogFieldIp {
  uint16_t _family; ///< IP address family.
};
/// IPv4 address as log field.
struct LogFieldIp4 : public LogFieldIp {
  in_addr_t _addr; ///< IPv4 address.
};
/// IPv6 address as log field.
struct LogFieldIp6 : public LogFieldIp {
  in6_addr _addr; ///< IPv6 address.
};
/// Something big enough to hold any of the IP field types.
union LogFieldIpStorage {
  LogFieldIp _ip;
  LogFieldIp4 _ip4;
  LogFieldIp6 _ip6;
};
