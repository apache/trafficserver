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
 *  CongestionDB.cc - Implementation of congestion control datastore
 *
 *
 ****************************************************************************/
#include "ts/ink_platform.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "Main.h"
#include "CongestionDB.h"
#include "Congestion.h"
#include "ProcessManager.h"

#define SCHEDULE_CONGEST_CONT_INTERVAL HRTIME_MSECONDS(5)
int CONGESTION_DB_SIZE = 1024;

CongestionDB *theCongestionDB = NULL;

/*
 * the CongestionDBCont is the continuation to do the congestion db related work
 * when the CongestionDB's corresponding function does not get the lock in the
 * first try
 */

class CongestionDBCont : public Continuation
{
public:
  CongestionDBCont();
  int GC(int event, Event *e);

  int get_congest_list(int event, Event *e);

  int get_congest_entry(int event, Event *e);

  Action m_action;

  // To save momery, use a union here
  union {
    struct {
      MIOBuffer *m_iobuf;
      int m_CurPartitionID;
      int m_list_format; // format of list
    } list_info;
    struct {
      uint64_t m_key;
      char *m_hostname;
      IpEndpoint m_ip;
      CongestionControlRecord *m_rule;
      CongestionEntry **m_ppEntry;
    } entry_info;
  } data;
};

// MACRO's to save typing
#define CDBC_buf data.list_info.m_iobuf
#define CDBC_pid data.list_info.m_CurPartitionID
#define CDBC_lf data.list_info.m_list_format
#define CDBC_key data.entry_info.m_key
#define CDBC_host data.entry_info.m_hostname
#define CDBC_ip data.entry_info.m_ip
#define CDBC_rule data.entry_info.m_rule
#define CDBC_ppE data.entry_info.m_ppEntry

inline CongestionDBCont::CongestionDBCont() : Continuation(NULL)
{
  memset(&data, 0, sizeof(data));
}

//--------------------------------------------------------------
// class allocators
static ClassAllocator<CongestionDBCont> CongestionDBContAllocator("CongestionDBContAllocator");

inline void
Free_CongestionDBCont(CongestionDBCont *cont)
{
  cont->m_action = NULL;
  cont->mutex    = NULL;
  CongestionDBContAllocator.free(cont);
}

ClassAllocator<CongestRequestParam> CongestRequestParamAllocator("CongestRequestParamAllocator");

inline void
Free_CongestRequestParam(CongestRequestParam *param)
{
  CongestRequestParamAllocator.free(param);
}

//-----------------------------------------------------------------
//  CongestionDB implementation
//-----------------------------------------------------------------
/*
 * CongestionDB(int tablesize)
 *  tablesize is the initial hashtable bucket number
 */
static long congestEntryGCTime = 0;

// Before the garbage collection of the congestion db, set the
//  current GC time, CongestionEntry::usefulInfo(t) will use the
//  timestamp to determine if the entry contains useful infomation

void
preCongestEntryGC(void)
{
  congestEntryGCTime = (long)ink_hrtime_to_sec(Thread::get_hrtime());
}

// if the entry contains useful info, return false -- keep it
// else return true -- delete it
bool
congestEntryGC(CongestionEntry *p)
{
  if (!p->usefulInfo(congestEntryGCTime)) {
    p->put();
    return true;
  }
  return false;
}

CongestionDB::CongestionDB(int tablesize) : CongestionTable(tablesize, &congestEntryGC, &preCongestEntryGC)
{
  ink_assert(tablesize > 0);
  todo_lists = new InkAtomicList[MT_HASHTABLE_PARTITIONS];
  for (int i = 0; i < MT_HASHTABLE_PARTITIONS; i++) {
    ink_atomiclist_init(&todo_lists[i], "cong_todo_list", (uintptr_t) & ((CongestRequestParam *)0)->link);
  }
}

/*
 * There should be no entry in the DB, before you call the destructor
 */

CongestionDB::~CongestionDB()
{
  delete[] todo_lists;
}

void
CongestionDB::addRecord(uint64_t key, CongestionEntry *pEntry)
{
  ink_assert(key == pEntry->m_key);
  pEntry->get();
  ProxyMutex *bucket_mutex = lock_for_key(key);
  MUTEX_TRY_LOCK(lock, bucket_mutex, this_ethread());
  if (lock.is_locked()) {
    RunTodoList(part_num(key));
    CongestionEntry *tmp = insert_entry(key, pEntry);
    if (tmp)
      tmp->put();
  } else {
    CongestRequestParam *param = CongestRequestParamAllocator.alloc();
    param->m_op                = CongestRequestParam::ADD_RECORD;
    param->m_key               = key;
    param->m_pEntry            = pEntry;
    ink_atomiclist_push(&todo_lists[part_num(key)], param);
  }
}

