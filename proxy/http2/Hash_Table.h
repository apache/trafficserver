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

/****************************************************************************

  HashTable.h --
  Created On          : Mon Dec 02 2007
 ****************************************************************************/


#ifndef _HASH_TABLE
#define _HASH_TABLE

#define NUM_BUCKETS 4096        //Number of buckets in the hash table

#include "ink_rwlock.h"
#include "../hdrs/HTTP.h"

class HttpRequestData;

//Alternate Header for a particular url.
typedef struct _alt_hdr
{
  HTTPHdr *hdr;
  ink_hrtime revalidation_start_time;
  bool revalidation_in_progress;
  bool response_noncacheable;
  struct _alt_hdr *next;
  struct _alt_hdr *prev;
} HeaderAlternate;

//Request node in the bucket, pointer to alternates are present.
typedef struct _req_node
{
  HttpRequestData *url;
  HeaderAlternate *Alternates;
  struct _req_node *next_request;
  struct _req_node *prev_request;
} RequestNode;

//Bucket structure with pointer to first node and access control variables
typedef struct _bucket
{
  RequestNode *First;
  ink_rwlock BucketMutex;
} Bucket;

//The Hash Table class with all the implementation.
class HashTable
{
  Bucket *m_HashTable[NUM_BUCKETS];
  int number_entries;
  RequestNode *find_request(int, char *);
  float match_Headers(HTTPHdr *, HTTPHdr *);
  bool initialized;
public:
    HashTable():number_entries(0), initialized(false)
  {
    memset(&m_HashTable, 0, sizeof(m_HashTable));
  }
   ~HashTable();
  void createHashTable();
  unsigned int KeyToIndex(register char *);
  HeaderAlternate *lookup(int, char *, HTTPHdr *);
  HeaderAlternate *insert(int, HttpRequestData *, bool);
  int remove(int, char *, HeaderAlternate *);
  void update_revalidation_start_time(int, HeaderAlternate *);
  void set_response_noncacheable(int, HeaderAlternate *);

};

#endif
