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
 *  IpLookup.cc - IpMatching table
 *
 *
 *
 ****************************************************************************/

#include <stdio.h>
#include "ink_platform.h"
#include "IpLookup.h"
#include "DynArray.h"
#include "ink_assert.h"
//#include "Diags.h"

#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

//
//  Begin IP Matcher Helper types
//
enum ip_info_types
{ IPN_INVALID, IPN_TREE_NODE,
  IPN_END_NODE
};

struct ip_match_el
{
  void *opaque_data;
  ip_addr_t range_start;
  ip_addr_t range_end;
};


// struct ip_table
//
//    A helper type to IpMatcher.  Acts to break
//      up ip address at 8 bit increments to
//      allow fast searching.  8 bit increments
//      are hard code into the implementation.
//    The result being something like a page
//      table with 256 entries at each level
struct ip_table
{
  ip_table();
  ~ip_table();
  ip_info_types type;
  void *next_level[256];
    DynArray<ip_match_el> spanning_entries;
};

ip_match_el default_ip_match_el = { NULL, 0xffffffff, 0xffffffff };

ip_table::ip_table():spanning_entries(&default_ip_match_el, 4)
{
  type = IPN_INVALID;
  memset(next_level, 0, sizeof(next_level));
}

ip_table::~ip_table()
{

  for (int i = 0; i < 256; i++) {
    switch (type) {
    case IPN_INVALID:
      // Impossible state
      ink_assert(0);
      break;
    case IPN_TREE_NODE:
      if (next_level[i] != NULL) {
        delete((ip_table *) next_level[i]);
      }
      break;
    case IPN_END_NODE:
      if (next_level[i] != NULL) {
        delete((DynArray<ip_match_el> *)next_level[i]);
      }
      break;
    }
  }
}

//
//  End IP Matcher Helper types
//

// IpLookup::IpLookup
//
IpLookup::IpLookup(const char *name, int depth):
match_els(NULL),
num_el(0),
table_name(name)
{
  // Ensure that we have a resonable table depth
  if (!(depth >= 2 && depth <= 3)) {
    table_depth = 2;
  } else {
    table_depth = depth;
  }

  // Allocate the first level
  ip_lookup = new ip_table;
  ip_lookup->type = IPN_TREE_NODE;
}

//
// IpLookup::~IpMatcher()
//
IpLookup::~IpLookup()
{
  delete ip_lookup;
}

//
// void IpLookup::NewEntry(ip_addr_t addr1,
//                          ip_addr_t addr2,
//                          void* opaque_data_in)
//
//
void
IpLookup::NewEntry(ip_addr_t addr1, ip_addr_t addr2, void *opaque_data_in)
{

  // Table insert vars
  ip_match_el new_el;
  ip_table *cur = ip_lookup;
  ip_table *tmp = NULL;
  ip_addr_t cur_mask = 0xff000000;
  int cur_shift_bits = 24;
  int cur_slot;

  DynArray<ip_match_el> *insert_bin = NULL;

  // Fill out the matching entry
  new_el.opaque_data = opaque_data_in;
  new_el.range_start = addr1;
  new_el.range_end = addr2;

  // Find where to insert the entry
  for (int i = 0; i < this->table_depth; i++) {
    // First thing is check to see if range
    //   spans table slots
    if ((addr1 & cur_mask) != (addr2 & cur_mask)) {
      // This is spanning entry
      insert_bin = &cur->spanning_entries;
      break;
    }
    // Figure out our slot
    cur_slot = ((addr1 & cur_mask) >> cur_shift_bits) & 0xff;

    // Check to see if we have to allocate the next
    //  level
    if (cur->next_level[cur_slot] == NULL) {
      if (i + 1 == this->table_depth) {
        // We are at the end of line and need
        //   a new dynamic array
        insert_bin = new DynArray<ip_match_el> (&default_ip_match_el, 8);
        cur->next_level[cur_slot] = insert_bin;
      } else {
        // Need a new table lookup level
        tmp = new ip_table();
        cur->next_level[cur_slot] = tmp;
        tmp->type = (i + 2 == this->table_depth) ? IPN_END_NODE : IPN_TREE_NODE;
        cur = tmp;
      }
    } else {
      // The next level is already allocated
      if (i + 1 == this->table_depth) {
        insert_bin = (DynArray<ip_match_el> *)cur->next_level[cur_slot];
      } else {
        cur = (ip_table *) cur->next_level[cur_slot];
      }
    }
    cur_shift_bits -= 8;
    cur_mask = cur_mask | (cur_mask >> 8);
  }

  // Insert the matching entry
  (*insert_bin) (insert_bin->length()) = new_el;

  num_el++;
}

