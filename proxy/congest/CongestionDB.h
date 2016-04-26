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
 *  CongestionDB.h - Implementation of Congestion Control
 *
 *
 ****************************************************************************/

/*
 * CongestionDB is implemented in a Multithread-Safe hash table
 * the Data will be wrote to a disk file for recovery purpose.
 */
#ifndef CongestionDB_H_
#define CongestionDB_H_

#include "P_EventSystem.h"
#include "MT_hashtable.h"
#include "ControlMatcher.h"

class CongestionControlRecord;
struct CongestionEntry;

typedef MTHashTable<uint64_t, CongestionEntry *> CongestionTable;
typedef HashTableIteratorState<uint64_t, CongestionEntry *> Iter;

/* API to the outside world */
// check whether key was congested, store the found entry into pEntry
Action *get_congest_entry(Continuation *cont, HttpRequestData *data, CongestionEntry **ppEntry);
Action *get_congest_list(Continuation *cont, MIOBuffer *buffer, int format = 0);
void remove_all_congested_entry(void);
void remove_congested_entry(uint64_t key);
void remove_congested_entry(char *buf, MIOBuffer *out_buffer);
void revalidateCongestionDB();
void initCongestionDB();

/*
 * CongestRequestParam is the data structure passed to the request
 * to update the congestion db with the appropriate info
 * It is used when the TS missed a try_lock, the request info will be
 * stored in the CongestRequestParam and insert in the to-do list of the
 * approperiate DB partition.
 * The first operation after the TS get the lock for a partition is
 * to run the to do list
 */

struct CongestRequestParam {
  enum Op_t {
    ADD_RECORD,
    REMOVE_RECORD,
    REMOVE_ALL_RECORDS,
    REVALIDATE_BUCKET,
  };

  CongestRequestParam() : m_key(0), m_op(REVALIDATE_BUCKET), m_pEntry(NULL) {}
  ~CongestRequestParam() {}
  uint64_t m_key;
  Op_t m_op;
  CongestionEntry *m_pEntry;

  LINK(CongestRequestParam, link);
};

/* struct declaration and definitions */
class CongestionDB : public CongestionTable
{
public:
  CongestionDB(int tablesize);
  ~CongestionDB();
  bool congested(uint64_t key);

  // add an entry to the db
  void addRecord(uint64_t key, CongestionEntry *pEntry);
  // remove an entry from the db
  void removeRecord(uint64_t key);
  void removeAllRecords(void);
  InkAtomicList *todo_lists;
  void RunTodoList(int buckId);
  void process(int buckId, CongestRequestParam *param);
  void revalidateBucket(int buckId);
};

extern CongestionDB *theCongestionDB;

#endif /* CongestionDB_H_ */
