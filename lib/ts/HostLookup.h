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
 *  HostLookup.h - Interface to genernal purpose matcher
 *
 *
 ****************************************************************************/

#ifndef _HOST_LOOKUP_H_
#define _HOST_LOOKUP_H_
// HostLookup  constantss
const int HOST_TABLE_DEPTH = 3; // Controls the max number of levels in the logical tree
const int HOST_ARRAY_MAX   = 8; // Sets the fixed array size

//
//  Begin Host Lookup Helper types
//
enum HostNodeType {
  HOST_TERMINAL,
  HOST_HASH,
  HOST_INDEX,
  HOST_ARRAY,
};
enum LeafType {
  LEAF_INVALID,
  HOST_PARTIAL,
  HOST_COMPLETE,
  DOMAIN_COMPLETE,
  DOMAIN_PARTIAL,
};

// The data in the HostMatcher tree is pointers to HostBranches.
//   No duplicates keys permitted in the tree.  To handle multiple
//   data items bound the same key, the HostBranch has the lead_indexs
//   array which stores pointers (in the form of array indexes) to
//   HostLeaf structs
//
// There is HostLeaf struct for each data item put into the
//   table
//
struct HostLeaf {
  LeafType type;
  char *match;       // Contains a copy of the match data
  int len;           // length of the data
  bool isNot;        // used by any fasssst path ...
  void *opaque_data; // Data associated with this leaf
};

struct HostBranch {
  HostBranch();
  ~HostBranch();
  int level;                 // what level in the tree.  the root is level 0
  HostNodeType type;         // tells what kind of data structure is next_level is
  void *next_level;          // opaque pointer to lookup structure
  DynArray<int> leaf_indexs; // pointers HostLeaf(s)
};

typedef void (*HostLookupPrintFunc)(void *opaque_data);
//
//  End Host Lookup Helper types
//

struct HostLookupState {
  HostLookupState() : cur(NULL), table_level(0), array_index(0), hostname(NULL), host_copy(NULL), host_copy_next(NULL) {}
  ~HostLookupState() { ats_free(host_copy); }
  HostBranch *cur;
  int table_level;
  int array_index;
  const char *hostname;
  char *host_copy;      // request lower-cased host name copy
  char *host_copy_next; // ptr to part of host_copy for next use
};

class HostLookup
{
public:
  HostLookup(const char *name);
  ~HostLookup();
  void NewEntry(const char *match_data, bool domain_record, void *opaque_data_in);
  void AllocateSpace(int num_entries);
  bool Match(const char *host);
  bool Match(const char *host, void **opaque_ptr);
  bool MatchFirst(const char *host, HostLookupState *s, void **opaque_ptr);
  bool MatchNext(HostLookupState *s, void **opaque_ptr);
  void Print(HostLookupPrintFunc f);
  void Print();
  HostLeaf *
  getLArray()
  {
    return leaf_array;
  };

private:
  void TableInsert(const char *match_data, int index, bool domain_record);
  HostBranch *TableNewLevel(HostBranch *from, const char *level_data);
  HostBranch *InsertBranch(HostBranch *insert_in, const char *level_data);
  HostBranch *FindNextLevel(HostBranch *from, const char *level_data, bool bNotProcess = false);
  bool MatchArray(HostLookupState *s, void **opaque_ptr, DynArray<int> &array, bool host_done);
  void PrintHostBranch(HostBranch *hb, HostLookupPrintFunc f);
  HostBranch *root;         // The top of the search tree
  HostLeaf *leaf_array;     // array of all leaves in tree
  int array_len;            // the length of the arrays
  int num_el;               // the numbe of itmems in the tree
  const char *matcher_name; // Used for Debug/Warning/Error messages
};

#endif