//inline bool IpLookup::MatchArray(IpLookupState* s, void**opaque_ptr, void* array_in)
//
//   Note void* is used for array_in hide all typing from the header file
//     the type must be DynArray<ip_match_el>*
//
//   Loops through a DynArray and check for a Match
//
inline bool
IpLookup::MatchArray(IpLookupState * s, void **opaque_ptr, void *array_in)
{
  DynArray<ip_match_el> *array = (DynArray<ip_match_el> *)array_in;
  ip_match_el *cur;
  /* struct in_addr debug;  */
  intptr_t j;

  for (j = s->array_slot + 1; j < array->length(); j++) {
    cur = &((*array)[j]);
    ink_assert(cur != NULL);
    if (s->addr >= cur->range_start && s->addr <= cur->range_end) {
      // We have a match
      /*
         if(is_debug_tag_set("ip-lookup")) {
         debug.s_addr = s->addr;
         Debug("ip-lookup", "%s Matched %s with ip range",
         table_name, inet_ntoa(debug));
         }
       */
      *opaque_ptr = cur->opaque_data;

      s->array_slot = j;
      return true;
    }
  }

  s->array_slot = j;
  return false;
}


// bool IpLookup::Match(ip_addr_t addr)
//
//    Wrapper for just finding one match in the table
//
bool
IpLookup::Match(ip_addr_t addr)
{
  IpLookupState tmp;
  void *unused;

  if (num_el == 0) {
    return false;
  } else {
    return this->MatchFirst(addr, &tmp, &unused);
  }
}

// bool IpLookup::Match(ip_addr_t addr, void** opaque_ptr)
//
//    Wrapper for just finding one match in the table
//
bool
IpLookup::Match(ip_addr_t addr, void **opaque_ptr)
{
  IpLookupState tmp;

  if (num_el == 0) {
    return false;
  } else {
    return this->MatchFirst(addr, &tmp, opaque_ptr);
  }
}


// bool IpLookup::MatchFirst(ip_addr_t addr, IpLookupState* s, void** opaque_ptr)
//
//   Function used to start a search for every match in the table
//
bool
IpLookup::MatchFirst(ip_addr_t addr, IpLookupState * s, void **opaque_ptr)
{
  s->cur = ip_lookup;
  s->table_level = 0;
  s->search_span = true;
  s->array_slot = -1;
  s->addr = htonl(addr);

  return MatchNext(s, opaque_ptr);
}

