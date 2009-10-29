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

/*****************************************************************************
 *
 *  IpLookup.h - Interface to IpMatching Table
 *
 *
 *
 ****************************************************************************/

#ifndef _IP_LOOKUP_H_
#define _IP_LOOKUP_H_

#include "ink_bool.h"

typedef unsigned long ip_addr_t;

// Forward declarations for types used within IpLookup
struct ip_table;
struct ip_match_el;

struct IpLookupState
{
  ip_table *cur;
  int table_level;
  bool search_span;
  int array_slot;
  ip_addr_t addr;
};

typedef void (*IpLookupPrintFunc) (void *opaque_data);

//  IP Table Depth
//    1 - Corresponds to all ranges within a class A network being
//             lumped together
//    2 - Corresponds to all ranges with a class B network ...
//    3 - Corresponds to all ranges with a class C network ...
//

class IpLookup
{
public:
  IpLookup(const char *name, int depth = 2);
   ~IpLookup();
  void NewEntry(ip_addr_t addr1, ip_addr_t addr2, void *opaque_data_in);
  bool Match(ip_addr_t addr);
  bool Match(ip_addr_t addr, void **opaque_ptr);
  bool MatchFirst(ip_addr_t addr, IpLookupState * s, void **opaque_ptr);
  bool MatchNext(IpLookupState * s, void **opaque_ptr);
  void Print(IpLookupPrintFunc f);
  void Print();
protected:
    bool MatchArray(IpLookupState * s, void **opaque_ptr, void *array_in);
  void PrintIpNode(ip_table * t, IpLookupPrintFunc f);
  void PrintArray(void *array_in, IpLookupPrintFunc f);
  ip_table *ip_lookup;          // the root of the ip lookiup tble
  ip_match_el *match_els;       // array of match elements in the table
  //int array_len;          // size of the arrays - its not being used
  int num_el;                   // number of elements in the table
  int table_depth;              // number of levels in the table 
  const char *table_name;       // a string identifing the table

};

#endif