void
CongestionDB::removeAllRecords()
{
  CongestionEntry *tmp;
  Iter it;
  for (int part = 0; part < MT_HASHTABLE_PARTITIONS; part++) {
    ProxyMutex *bucket_mutex = lock_for_key(part);
    MUTEX_TRY_LOCK(lock, bucket_mutex, this_ethread());
    if (lock.is_locked()) {
      RunTodoList(part);
      tmp = first_entry(part, &it);
      while (tmp) {
        remove_entry(part, &it);
        tmp->put();
        tmp = cur_entry(part, &it);
      }
    } else {
      CongestRequestParam *param = CongestRequestParamAllocator.alloc();
      param->m_op                = CongestRequestParam::REMOVE_ALL_RECORDS;
      param->m_key               = part;
      ink_atomiclist_push(&todo_lists[part], param);
    }
  }
}

void
CongestionDB::removeRecord(uint64_t key)
{
  CongestionEntry *tmp;
  ProxyMutex *bucket_mutex = lock_for_key(key);
  MUTEX_TRY_LOCK(lock, bucket_mutex, this_ethread());
  if (lock.is_locked()) {
    RunTodoList(part_num(key));
    tmp = remove_entry(key);
    if (tmp)
      tmp->put();
  } else {
    CongestRequestParam *param = CongestRequestParamAllocator.alloc();
    param->m_op                = CongestRequestParam::REMOVE_RECORD;
    param->m_key               = key;
    ink_atomiclist_push(&todo_lists[part_num(key)], param);
  }
}

// process one item in the to do list
void
CongestionDB::process(int buckId, CongestRequestParam *param)
{
  CongestionEntry *pEntry = NULL;
  switch (param->m_op) {
  case CongestRequestParam::ADD_RECORD:
    pEntry = insert_entry(param->m_key, param->m_pEntry);
    if (pEntry) {
      pEntry->put();
    }
    break;
  case CongestRequestParam::REMOVE_ALL_RECORDS: {
    CongestionEntry *tmp;
    Iter it;
    tmp = first_entry(param->m_key, &it);
    while (tmp) {
      remove_entry(param->m_key, &it);
      tmp->put();
      tmp = cur_entry(param->m_key, &it);
    }
    break;
  }
  case CongestRequestParam::REMOVE_RECORD:
    pEntry = remove_entry(param->m_key);
    if (pEntry)
      pEntry->put();
    break;
  case CongestRequestParam::REVALIDATE_BUCKET:
    revalidateBucket(buckId);
    break;
  default:
    ink_assert(!"CongestionDB::process unrecognized op");
  }
}

void
CongestionDB::RunTodoList(int buckId)
{
  CongestRequestParam *param = NULL, *cur = NULL;
  if ((param = (CongestRequestParam *)ink_atomiclist_popall(&todo_lists[buckId])) != NULL) {
    /* start the work at the end of the list */
    param->link.prev = NULL;
    while (param->link.next) {
      param->link.next->link.prev = param;
      param                       = param->link.next;
    };
    while (param) {
      process(buckId, param);
      cur   = param;
      param = param->link.prev;
      Free_CongestRequestParam(cur);
    }
  }
}

void
CongestionDB::revalidateBucket(int buckId)
{
  Iter it;
  CongestionEntry *cur = NULL;
  cur                  = first_entry(buckId, &it);
  while (cur != NULL) {
    if (!cur->validate()) {
      remove_entry(buckId, &it);
      cur->put();
      // the next entry has been moved to the current pos
      // because of the remove_entry
      cur = cur_entry(buckId, &it);
    } else
      cur = next_entry(buckId, &it);
  }
}

//-----------------------------------------------------------------
//  CongestionDBCont implementation
//-----------------------------------------------------------------