// bool IpLookup::MatchNext(void** opaque_ptr, IpLookupState* s)
//
//   Function used to continue a search for every match in the
//    table
//
bool
IpLookup::MatchNext(IpLookupState * s, void **opaque_ptr)
{
  ip_table *cur = s->cur;
  int table_level = s->table_level;
  DynArray<ip_match_el> *search_bin = NULL;
  ip_addr_t cur_mask = 0xff000000;
  int cur_shift_bits = 24;
  int cur_slot;

  // Check to see there is a table to search
  if (cur == NULL) {
    return false;
  }
  // We must adjust the cur_mask and cur_shift_bits based 
  //  on the current table level otherwise we get garbage r
  //  results for subsequent calls to nextMatch (INKqa04167)
  for (int q = 0; q < s->table_level; q++) {
    cur_shift_bits -= 8;
    cur_mask = cur_mask | (cur_mask >> 8);
  }

  //
  // Loop through the table levels looking for the right
  //    slot, searching the spanning entries along the way
  //
  while (table_level < this->table_depth) {

    // Check to see if need to look at the spannning entries for this
    //   level
    if (s->search_span == true) {
      if (this->MatchArray(s, opaque_ptr, &cur->spanning_entries) == true) {
        return true;
      } else {
        s->array_slot = -1;
        s->search_span = false;
      }
    }

    cur_slot = ((s->addr & cur_mask) >> cur_shift_bits) & 0xff;

    if (s->table_level + 1 == this->table_depth) {
      ink_assert(cur->type == IPN_END_NODE);
      search_bin = (DynArray<ip_match_el> *)cur->next_level[cur_slot];

      if (search_bin != NULL) {
        if (this->MatchArray(s, opaque_ptr, search_bin) == true) {
          return true;
        }
      }

      table_level++;
      s->table_level = table_level;

    } else {
      ink_assert(cur->type == IPN_TREE_NODE);

      // Update our local copy of the search state
      cur = (ip_table *) cur->next_level[cur_slot];
      table_level++;

      // Update our store copy of the search state
      s->cur = cur;
      s->table_level = table_level;
      s->search_span = true;

      // If there are no entries father down in the table
      //   bail
      if (cur == NULL) {
        break;
      }
    }

    cur_shift_bits -= 8;
    cur_mask = cur_mask | (cur_mask >> 8);

  }

  return false;
}

// void IpLookup::PrintArray(void* array_in, IpLookupPrintFunc f) 
//
//   Note void* is used for array_in hide all typing from the header file
//     the type must be DynArray<ip_match_el>*
//
//   Loops through a DynArray and calls the print function on each element
//
void
IpLookup::PrintArray(void *array_in, IpLookupPrintFunc f)
{
  DynArray<ip_match_el> *array = (DynArray<ip_match_el> *)array_in;
  ip_match_el *cur;
  struct in_addr rangeStart;
  struct in_addr rangeEnd;

  for (intptr_t j = 0; j < array->length(); j++) {
    cur = &((*array)[j]);
    ink_assert(cur != NULL);
    rangeStart.s_addr = htonl(cur->range_start);
    rangeEnd.s_addr = htonl(cur->range_end);
    printf("\t\tRange start: %s ", inet_ntoa(rangeStart));
    printf("Range End %s\n", inet_ntoa(rangeEnd));
    if (f != NULL) {
      f(cur->opaque_data);
    }
  }
}

// void IpLookup::PrintIpNode(ip_table* t, IpLookupPrintFunc f)
//
//    Recurinve prints out an IpNode
//
void
IpLookup::PrintIpNode(ip_table * t, IpLookupPrintFunc f)
{
  for (int i = 0; i < 256; i++) {
    if (t->next_level[i] != NULL) {
      switch (t->type) {
      case IPN_TREE_NODE:
        PrintIpNode((ip_table *) t->next_level[i], f);
        break;
      case IPN_END_NODE:
        PrintArray(t->next_level[i], f);
        break;
      case IPN_INVALID:
        printf("\t\tBad Tree Node at %p\n", t);
        break;
      }
    }
  }
  PrintArray(&t->spanning_entries, f);
}

// void IpLookup::Print(IpLookupPrintFunc f)
//
//   Top level print function
//
void
IpLookup::Print(IpLookupPrintFunc f)
{
  PrintIpNode(ip_lookup, f);
}

// void IpLookup::Print(IpLookupPrintFunc f)
//
//   Top level print function
//
void
IpLookup::Print()
{
  PrintIpNode(ip_lookup, NULL);
}
