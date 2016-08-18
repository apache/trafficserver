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
 *  HostLookup.cc - Implementation of a hostname/domainname matcher
 *
 *
 ****************************************************************************/
#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/DynArray.h"
#include "ts/ink_inet.h"
#include "ts/ink_assert.h"
#include "ts/ink_hash_table.h"
#include "ts/Tokenizer.h"
#include "ts/HostLookup.h"
#include "ts/MatcherUtils.h"

// bool domaincmp(const char* hostname, const char* domain)
//
//    Returns true if hostname is in domain
//
bool
domaincmp(const char *hostname, const char *domain)
{
  ink_assert(hostname != NULL);
  ink_assert(domain != NULL);

  const char *host_cur   = hostname + strlen(hostname);
  const char *domain_cur = domain + strlen(domain);

  // Check to see if were passed emtpy stings for either
  //  argument.  Empty strings do not match anything
  //
  if (domain_cur == domain || host_cur == hostname) {
    return false;
  }
  // Go back to the last character
  domain_cur--;
  host_cur--;

  // Trailing dots should be ignored since they are optional
  //
  if (*(domain_cur) == '.') {
    domain_cur--;
  }
  if (*(host_cur) == '.') {
    host_cur--;
  }
  // Walk through both strings backward
  while (domain_cur >= domain && host_cur >= hostname) {
    // If we still have characters left on both strings and
    //   they do not match, matching fails
    //
    if (tolower(*domain_cur) != tolower(*host_cur)) {
      return false;
    }

    domain_cur--;
    host_cur--;
  };

  // There are three possible cases that could have gotten us
  //  here
  //
  //     Case 1: we ran out of both strings
  //     Case 2: we ran out of domain but not hostname
  //     Case 3: we ran out of hostname but not domain
  //
  if (domain_cur < domain) {
    if (host_cur < hostname) {
      // This covers the case 1
      //   ex: example.com matching example.com
      return true;
    } else {
      // This covers case 2 (the most common case):
      //   ex: www.example.com matching .com or com
      // But we must check that we do match
      //   www.inktomi.ecom against com
      //
      if (*(domain_cur + 1) == '.') {
        return true;
      } else if (*host_cur == '.') {
        return true;
      } else {
        return false;
      }
    }
  } else if (host_cur < hostname) {
    // This covers the case 3 (a very unusual case)
    //  ex: example.com needing to match .example.com
    if (*domain_cur == '.' && domain_cur == domain) {
      return true;
    } else {
      return false;
    }
  }

  ink_assert(!"Should not get here");
  return false;
}

// int hostcmp(const char* c1, const char* c2)
//
//   Similar to strcasecmp except that if one string has a
//     trailing '.' and the other one does not
//     then they are equal
//
//   Meant to compare to host names and
//     take in out account that www.example.com
//     and www.example.com. are the same host
//     since the trailing dot is optional
//
int
hostcmp(const char *c1, const char *c2)
{
  ink_assert(c1 != NULL);
  ink_assert(c2 != NULL);
  do {
    if (tolower(*c1) < tolower(*c2)) {
      if (*c1 == '\0' && *c2 == '.' && *(c2 + 1) == '\0') {
        break;
      }
      return -1;
    } else if (tolower(*c1) > tolower(*c2)) {
      if (*c2 == '\0' && *c1 == '.' && *(c1 + 1) == '\0') {
        break;
      }
      return 1;
    }

    if (*c1 == '\0') {
      break;
    }
    c1++;
    c2++;
  } while (1);

  return 0;
}