int
CongestionDBCont::GC(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (congestionControlEnabled == 1 || congestionControlEnabled == 2) {
    if (theCongestionDB == NULL)
      goto Ldone;
    for (; CDBC_pid < theCongestionDB->getSize(); CDBC_pid++) {
      ProxyMutex *bucket_mutex = theCongestionDB->lock_for_key(CDBC_pid);
      {
        MUTEX_TRY_LOCK(lock, bucket_mutex, this_ethread());
        if (lock.is_locked()) {
          ink_hrtime now = Thread::get_hrtime();
          now            = ink_hrtime_to_sec(now);
          theCongestionDB->RunTodoList(CDBC_pid);
          Iter it;
          CongestionEntry *pEntry = theCongestionDB->first_entry(CDBC_pid, &it);
          while (pEntry) {
            if (!pEntry->usefulInfo(now)) {
              theCongestionDB->remove_entry(CDBC_pid, &it);
              pEntry->put();
              pEntry = theCongestionDB->cur_entry(CDBC_pid, &it);
            }
          }
        } else {
          Debug("congestion_db", "flush gc missed the lock [%d], retry", CDBC_pid);
          return EVENT_CONT;
        }
      }
    }
  }
Ldone:
  CDBC_pid = 0;
  return EVENT_DONE;
}

int
CongestionDBCont::get_congest_list(int /* event ATS_UNUSED */, Event *e)
{
  if (m_action.cancelled) {
    Free_CongestionDBCont(this);
    return EVENT_DONE;
  }
  for (; CDBC_pid < theCongestionDB->getSize(); CDBC_pid++) {
    ProxyMutex *bucket_mutex = theCongestionDB->lock_for_key(CDBC_pid);
    {
      MUTEX_TRY_LOCK(lock_bucket, bucket_mutex, this_ethread());
      if (!lock_bucket.is_locked()) {
        e->schedule_in(SCHEDULE_CONGEST_CONT_INTERVAL);
        return EVENT_CONT;
      } else {
        theCongestionDB->RunTodoList(CDBC_pid);
        char buf[1024];
        Iter it;
        int len;
        CongestionEntry *pEntry = theCongestionDB->first_entry(CDBC_pid, &it);
        while (pEntry) {
          if ((pEntry->congested() && pEntry->pRecord->max_connection != 0) || CDBC_lf > 10) {
            len = pEntry->sprint(buf, 1024, CDBC_lf);
            CDBC_buf->write(buf, len);
          }
          pEntry = theCongestionDB->next_entry(CDBC_pid, &it);
        }
      }
    }
  }

  /* handle event done */
  m_action.continuation->handleEvent(CONGESTION_EVENT_CONGESTED_LIST_DONE, NULL);
  Free_CongestionDBCont(this);
  return EVENT_DONE;
}

int
CongestionDBCont::get_congest_entry(int /* event ATS_UNUSED */, Event *e)
{
  Debug("congestion_control", "cont::get_congest_entry started");

  if (m_action.cancelled) {
    Debug("congestion_cont", "action cancelled for %p", this);
    Free_CongestionDBCont(this);
    Debug("congestion_control", "cont::get_congest_entry state machine canceled");
    return EVENT_DONE;
  }
  ProxyMutex *bucket_mutex = theCongestionDB->lock_for_key(CDBC_key);
  MUTEX_TRY_LOCK(lock_bucket, bucket_mutex, this_ethread());
  if (lock_bucket.is_locked()) {
    theCongestionDB->RunTodoList(theCongestionDB->part_num(CDBC_key));
    *CDBC_ppE = theCongestionDB->lookup_entry(CDBC_key);
    if (*CDBC_ppE != NULL) {
      CDBC_rule->put();
      (*CDBC_ppE)->get();
      Debug("congestion_control", "cont::get_congest_entry entry found");
      m_action.continuation->handleEvent(CONGESTION_EVENT_CONTROL_LOOKUP_DONE, NULL);
    } else {
      /* create a new entry and add it to the congestDB */
      *CDBC_ppE = new CongestionEntry(CDBC_host, &CDBC_ip.sa, CDBC_rule, CDBC_key);
      CDBC_rule->put();
      (*CDBC_ppE)->get();
      theCongestionDB->insert_entry(CDBC_key, *CDBC_ppE);
      Debug("congestion_control", "cont::get_congest_entry new entry created");
      m_action.continuation->handleEvent(CONGESTION_EVENT_CONTROL_LOOKUP_DONE, NULL);
    }
    Free_CongestionDBCont(this);
    return EVENT_DONE;
  } else {
    Debug("congestion_control", "cont::get_congest_entry MUTEX_TRY_LOCK failed");
    e->schedule_in(SCHEDULE_CONGEST_CONT_INTERVAL);
    return EVENT_CONT;
  }
}

