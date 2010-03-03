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



#ifndef LOG_FIELD_H
#define LOG_FIELD_H

#include "inktomi++.h"
#include "LogFieldAliasMap.h"

class LogAccess;

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
  typedef int (LogAccess::*MarshalFunc) (char *buf);
  typedef int (*UnmarshalFunc) (char **buf, char *dest, int len);
  typedef int (*UnmarshalFuncWithMap) (char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);


  enum Type
  {
    sINT = 0,
    dINT,
    STRING,
    N_TYPES
  };

  enum Container
  {
    NO_CONTAINER = 0,
    CQH,
    PSH,
    PQH,
    SSH,
    ECQH,
    EPSH,
    EPQH,
    ESSH,
    ICFG,
    SCFG,
    RECORD,
    N_CONTAINERS
  };

  enum Aggregate
  {
    NO_AGGREGATE = 0,
    eCOUNT,
    eSUM,
    eAVG,
    eFIRST,
    eLAST,
    N_AGGREGATES
  };

    LogField(char *name, char *symbol, Type type, MarshalFunc marshal, UnmarshalFunc unmarshal);

    LogField(char *name, char *symbol, Type type,
             MarshalFunc marshal, UnmarshalFuncWithMap unmarshal, Ptr<LogFieldAliasMap> map);

    LogField(char *field, Container container);
    LogField(const LogField & rhs);
   ~LogField();

  unsigned marshal_len(LogAccess * lad);
  unsigned marshal(LogAccess * lad, char *buf);
  unsigned marshal_agg(char *buf);
  unsigned unmarshal(char **buf, char *dest, int len);
  void display(FILE * fd = stdout);

  char *name()
  {
    return m_name;
  }
  char *symbol()
  {
    return m_symbol;
  }
  Type type()
  {
    return m_type;
  }
  Ptr<LogFieldAliasMap> map() {
    return m_alias_map;
  };
  Aggregate aggregate()
  {
    return m_agg_op;
  }
  bool is_time_field()
  {
    return m_time_field;
  }

  void set_aggregate_op(Aggregate agg_op);
  void update_aggregate(unsigned val);  // SAME AS LOG_INT

  static Container valid_container_name(char *name);
  static Aggregate valid_aggregate_name(char *name);
  static bool fieldlist_contains_aggregates(char *fieldlist);

private:
  char *m_name;
  char *m_symbol;
  Type m_type;
  Container m_container;
  MarshalFunc m_marshal_func;   // place data into buffer
  UnmarshalFunc m_unmarshal_func;       // create a string of the data
  UnmarshalFuncWithMap m_unmarshal_func_map;
  Aggregate m_agg_op;
  unsigned m_agg_cnt;           // SAME AS LOG_INT
  unsigned m_agg_val;           // SAME AS LOG_INT
  bool m_time_field;
  Ptr<LogFieldAliasMap> m_alias_map; // map sINT <--> string

public:
  LINK(LogField, link);

private:
// luis, check where this is used and what it does
//    void init (char *name, char *symbol, Type type);

  // -- member functions that are not allowed --
  LogField();
  LogField & operator=(const LogField & rhs);
};

extern char *container_names[];
extern char *aggregate_names[];

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
  void add(LogField * field, bool copy = true);
  LogField *find_by_name(const char *name) const;
  LogField *find_by_symbol(const char *symbol) const;
  unsigned marshal_len(LogAccess * lad);
  unsigned marshal(LogAccess * lad, char *buf);
  unsigned marshal_agg(char *buf);

  LogField *first() const
  {
    return m_field_list.head;
  }
  LogField *next(LogField * here) const
  {
    return (here->link).next;
  }
  unsigned count();
  void display(FILE * fd = stdout);

private:
  unsigned m_marshal_len;
  Queue<LogField> m_field_list;

  // -- member functions that are not allowed --
  LogFieldList(const LogFieldList & rhs);
  LogFieldList & operator=(const LogFieldList & rhs);
};

#endif