// static const unsigned char asciiToTable[256]
//
//   Used to Map Legal hostname characters into
//     to indexes used by char_table to
//     index strings
//
//   Legal hostname characters are 0-9, A-Z, a-z and '-'
//   '_' is also included although it is not in the spec (RFC 883)
//
//   Uppercase and lowercase "a-z" both map to same indexes
//     since hostnames are not case sensative
//
//   Illegal characters map to 255
//
static const unsigned char asciiToTable[256] = {
  255, 255, 255, 255, 255, 255, 255, 255, // 0 - 7
  255, 255, 255, 255, 255, 255, 255, 255, // 8 - 15
  255, 255, 255, 255, 255, 255, 255, 255, // 16 - 23
  255, 255, 255, 255, 255, 255, 255, 255, // 24 - 31
  255, 255, 255, 255, 255, 255, 255, 255, // 32 - 39
  255, 255, 255, 255, 255, 0,   255, 255, // 40 - 47 ('-')
  1,   2,   3,   4,   5,   6,   7,   8,   // 48 - 55 (0-7)
  9,   10,  255, 255, 255, 255, 255, 255, // 56 - 63 (8-9)
  255, 11,  12,  13,  14,  15,  16,  17,  // 64 - 71 (A-G)
  18,  19,  20,  21,  22,  23,  24,  25,  // 72 - 79 (H-O)
  26,  27,  28,  29,  30,  31,  32,  33,  // 80 - 87 (P-W)
  34,  35,  36,  255, 255, 255, 255, 37,  // 88 - 95 (X-Z, '_')
  255, 11,  12,  13,  14,  15,  16,  17,  // 96 - 103 (a-g)
  18,  19,  20,  21,  22,  23,  24,  25,  // 104 - 111 (h-0)
  26,  27,  28,  29,  30,  31,  32,  33,  // 112 - 119 (p-w)
  34,  35,  36,  255, 255, 255, 255, 255, // 120 - 127 (x-z)
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

// Number of legal characters in the acssiToTable array
static const int numLegalChars = 38;

// struct charIndex_el
//
//   Used by class charIndex.  Forms a single level
//    in charIndex tree
//
struct charIndex_el {
  charIndex_el();
  ~charIndex_el();
  HostBranch *branch_array[numLegalChars];
  charIndex_el *next_level[numLegalChars];
};

charIndex_el::charIndex_el()
{
  memset(branch_array, 0, sizeof(branch_array));
  memset(next_level, 0, sizeof(next_level));
}

charIndex_el::~charIndex_el()
{
  int i;

  // Recursively delete all the lower levels of the
  //   data structure
  for (i = 0; i < numLegalChars; i++) {
    if (next_level[i] != NULL) {
      delete next_level[i];
    }
  }
}

// struct charIndexIterInternal
//
//  An internal struct to charIndexIterState
//    Stores the location of an element in
//    class charIndex
//
struct charIndexIterInternal {
  charIndex_el *ptr;
  int index;
};

// Used as a default return element for DynArray in
//   struct charIndexIterState
static charIndexIterInternal default_iter = {NULL, -1};

// struct charIndexIterState
//
//    struct for the callee to keep interation state
//      for class charIndex
//
struct charIndexIterState {
  charIndexIterState();

  // Where that level we are in interation
  int cur_level;

  // Where we got the last element from
  int cur_index;
  charIndex_el *cur_el;

  //  Queue of the above levels
  DynArray<charIndexIterInternal> q;
};

charIndexIterState::charIndexIterState() : cur_level(-1), cur_index(-1), cur_el(NULL), q(&default_iter, 6)
{
}

// class charIndex - A constant time string matcher intended for
//    short strings in a sparsely populated DNS paritition
//
//    Creates a look up table for character in data string
//
//    Mapping from character to table index is done by
//      asciiToTable[]
//
//    The illegalKey hash table is side structure for any
//     entries that contain illegal hostname characters that
//     we can not index into the normal table
//
//    Example: com
//      c maps to 13, o maps to 25, m maps to 23
//
//                             charIndex_el         charIndex_el
//                             -----------         ------------
//                           0 |    |    |         |    |     |
//                           . |    |    |         |    |     |
//    charIndex_el           . |    |    |         |    |     |
//    ----------             . |    |    |         |    |     |
//  0 |   |    |             . |    |    |   |-->23| ptr|  0  |  (ptr is to the
//  . |   |    |   |-------->25| 0  |   -----|     |    |     |   hostBranch for
//  . |   |    |   |         . |    |    |         |    |     |   domain com)
// 13 | 0 |  --|---|         . |    |    |         |    |     |
//  . |   |    |             . |    |    |         |    |     |
//  . |   |    |               |    |    |         |    |     |
//  . |   |    |               |    |    |         |    |     |
//    |   |    |               -----------         -----------|
//    |   |    |
//    |   |    |
//    |   |    |
//    |--------|
//
//
//
class charIndex
{
public:
  charIndex();
  ~charIndex();
  void Insert(const char *match_data, HostBranch *toInsert);
  HostBranch *Lookup(const char *match_data);
  HostBranch *iter_first(charIndexIterState *s);
  HostBranch *iter_next(charIndexIterState *s);

private:
  charIndex_el *root;
  InkHashTable *illegalKey;
};

charIndex::charIndex() : illegalKey(NULL)
{
  root = new charIndex_el;
}

charIndex::~charIndex()
{
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry = NULL;
  HostBranch *tmp;

  delete root;

  // Destroy the illegalKey hashtable if there is one and free
  //   up all of its values
  if (illegalKey != NULL) {
    ht_entry = ink_hash_table_iterator_first(illegalKey, &ht_iter);

    while (ht_entry != NULL) {
      tmp = (HostBranch *)ink_hash_table_entry_value(illegalKey, ht_entry);
      ink_assert(tmp != NULL);
      delete tmp;
      ht_entry = ink_hash_table_iterator_next(illegalKey, &ht_iter);
    }
    ink_hash_table_destroy(illegalKey);
  }
}

// void charIndex::Insert(const char* match_data, HostBranch* toInsert)
//
//   Places a binding for match_data to toInsert into the index
//
void
charIndex::Insert(const char *match_data, HostBranch *toInsert)
{
  unsigned char index;
  const char *match_start = match_data;
  charIndex_el *cur       = root;
  charIndex_el *next;

  if (*match_data == '\0') {
    // Should not happen
    ink_assert(0);
    return;
  }

  while (1) {
    index = asciiToTable[(unsigned char)(*match_data)];

    // Check to see if our index into table is for an
    //  'illegal' DNS character
    if (index == 255) {
      // Insert into illgals hash table
      if (illegalKey == NULL) {
        illegalKey = ink_hash_table_create(InkHashTableKeyType_String);
      }
      ink_hash_table_insert(illegalKey, (char *)match_start, toInsert);
      break;
    }

    // Check to see if are at the level we supposed be at
    if (*(match_data + 1) == '\0') {
      // The slot should always be emtpy, no duplicate
      //   keys are allowed
      ink_assert(cur->branch_array[index] == NULL);
      cur->branch_array[index] = toInsert;
      break;
    } else {
      // We need to find the next level in the table

      next = cur->next_level[index];

      // Check to see if we need to expand the table
      if (next == NULL) {
        next                   = new charIndex_el;
        cur->next_level[index] = next;
      }
      cur = next;
    }
    match_data++;
  }
}

// HostBranch* charIndex::Lookup(const char* match_data)
//
//  Searches the charIndex on key match_data
//    If there is a binding for match_data, returns a pointer to it
//    otherwise a NULL pointer is returned
//
HostBranch *
charIndex::Lookup(const char *match_data)
{
  unsigned char index;
  charIndex_el *cur = root;
  void *hash_lookup;
  const char *match_start = match_data;

  if (root == NULL || *match_data == '\0') {
    return NULL;
  }

  while (1) {
    index = asciiToTable[(unsigned char)(*match_data)];

    // Check to see if our index into table is for an
    //  'illegal' DNS character
    if (index == 255) {
      if (illegalKey == NULL) {
        return NULL;
      } else {
        if (ink_hash_table_lookup(illegalKey, (char *)match_start, &hash_lookup)) {
          return (HostBranch *)hash_lookup;
        } else {
          return NULL;
        }
      }
    }
    // Check to see if we are looking for the next level or
    //    a HostBranch
    if (*(match_data + 1) == '\0') {
      return cur->branch_array[index];
    } else {
      cur = cur->next_level[index];

      if (cur == NULL) {
        return NULL;
      }
    }

    match_data++;
  }
}

//
// HostBranch* charIndex::iter_next(charIndexIterState* s)
//
//    Initialize iterator state and returns the first element
//     found in the charTable.  If none is found, NULL
//     is returned
//
HostBranch *
charIndex::iter_first(charIndexIterState *s)
{
  s->cur_level = 0;
  s->cur_index = -1;
  s->cur_el    = root;

  return iter_next(s);
}

//
// HostBranch* charIndex::iter_next(charIndexIterState* s)
//
//    Finds the next element in the char index and returns
//      a pointer to it.  If there are no more elements, NULL
//      is returned
//
HostBranch *
charIndex::iter_next(charIndexIterState *s)
{
  int index;
  charIndex_el *current_el = s->cur_el;
  intptr_t level           = s->cur_level;
  charIndexIterInternal stored_state;
  HostBranch *r = NULL;
  bool first_element;

  // bool first_element is used to tell if first elemente
  //  pointed to by s has already been returned to caller
  //  it has unless we are being called from iter_first
  if (s->cur_index < 0) {
    first_element = false;
    index         = s->cur_index + 1;
  } else {
    first_element = true;
    index         = s->cur_index;
  }

  while (1) {
    // Check to see if we need to go back up a level
    if (index >= numLegalChars) {
      if (level <= 0) {
        // No more levels so bail out
        break;
      } else {
        // Go back up to a stored level
        stored_state = s->q[level - 1];
        ink_assert(stored_state.ptr != NULL);
        ink_assert(stored_state.index >= 0);
        level--;
        current_el = stored_state.ptr;
        index      = stored_state.index + 1;
      }
    } else {
      // Check to see if there is something to return
      //
      //  Note: we check for something to return before a descending
      //    a level so that when we come back up we will
      //    be done with this index
      //
      if (current_el->branch_array[index] != NULL && first_element == false) {
        r            = current_el->branch_array[index];
        s->cur_level = level;
        s->cur_index = index;
        s->cur_el    = current_el;
        break;
      } else if (current_el->next_level[index] != NULL) {
        // There is a lower level to iterate over, store our
        //   current state and descend
        stored_state.ptr   = current_el;
        stored_state.index = index;
        s->q(level)        = stored_state;
        current_el         = current_el->next_level[index];
        index              = 0;
        level++;
      } else {
        // Nothing here so advance to next index
        index++;
      }
    }
    first_element = false;
  }

  return r;
}

// class hostArray
//
//   Is a fixed size array for holding HostBrach*
//   Allows only sequential access to data
//

// Since the only iter state is an index into the
//   array typedef it
typedef int hostArrayIterState;

class hostArray
{
public:
  hostArray();
  ~hostArray();
  bool Insert(const char *match_data_in, HostBranch *toInsert);
  HostBranch *Lookup(const char *match_data_in, bool bNotProcess);
  HostBranch *iter_first(hostArrayIterState *s, char **key = NULL);
  HostBranch *iter_next(hostArrayIterState *s, char **key = NULL);

private:
  int num_el; // number of elements currently in the array
  HostBranch *branch_array[HOST_ARRAY_MAX];
  char *match_data[HOST_ARRAY_MAX];
};

hostArray::hostArray() : num_el(0)
{
  memset(branch_array, 0, sizeof(branch_array));
  memset(match_data, 0, sizeof(match_data));
}

hostArray::~hostArray()
{
  for (int i = 0; i < num_el; i++) {
    ink_assert(branch_array[i] != NULL);
    ink_assert(match_data[i] != NULL);
    ats_free(match_data[i]);
  }
}

// bool hostArray::Insert(const char* match_data_in, HostBranch* toInsert)
//
//    Places toInsert into the array with key match_data if there
//     is adequate space, in which case true is returned
//    If space is inadequate, false is returned and nothing is inserted
//
bool
hostArray::Insert(const char *match_data_in, HostBranch *toInsert)
{
  if (num_el >= HOST_ARRAY_MAX) {
    return false;
  } else {
    branch_array[num_el] = toInsert;
    match_data[num_el]   = ats_strdup(match_data_in);
    num_el++;
    return true;
  }
}

// HostBranch* hostArray::Lookup(const char* match_data_in)
//
//   Looks for key match_data_in.  If a binding is found,
//     returns HostBranch* found to the key, otherwise
//     NULL is returned
//
HostBranch *
hostArray::Lookup(const char *match_data_in, bool bNotProcess)
{
  HostBranch *r = NULL;
  char *pMD;

  for (int i = 0; i < num_el; i++) {
    pMD = match_data[i];

    if (bNotProcess && '!' == *pMD) {
      char *cp = ++pMD;
      if ('\0' == *cp)
        continue;

      if (strcmp(cp, match_data_in) != 0) {
        r = branch_array[i];
      } else {
        continue;
      }
    }

    else if (strcmp(match_data[i], match_data_in) == 0) {
      r = branch_array[i];
      break;
    }
  }
  return r;
}

// HostBranch* hostArray::iter_first(hostArrayIterState* s) {
//
//   Initilizes s and returns the first element or
//     NULL if no elements exist
//
HostBranch *
hostArray::iter_first(hostArrayIterState *s, char **key)
{
  *s = -1;
  return iter_next(s, key);
}

// HostBranch* hostArray::iter_next(hostArrayIterState* s) {
//
//    Returns the next element in the hostArray or
//      NULL if none exist
//
HostBranch *
hostArray::iter_next(hostArrayIterState *s, char **key)
{
  (*s)++;

  if (*s < num_el) {
    if (key != NULL) {
      *key = match_data[*s];
    }
    return branch_array[*s];
  } else {
    return NULL;
  }
}

// maps enum LeafType to strings
const char *LeafTypeStr[] = {"Leaf Invalid", "Host (Partial)", "Host (Full)", "Domain (Full)", "Domain (Partial)"};

static int negative_one = -1;

HostBranch::HostBranch() : level(0), type(HOST_TERMINAL), next_level(NULL), leaf_indexs(&negative_one, 1)
{
}

// HostBranch::~HostBranch()
//
//   Recursive delete all host branches below us
//
HostBranch::~HostBranch()
{
  // Hash Iteration
  InkHashTable *ht;
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry = NULL;

  // charIndex Iteration
  charIndexIterState ci_iter;
  charIndex *ci;

  // hostArray Iteration
  hostArray *ha;
  hostArrayIterState ha_iter;

  HostBranch *lower_branch;

  switch (type) {
  case HOST_TERMINAL:
    ink_assert(next_level == NULL);
    break;
  case HOST_HASH:
    ink_assert(next_level != NULL);
    ht       = (InkHashTable *)next_level;
    ht_entry = ink_hash_table_iterator_first(ht, &ht_iter);

    while (ht_entry != NULL) {
      lower_branch = (HostBranch *)ink_hash_table_entry_value(ht, ht_entry);
      delete lower_branch;
      ht_entry = ink_hash_table_iterator_next(ht, &ht_iter);
    }
    ink_hash_table_destroy(ht);
    break;
  case HOST_INDEX:
    ink_assert(next_level != NULL);
    ci           = (charIndex *)next_level;
    lower_branch = ci->iter_first(&ci_iter);
    while (lower_branch != NULL) {
      delete lower_branch;
      lower_branch = ci->iter_next(&ci_iter);
    }
    delete ci;
    break;
  case HOST_ARRAY:
    ink_assert(next_level != NULL);
    ha           = (hostArray *)next_level;
    lower_branch = ha->iter_first(&ha_iter);
    while (lower_branch != NULL) {
      delete lower_branch;
      lower_branch = ha->iter_next(&ha_iter);
    }
    delete ha;
    break;
  }
}

HostLookup::HostLookup(const char *name) : leaf_array(NULL), array_len(-1), num_el(-1), matcher_name(name)
{
  root             = new HostBranch;
  root->level      = 0;
  root->type       = HOST_TERMINAL;
  root->next_level = NULL;
}

HostLookup::~HostLookup()
{
  if (leaf_array != NULL) {
    // Free up the match strings
    for (int i = 0; i < num_el; i++) {
      ats_free(leaf_array[i].match);
    }
    delete[] leaf_array;
  }

  delete root;
}

static void
empty_print_fn(void * /* opaque_data ATS_UNUSED */)
{
}

void
HostLookup::Print()
{
  Print(empty_print_fn);
}

void
HostLookup::Print(HostLookupPrintFunc f)
{
  PrintHostBranch(root, f);
}

//
// void HostLookup::PrintHostBranch(HostBranch* hb, HostLookupPrintFunc f)
//
//   Recursively traverse the matching tree rooted at arg hb
//     and print out each element
//
void
HostLookup::PrintHostBranch(HostBranch *hb, HostLookupPrintFunc f)
{
  // Hash iteration
  InkHashTable *ht;
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry = NULL;

  // charIndex Iteration
  charIndexIterState ci_iter;
  charIndex *ci;

  // hostArray Iteration
  hostArray *h_array;
  hostArrayIterState ha_iter;

  HostBranch *lower_branch;
  intptr_t curIndex;
  intptr_t i; // Loop var

  for (i = 0; i < hb->leaf_indexs.length(); i++) {
    curIndex = hb->leaf_indexs[i];
    printf("\t\t%s for %s\n", LeafTypeStr[leaf_array[curIndex].type], leaf_array[curIndex].match);
    f(leaf_array[curIndex].opaque_data);
  }

  switch (hb->type) {
  case HOST_TERMINAL:
    ink_assert(hb->next_level == NULL);
    break;
  case HOST_HASH:
    ink_assert(hb->next_level != NULL);
    ht       = (InkHashTable *)hb->next_level;
    ht_entry = ink_hash_table_iterator_first(ht, &ht_iter);

    while (ht_entry != NULL) {
      lower_branch = (HostBranch *)ink_hash_table_entry_value(ht, ht_entry);
      PrintHostBranch(lower_branch, f);
      ht_entry = ink_hash_table_iterator_next(ht, &ht_iter);
    }
    break;
  case HOST_INDEX:
    ink_assert(hb->next_level != NULL);
    ci           = (charIndex *)hb->next_level;
    lower_branch = ci->iter_first(&ci_iter);
    while (lower_branch != NULL) {
      PrintHostBranch(lower_branch, f);
      lower_branch = ci->iter_next(&ci_iter);
    }
    break;
  case HOST_ARRAY:
    ink_assert(hb->next_level != NULL);
    h_array      = (hostArray *)hb->next_level;
    lower_branch = h_array->iter_first(&ha_iter);
    while (lower_branch != NULL) {
      PrintHostBranch(lower_branch, f);
      lower_branch = h_array->iter_next(&ha_iter);
    }
    break;
  }
}

//
// HostBranch* HostLookup::TableNewLevel(HostBranch* from, const char* level_data)
//
//    Creates the next level structure in arg from
//       Creates a HostBranch for level_data, inserts it in to the
//         the next_level structure and returns a pointer to the new
//         HostBranch
//
HostBranch *
HostLookup::TableNewLevel(HostBranch *from, const char *level_data)
{
  hostArray *new_ha_table;
  charIndex *new_ci_table;

  ink_assert(from->type == HOST_TERMINAL);

  // Use the charIndex for high speed matching at the first level of
  //   the table.  The first level is short strings, ie: com, edu, jp, fr
  if (from->level == 0) {
    new_ci_table     = new charIndex;
    from->type       = HOST_INDEX;
    from->next_level = new_ci_table;
  } else {
    new_ha_table     = new hostArray;
    from->type       = HOST_ARRAY;
    from->next_level = new_ha_table;
  }

  return InsertBranch(from, level_data);
}

//
// HostBranch* HostLookup::InsertBranch(HostBranch* insert_to, const char* level_data)
//
//
//    Abstrction to place a new node for level_data below node
//      insert to.  Inserts into any of the data types used by
//      by class HostMatcher
//
HostBranch *
HostLookup::InsertBranch(HostBranch *insert_in, const char *level_data)
{
  // Variables for moving an array into a hash table after it
  //   gets too big
  //
  hostArray *ha;
  hostArrayIterState ha_iter;
  HostBranch *tmp;
  char *key = NULL;
  InkHashTable *new_ht;

  HostBranch *new_branch = new HostBranch;
  new_branch->type       = HOST_TERMINAL;
  new_branch->level      = insert_in->level + 1;
  new_branch->next_level = NULL;

  switch (insert_in->type) {
  case HOST_TERMINAL:
    // Should not happen
    ink_release_assert(0);
    break;
  case HOST_HASH:
    ink_hash_table_insert((InkHashTable *)insert_in->next_level, (char *)level_data, new_branch);
    break;
  case HOST_INDEX:
    ((charIndex *)insert_in->next_level)->Insert(level_data, new_branch);
    break;
  case HOST_ARRAY:
    if (((hostArray *)insert_in->next_level)->Insert(level_data, new_branch) == false) {
      // The array is out of space, time to move to a hash table
      ha     = (hostArray *)insert_in->next_level;
      new_ht = ink_hash_table_create(InkHashTableKeyType_String);
      ink_hash_table_insert(new_ht, (char *)level_data, new_branch);

      // Iterate through the existing elements in the array and
      //  stuff them into the hash table
      tmp = ha->iter_first(&ha_iter, &key);
      ink_assert(tmp != NULL);
      while (tmp != NULL) {
        ink_assert(key != NULL);
        ink_hash_table_insert(new_ht, key, tmp);
        tmp = ha->iter_next(&ha_iter, &key);
      }

      // Ring out the old, ring in the new
      delete ha;
      insert_in->next_level = new_ht;
      insert_in->type       = HOST_HASH;
    }
    break;
  }

  return new_branch;
}

// HostBranch* HostLookup::FindNextLevel(HostBranch* from,
//                                       const char* level_data)
//
//   Searches in the branch for  the next level down in the search
//     structure bound to level data
//   If found returns a pointer to the next level HostBranch,
//    otherwise returns NULL
//
HostBranch *
HostLookup::FindNextLevel(HostBranch *from, const char *level_data, bool bNotProcess)
{
  HostBranch *r = NULL;
  InkHashTable *hash;
  charIndex *ci_table;
  hostArray *ha_table;
  void *lookup;

  switch (from->type) {
  case HOST_TERMINAL:
    // Should not happen
    ink_assert(0);
    return NULL;
  case HOST_HASH:
    hash = (InkHashTable *)from->next_level;
    ink_assert(hash != NULL);
    if (ink_hash_table_lookup(hash, (char *)level_data, &lookup)) {
      r = (HostBranch *)lookup;
    } else {
      r = NULL;
    }
    break;
  case HOST_INDEX:
    ci_table = (charIndex *)from->next_level;
    ink_assert(ci_table != NULL);
    r = ci_table->Lookup(level_data);
    break;
  case HOST_ARRAY:
    ha_table = (hostArray *)from->next_level;
    ink_assert(ha_table != NULL);
    r = ha_table->Lookup(level_data, bNotProcess);
    break;
  }
  return r;
}

// void HostLookup::TableInsert(const char* match_data, int index)
//
//   Insert the match data, index pair into domain/search table
//
//   arg index is the index into both Data and HostLeaf array for
//    the elements corresponding to match_data
//
void
HostLookup::TableInsert(const char *match_data, int index, bool domain_record)
{
  HostBranch *cur = this->root;
  HostBranch *next;
  char *match_copy = ats_strdup(match_data);
  Tokenizer match_tok(".");
  int numTok;
  int i;

  LowerCaseStr(match_copy);
  numTok = match_tok.Initialize(match_copy, SHARE_TOKS);

  // Traverse down the search structure until we either
  //       Get beyond the fixed number depth of the host table
  //  OR   We reach the level where the match stops
  //
  for (i = 0; i < HOST_TABLE_DEPTH; i++) {
    // Check to see we need to stop at the current level
    if (numTok == cur->level) {
      break;
    }

    if (cur->next_level == NULL) {
      cur = TableNewLevel(cur, match_tok[numTok - i - 1]);
    } else {
      next = FindNextLevel(cur, match_tok[numTok - i - 1]);
      if (next == NULL) {
        cur = InsertBranch(cur, match_tok[numTok - i - 1]);
      } else {
        cur = next;
      }
    }
  }

  // Update the leaf type.  There are three types:
  //     HOST_PARTIAL - Indicates that part of the hostname name
  //         was not matched by traversin the search structure since
  //         it had too elements.  A comparison must be done at the
  //         leaf node to make sure we have a match
  //     HOST_COMPLETE - Indicates that the entire domain name
  //         in the match_data was matched by traversing the search
  //         structure, no further comparison is necessary
  //
  //     DOMAIN_COMPLETE - Indicates that the entire domain name
  //         in the match_data was matched by traversing the search
  //         structure, no further comparison is necessary
  //     DOMAIN_PARTIAL - Indicates that part of the domain name
  //         was not matched by traversin the search structure since
  //         it had too elements.  A comparison must be done at the
  //         leaf node to make sure we have a match
  if (domain_record == false) {
    if (numTok > HOST_TABLE_DEPTH) {
      leaf_array[index].type = HOST_PARTIAL;
    } else {
      leaf_array[index].type = HOST_COMPLETE;
    }
  } else {
    if (numTok > HOST_TABLE_DEPTH) {
      leaf_array[index].type = DOMAIN_PARTIAL;
    } else {
      leaf_array[index].type = DOMAIN_COMPLETE;
    }
  }

  // Place the index in to leaf array into the match list for this
  //   HOST_BRANCH
  cur->leaf_indexs(cur->leaf_indexs.length()) = index;

  ats_free(match_copy);
}

// bool HostLookup::MatchArray(HostLookupState* s, void**opaque_ptr, DynArray<int>& array,
//                             bool host_done)
//
//  Helper function to iterate throught arg array and update Result
//    for each element in arg array
//
//  host_done should be passed as true if this call represents the all fields
//     in the matched against hostname being consumed.  Example: for www.example.com
//     this would be true for the call matching against the "www", but
//     neither of the prior two fields, "inktomi" and "com"
//

bool
HostLookup::MatchArray(HostLookupState *s, void **opaque_ptr, DynArray<int> &array, bool host_done)
{
  intptr_t index;
  intptr_t i;

  for (i = s->array_index + 1; i < array.length(); i++) {
    index = array[i];

    switch (leaf_array[index].type) {
    case HOST_PARTIAL:
      if (hostcmp(s->hostname, leaf_array[index].match) == 0) {
        *opaque_ptr    = leaf_array[index].opaque_data;
        s->array_index = i;
        return true;
      }
      break;
    case HOST_COMPLETE:
      // We have to have consumed the whole hostname for this to match
      //   so that we do not match a rule for "example.com" to
      //   "www.example.com
      //
      if (host_done == true) {
        *opaque_ptr    = leaf_array[index].opaque_data;
        s->array_index = i;
        return true;
      }
      break;
    case DOMAIN_PARTIAL:
      if (domaincmp(s->hostname, leaf_array[index].match) == false) {
        break;
      }
    // FALL THROUGH
    case DOMAIN_COMPLETE:
      *opaque_ptr    = leaf_array[index].opaque_data;
      s->array_index = i;
      return true;
    case LEAF_INVALID:
      // Should not get here
      ink_assert(0);
      break;
    }
  }

  s->array_index = i;
  return false;
}

// bool HostLookup::MatchFirst(const char* host, HostLookupState* s, void** opaque_ptr)
//
//
bool
HostLookup::MatchFirst(const char *host, HostLookupState *s, void **opaque_ptr)
{
  char *last_dot = NULL;

  s->cur         = root;
  s->table_level = 0;
  s->array_index = -1;
  s->hostname    = host ? host : "";
  s->host_copy   = ats_strdup(s->hostname);
  LowerCaseStr(s->host_copy);

  // Find the top level domain in the host copy
  s->host_copy_next = s->host_copy;
  while (*s->host_copy_next != '\0') {
    if (*s->host_copy_next == '.') {
      last_dot = s->host_copy_next;
    }
    s->host_copy_next++;
  }

  if (last_dot == NULL) {
    // Must be an unqualified hostname, no dots
    s->host_copy_next = s->host_copy;
  } else {
    s->host_copy_next = last_dot + 1;
  }

  return MatchNext(s, opaque_ptr);
}

// bool HostLookup::MatchNext(HostLookupState* s, void** opaque_ptr)
//
//  Searches our tree and updates argresult for each element matching
//    arg hostname
//
bool
HostLookup::MatchNext(HostLookupState *s, void **opaque_ptr)
{
  HostBranch *cur = s->cur;

  // Check to see if there is any work to be done
  if (num_el <= 0) {
    return false;
  }

  while (s->table_level <= HOST_TABLE_DEPTH) {
    if (MatchArray(s, opaque_ptr, cur->leaf_indexs, (s->host_copy_next == NULL))) {
      return true;
    }
    // Check to see if we run out of tokens in the hostname
    if (s->host_copy_next == NULL) {
      break;
    }
    // Check to see if there are any lower levels
    if (cur->type == HOST_TERMINAL) {
      break;
    }

    cur = FindNextLevel(cur, s->host_copy_next, true);

    if (cur == NULL) {
      break;
    } else {
      s->cur         = cur;
      s->array_index = -1;
      s->table_level++;

      // Find the next part of the hostname to process
      if (s->host_copy_next <= s->host_copy) {
        // Nothing left
        s->host_copy_next = NULL;
      } else {
        // Back up to period ahead of us and axe it
        s->host_copy_next--;
        ink_assert(*s->host_copy_next == '.');
        *s->host_copy_next = '\0';

        s->host_copy_next--;

        while (1) {
          if (s->host_copy_next <= s->host_copy) {
            s->host_copy_next = s->host_copy;
            break;
          }
          // Check for our stop.  If we hit a period, we want
          //  the our portion of the hostname starts one character
          //  after it
          if (*s->host_copy_next == '.') {
            s->host_copy_next++;
            break;
          }

          s->host_copy_next--;
        }
      }
    }
  }

  return false;
}

// void HostLookup::AllocateSpace(int num_entries)
//
//   Allocate the leaf array structure
//
void
HostLookup::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  leaf_array = new HostLeaf[num_entries];
  memset(leaf_array, 0, sizeof(HostLeaf) * num_entries);

  array_len = num_entries;
  num_el    = 0;
}

// void HostLookup::NewEntry(const char* match_data, bool domain_record, void* opaque_data_in)
//
//   Insert a new element in to the table
//
void
HostLookup::NewEntry(const char *match_data, bool domain_record, void *opaque_data_in)
{
  // Make sure space has been allocated
  ink_assert(num_el >= 0);
  ink_assert(array_len >= 0);

  // Make sure we do not overrun the array;
  ink_assert(num_el < array_len);

  leaf_array[num_el].match       = ats_strdup(match_data);
  leaf_array[num_el].opaque_data = opaque_data_in;

  if ('!' != *(leaf_array[num_el].match)) {
    leaf_array[num_el].len   = strlen(match_data);
    leaf_array[num_el].isNot = false;
  } else {
    leaf_array[num_el].len   = strlen(match_data) - 1;
    leaf_array[num_el].isNot = true;
  }

  TableInsert(match_data, num_el, domain_record);
  num_el++;
}