//-----------------------------------------------------------------
//  Global fuctions implementation
//-----------------------------------------------------------------

void
initCongestionDB()
{
  if (theCongestionDB == NULL) {
    theCongestionDB = new CongestionDB(CONGESTION_DB_SIZE / MT_HASHTABLE_PARTITIONS);
  }
}

void
revalidateCongestionDB()
{
  ProxyMutex *bucket_mutex;
  if (theCongestionDB == NULL) {
    theCongestionDB = new CongestionDB(CONGESTION_DB_SIZE / MT_HASHTABLE_PARTITIONS);
    return;
  }
  Debug("congestion_config", "congestion control revalidating CongestionDB");
  for (int i = 0; i < theCongestionDB->getSize(); i++) {
    bucket_mutex = theCongestionDB->lock_for_key(i);
    {
      MUTEX_TRY_LOCK(lock_bucket, bucket_mutex, this_ethread());
      if (lock_bucket.is_locked()) {
        theCongestionDB->RunTodoList(i);
        theCongestionDB->revalidateBucket(i);
      } else {
        CongestRequestParam *param = CongestRequestParamAllocator.alloc();
        param->m_op                = CongestRequestParam::REVALIDATE_BUCKET;
        ink_atomiclist_push(&theCongestionDB->todo_lists[i], param);
      }
    }
  }
  Debug("congestion_config", "congestion control revalidating CongestionDB Done");
}

Action *
get_congest_entry(Continuation *cont, HttpRequestData *data, CongestionEntry **ppEntry)
{
  if (congestionControlEnabled != 1 && congestionControlEnabled != 2)
    return ACTION_RESULT_DONE;
  Debug("congestion_control", "congestion control get_congest_entry start");

  CongestionControlRecord *p = CongestionControlled(data);
  Debug("congestion_control", "Control Matcher matched rule_num %d", p == NULL ? -1 : p->line_num);
  if (p == NULL)
    return ACTION_RESULT_DONE;
  // if the fail_window <= 0 and the max_connection == -1, then no congestion control
  if (p->max_connection_failures <= 0 && p->max_connection < 0) {
    return ACTION_RESULT_DONE;
  }
  uint64_t key = make_key((char *)data->get_host(), data->get_ip(), p);
  Debug("congestion_control", "Key = %" PRIu64 "", key);

  ProxyMutex *bucket_mutex = theCongestionDB->lock_for_key(key);
  MUTEX_TRY_LOCK(lock_bucket, bucket_mutex, this_ethread());
  if (lock_bucket.is_locked()) {
    theCongestionDB->RunTodoList(theCongestionDB->part_num(key));
    *ppEntry = theCongestionDB->lookup_entry(key);
    if (*ppEntry != NULL) {
      (*ppEntry)->get();
      Debug("congestion_control", "get_congest_entry, found entry %p done", (void *)*ppEntry);
      return ACTION_RESULT_DONE;
    } else {
      // create a new entry and add it to the congestDB
      *ppEntry = new CongestionEntry(data->get_host(), data->get_ip(), p, key);
      (*ppEntry)->get();
      theCongestionDB->insert_entry(key, *ppEntry);
      Debug("congestion_control", "get_congest_entry, new entry %p done", (void *)*ppEntry);
      return ACTION_RESULT_DONE;
    }
  } else {
    Debug("congestion_control", "get_congest_entry, trylock failed, schedule cont");
    CongestionDBCont *Ccont = CongestionDBContAllocator.alloc();
    Ccont->m_action         = cont;
    Ccont->mutex            = cont->mutex;
    Ccont->CDBC_key         = key;
    Ccont->CDBC_host        = (char *)data->get_host();
    ats_ip_copy(&Ccont->CDBC_ip.sa, data->get_ip());
    p->get();
    Ccont->CDBC_rule = p;
    Ccont->CDBC_ppE  = ppEntry;

    SET_CONTINUATION_HANDLER(Ccont, &CongestionDBCont::get_congest_entry);
    eventProcessor.schedule_in(Ccont, SCHEDULE_CONGEST_CONT_INTERVAL, ET_NET);
    return &Ccont->m_action;
  }
}

Action *
get_congest_list(Continuation *cont, MIOBuffer *buffer, int format)
{
  if (theCongestionDB == NULL || (congestionControlEnabled != 1 && congestionControlEnabled != 2))
    return ACTION_RESULT_DONE;
  for (int i = 0; i < theCongestionDB->getSize(); i++) {
    ProxyMutex *bucket_mutex = theCongestionDB->lock_for_key(i);
    {
      MUTEX_TRY_LOCK(lock_bucket, bucket_mutex, this_ethread());
      if (lock_bucket.is_locked()) {
        theCongestionDB->RunTodoList(i);
        char buf[1024];
        Iter it;
        int len;
        CongestionEntry *pEntry = theCongestionDB->first_entry(i, &it);
        while (pEntry) {
          if ((pEntry->congested() && pEntry->pRecord->max_connection != 0) || format > 10) {
            len = pEntry->sprint(buf, 1024, format);
            buffer->write(buf, len);
          }
          pEntry = theCongestionDB->next_entry(i, &it);
        }
      } else {
        /* we did not get the lock, schedule it */
        CongestionDBCont *CCcont = CongestionDBContAllocator.alloc();
        CCcont->CDBC_pid         = i;
        CCcont->CDBC_buf         = buffer;
        CCcont->m_action         = cont;
        CCcont->mutex            = cont->mutex;
        CCcont->CDBC_lf          = format;
        SET_CONTINUATION_HANDLER(CCcont, &CongestionDBCont::get_congest_list);
        eventProcessor.schedule_in(CCcont, SCHEDULE_CONGEST_CONT_INTERVAL, ET_NET);
        return &CCcont->m_action;
      }
    }
  }
  return ACTION_RESULT_DONE;
}

/*
 * this function is to suport removing the congested state for a
 * specific server when the administrator knows it is online again
 */

void
remove_all_congested_entry()
{
  if (theCongestionDB != NULL) {
    theCongestionDB->removeAllRecords();
  }
}

void
remove_congested_entry(uint64_t key)
{
  if (theCongestionDB != NULL) {
    theCongestionDB->removeRecord(key);
  }
}

//--------------------------------------------------------------
//  remove_congested_entry(char* buf, MIOBuffer *out_buffer)
//   INPUT: buf
//         format: "all",
//                 "host=<hostname>[/<prefix>]",
//                 "ip=<ip addr>[/<prefix>]",
//                 "key=<internal key>"
//   OUTPUT: out_buffer
//           message to the Raf
//--------------------------------------------------------------
void
remove_congested_entry(char *buf, MIOBuffer *out_buffer)
{
  const int MSG_LEN = 512;
  char msg[MSG_LEN + 1];
  int len = 0;
  uint64_t key;
  if (strcasecmp(buf, "all") == 0) {
    remove_all_congested_entry();
    len = snprintf(msg, MSG_LEN, "all entries in congestion control table removed\n");
    // coverity[secure_coding]
  } else if (sscanf(buf, "key=%" PRIu64 "", &key) == 1) {
    remove_congested_entry(key);
    len = snprintf(msg, MSG_LEN, "key %" PRIu64 " removed\n", key);
  } else if (strncasecmp(buf, "host=", 5) == 0) {
    char *p      = buf + 5;
    char *prefix = strchr(p, '/');
    int prelen   = 0;
    if (prefix) {
      *prefix = '\0';
      prefix++;
      prelen = strlen(prefix);
    }
    key = make_key(p, strlen(p), 0, prefix, prelen);
    remove_congested_entry(key);
    len = snprintf(msg, MSG_LEN, "host=%s prefix=%s removed\n", p, prefix ? prefix : "(nil)");
  } else if (strncasecmp(buf, "ip=", 3) == 0) {
    IpEndpoint ip;
    memset(&ip, 0, sizeof(ip));

    char *p      = buf + 3;
    char *prefix = strchr(p, '/');
    int prelen   = 0;
    if (prefix) {
      *prefix = '\0';
      prefix++;
      prelen = strlen(prefix);
    }
    ats_ip_pton(p, &ip);
    if (!ats_is_ip(&ip)) {
      len = snprintf(msg, MSG_LEN, "invalid ip: %s\n", buf);
    } else {
      key = make_key(NULL, 0, &ip.sa, prefix, prelen);
      remove_congested_entry(key);
      len = snprintf(msg, MSG_LEN, "ip=%s prefix=%s removed\n", p, prefix ? prefix : "(nil)");
    }
  }
  out_buffer->write(msg, len);
}
