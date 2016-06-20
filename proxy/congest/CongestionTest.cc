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
 *  CongestionTest.cc - Regression Test of the congestion control module
 *
 *
 ****************************************************************************/
#include "ts/ink_platform.h"
#include <math.h>
#include "Main.h"
#include "CongestionDB.h"
#include "Congestion.h"
#include "Error.h"

//-------------------------------------------------------------
// Test the HashTable implementation
//-------------------------------------------------------------
/* all of the elements inserted into the HashTable should be in the
 * table and can be easily retrived
 * also exercise the resizing of the table
 */
EXCLUSIVE_REGRESSION_TEST(Congestion_HashTable)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  MTHashTable<long, long> *htable = new MTHashTable<long, long>(4);
  // add elements to the table;
  long i, count = 1 * 1024 * 1024;
  rprintf(t, "adding data into the hash table .", count);
  for (i = 1; i <= count; i++) {
    htable->insert_entry(i, i);
    if (i % (count / 50) == 0)
      fprintf(stderr, ".");
  }
  fprintf(stderr, "done\n");
  rprintf(t, "%d data added into the hash table\n", count);
  rprintf(t, "verifying the content");
  for (i = 1; i <= count; i++) {
    long data = htable->lookup_entry(i);
    if (i % (count / 50) == 0)
      fprintf(stderr, ".");
    if (data != i) {
      rprintf(t, "verify content failed: key(%d) data(%d)\n", i, data);
      *pstatus = REGRESSION_TEST_FAILED;
      return;
    }
  }
  fprintf(stderr, "done\n");
  long removed_count = 0;
  // delete some data
  rprintf(t, "removing data.");
  for (i = 1; i < count / 2; i++) {
    htable->remove_entry(i * 2);
    if (i % (count / 50) == 0)
      fprintf(stderr, ".");
    removed_count++;
  }
  fprintf(stderr, "done\n");

  rprintf(t, "%d data entries are removed\n", removed_count);
  rprintf(t, "verify the content again");
  for (i = 1; i <= count; i++) {
    long data = htable->lookup_entry(i);
    if (i % 2 == 1 && data == 0) {
      rprintf(t, "verify content failed: key(%d) deleted\n", i);
      *pstatus = REGRESSION_TEST_FAILED;
      delete htable;
      return;
    }
    if (data != 0 && data != i) {
      rprintf(t, "verify content failed: key(%d) data(%d)\n", i, data);
      *pstatus = REGRESSION_TEST_FAILED;
      delete htable;
      return;
    }
    if (i % (count / 50) == 0)
      fprintf(stderr, ".");
  }
  fprintf(stderr, "done\n");

  rprintf(t, "use iterator to list all the elements and delete half of them");
  HashTableIteratorState<long, long> it;
  int j, new_count = 0;
  for (j = 0; j < MT_HASHTABLE_PARTITIONS; j++) {
    int data = htable->first_entry(j, &it);
    while (data > 0) {
      new_count++;
      if (new_count % (count / 25) == 0)
        fprintf(stderr, ".");

      if (new_count % 2 == 0) {
        htable->remove_entry(j, &it);
        data = htable->cur_entry(j, &it);
        removed_count++;
      } else
        data = htable->next_entry(j, &it);
    }
  }
  fprintf(stderr, "done\n");

  rprintf(t, "verify the content once again");
  new_count = count - removed_count;
  for (j = 0; j < MT_HASHTABLE_PARTITIONS; j++) {
    int data = htable->first_entry(j, &it);
    while (data > 0) {
      new_count--;
      if (new_count % (count / 25) == 0)
        fprintf(stderr, ".");
      data = htable->next_entry(j, &it);
      if (data != htable->lookup_entry(data)) {
        rprintf(t, "verify content failed: key(%d) data(%d)\n", data, htable->lookup_entry(data));
        *pstatus = REGRESSION_TEST_FAILED;
        delete htable;
        return;
      }
    }
  }

  fprintf(stderr, "done\n");
  if (new_count != 0) {
    rprintf(t, "there are %d extra entries in the table\n", new_count);
    *pstatus = REGRESSION_TEST_FAILED;
    delete htable;
    return;
  }

  rprintf(t, "remove everything using iterator");
  new_count = count - removed_count;
  for (j = 0; j < MT_HASHTABLE_PARTITIONS; j++) {
    int data = htable->first_entry(j, &it);
    while (data > 0) {
      new_count--;
      if (new_count % (count / 25) == 0)
        fprintf(stderr, ".");
      htable->remove_entry(j, &it);
      data = htable->cur_entry(j, &it);
    }
  }

  fprintf(stderr, "done\n");
  if (new_count != 0) {
    rprintf(t, "there are %d extra entries in the table\n", new_count);
    *pstatus = REGRESSION_TEST_FAILED;
    delete htable;
    return;
  }

  delete htable;
  *pstatus = REGRESSION_TEST_PASSED;
}

//-------------------------------------------------------------
// Test the FailHistory implementation
//-------------------------------------------------------------
/* register events into the FailHistory and the number of events
 * should be correct
 */
struct CCFailHistoryTestCont : public Continuation {
  enum {
    FAIL_WINDOW = 300,
  };
  enum {
    SIMPLE_TEST,
    MULTIPLE_THREAD_TEST,
    ROTATING_TEST,
  };
  int test_mode;
  int final_status;
  bool complete;
  RegressionTest *test;
  int mainEvent(int event, Event *e);
  CCFailHistoryTestCont()
    : Continuation(new_ProxyMutex()), test_mode(SIMPLE_TEST), final_status(0), complete(false), failEvents(NULL), entry(NULL)
  {
  }

  CCFailHistoryTestCont(Ptr<ProxyMutex> _mutex, RegressionTest *_test)
    : Continuation(_mutex),
      test_mode(SIMPLE_TEST),
      final_status(REGRESSION_TEST_PASSED),
      complete(false),
      test(_test),
      failEvents(NULL),
      pending_action(NULL)
  {
    SET_HANDLER(&CCFailHistoryTestCont::mainEvent);
    rule                          = new CongestionControlRecord;
    rule->fail_window             = FAIL_WINDOW;
    rule->max_connection_failures = 10;
    rule->pRecord                 = new CongestionControlRecord(*rule);
    entry                         = new CongestionEntry("dummy_host", 0, rule->pRecord, 0);
  }

  ~CCFailHistoryTestCont()
  {
    if (pending_action) {
      pending_action->cancel();
    }
    entry->put();
    delete rule;
    clear_events();
  }

  void init_events();
  void clear_events();
  int check_history(bool print);
  int schedule_event(int event, Event *e);

  struct FailEvents {
    time_t time;
    Link<FailEvents> link;
  };
  InkAtomicList *failEvents;
  CongestionControlRecord *rule;
  CongestionEntry *entry;
  Action *pending_action;
};

void
CCFailHistoryTestCont::clear_events()
{
  if (failEvents) {
    CCFailHistoryTestCont::FailEvents *events = (CCFailHistoryTestCont::FailEvents *)ink_atomiclist_popall(failEvents);
    while (events != NULL) {
      CCFailHistoryTestCont::FailEvents *next = events->link.next;
      delete events;
      events = next;
    }
    delete failEvents;
    failEvents = NULL;
  }
}

void
CCFailHistoryTestCont::init_events()
{
  clear_events();

  failEvents = new InkAtomicList;
  ink_atomiclist_init(failEvents, "failEvents", (uintptr_t) & ((CCFailHistoryTestCont::FailEvents *)0)->link);

  int i, j;
  CCFailHistoryTestCont::FailEvents *new_event = NULL;

  switch (test_mode) {
  case CCFailHistoryTestCont::ROTATING_TEST:
    for (i = 0; i < 16384; i++) {
      for (j = 0; j < 10; j++) {
        new_event = new CCFailHistoryTestCont::FailEvents;
        // coverity[secure_coding]
        new_event->time = rand() % (FAIL_WINDOW) + j * FAIL_WINDOW;
        ink_atomiclist_push(failEvents, new_event);
      }
    }
    break;
  case CCFailHistoryTestCont::SIMPLE_TEST:
  default:
    for (i = 0; i < 65536; i++) {
      new_event = new CCFailHistoryTestCont::FailEvents;
      // coverity[secure_coding]
      new_event->time = rand() % FAIL_WINDOW;
      ink_atomiclist_push(failEvents, new_event);
    }
  }
}

int
CCFailHistoryTestCont::schedule_event(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (failEvents == NULL)
    return EVENT_DONE;
  CCFailHistoryTestCont::FailEvents *f = (CCFailHistoryTestCont::FailEvents *)ink_atomiclist_pop(failEvents);
  if (f != NULL) {
    entry->failed_at(f->time);
    delete f;
    return EVENT_CONT;
  }
  return EVENT_DONE;
}

int
CCFailHistoryTestCont::check_history(bool print)
{
  if (print) {
    rprintf(test, "Verify the result\n");
    rprintf(test, "Content of history\n");
    int e = 0;
    for (int i = 0; i < CONG_HIST_ENTRIES; i++) {
      e += entry->m_history.bins[i];
      rprintf(test, "bucket %d => events %d , sum = %d\n", i, entry->m_history.bins[i], e);
    }
    fprintf(stderr, "Events: %d, CurIndex: %d, LastEvent: %ld, HistLen: %d, BinLen: %d, Start: %ld\n", entry->m_history.events,
            entry->m_history.cur_index, entry->m_history.last_event, entry->m_history.length, entry->m_history.bin_len,
            entry->m_history.start);
    char buf[1024];
    entry->sprint(buf, 1024, 10);
    rprintf(test, "%s", buf);
  }
  if (test_mode == CCFailHistoryTestCont::SIMPLE_TEST && entry->m_history.events == 65536)
    return 0;
  return 0;
}

int
CCFailHistoryTestCont::mainEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  test_mode = CCFailHistoryTestCont::SIMPLE_TEST;
  init_events();
  entry->init(rule->pRecord);
  while (schedule_event(0, NULL) == EVENT_CONT)
    ;
  if (check_history(true) == 0) {
    final_status = REGRESSION_TEST_PASSED;
  } else {
    final_status = REGRESSION_TEST_FAILED;
    goto Ldone;
  }

  test_mode = CCFailHistoryTestCont::ROTATING_TEST;
  init_events();
  entry->init(rule->pRecord);
  while (schedule_event(0, NULL) == EVENT_CONT)
    ;
  if (check_history(true) == 0) {
    final_status = REGRESSION_TEST_PASSED;
  } else {
    final_status = REGRESSION_TEST_FAILED;
    goto Ldone;
  }

Ldone:
  complete = true;
  if (complete) {
    test->status = final_status;
    delete this;
    return EVENT_DONE;
  }
  return EVENT_CONT;
}

EXCLUSIVE_REGRESSION_TEST(Congestion_FailHistory)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  CCFailHistoryTestCont *test = new CCFailHistoryTestCont(make_ptr(new_ProxyMutex()), t);
  eventProcessor.schedule_in(test, HRTIME_SECONDS(1));
  *pstatus = REGRESSION_TEST_INPROGRESS;
}

//-------------------------------------------------------------
// Test the CongestionDB implementation
//-------------------------------------------------------------
/* Insert simulated CongestionEntry into the CongestionDB and
 * exercise the GC of the DB, remove entries from DB
 */

struct CCCongestionDBTestCont : public Continuation {
  int final_status;
  bool complete;
  RegressionTest *test;

  int mainEvent(int event, Event *e);

  void init();
  int get_congest_list();
  CongestionControlRecord *rule;
  CongestionDB *db;
  int dbsize;
  CongestionEntry *gen_CongestionEntry(sockaddr const *ip, int congested = 0);

  CCCongestionDBTestCont(Ptr<ProxyMutex> _mutex, RegressionTest *_test)
    : Continuation(_mutex), final_status(REGRESSION_TEST_PASSED), complete(false), test(_test), rule(NULL), db(NULL), dbsize(1024)
  {
    SET_HANDLER(&CCCongestionDBTestCont::mainEvent);
  }
  virtual ~CCCongestionDBTestCont()
  {
    if (db) {
      db->removeAllRecords();
      delete db;
    }
    if (rule)
      delete rule;
  }
};

CongestionEntry *
CCCongestionDBTestCont::gen_CongestionEntry(sockaddr const *ip, int congested)
{
  char hostname[INET6_ADDRSTRLEN];
  uint64_t key;
  ats_ip_ntop(ip, hostname, sizeof(hostname));
  key                  = make_key(hostname, strlen(hostname), ip, rule->pRecord);
  CongestionEntry *ret = new CongestionEntry(hostname, ip, rule->pRecord, key);
  ret->m_congested     = congested;
  ret->m_ref_count     = 0;
  return ret;
}

void
CCCongestionDBTestCont::init()
{
  // create/clear db
  if (!db)
    db = new CongestionDB(dbsize / MT_HASHTABLE_PARTITIONS);
  else
    db->removeAllRecords();
  if (!rule) {
    rule                          = new CongestionControlRecord;
    rule->fail_window             = 300;
    rule->max_connection_failures = 10;
    rule->pRecord                 = new CongestionControlRecord(*rule);
  }
}

int
CCCongestionDBTestCont::get_congest_list()
{
  int cnt = 0;
  if (db == NULL)
    return 0;
  for (int i = 0; i < db->getSize(); i++) {
    db->RunTodoList(i);
    char buf[1024];
    Iter it;

    CongestionEntry *pEntry = db->first_entry(i, &it);
    while (pEntry) {
      cnt++;
      if (cnt % 100 == 0) {
        pEntry->sprint(buf, 1024, 100);
        fprintf(stderr, "%s", buf);
      }
      pEntry = db->next_entry(i, &it);
    }
  }
  return cnt;
}

int
CCCongestionDBTestCont::mainEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  int to_add = 1 * 1024 * 1024;
  int i;
  int items[10] = {0};
  init();
  rprintf(test, "Add %d records into the db", dbsize);

  for (i = 0; i < dbsize; i++) {
    if (i % (dbsize / 25) == 0)
      fprintf(stderr, ".");

    IpEndpoint ip;
    ats_ip4_set(&ip, i + 255);

    CongestionEntry *tmp = gen_CongestionEntry(&ip.sa);
    db->addRecord(tmp->m_key, tmp);
  }
  fprintf(stderr, "done\n");

  items[0] = get_congest_list();

  db->removeAllRecords();

  rprintf(test, "There are %d records in the db\n", items[0]);

  rprintf(test, "Add %d records into the db", to_add);
  for (i = 0; i < to_add; i++) {
    if (i % (to_add / 25) == 0)
      fprintf(stderr, ".");

    IpEndpoint ip;
    ats_ip4_set(&ip, i + 255);
    CongestionEntry *tmp = gen_CongestionEntry(&ip.sa);
    db->addRecord(tmp->m_key, tmp);
  }

  items[1] = get_congest_list();

  db->removeAllRecords();

  rprintf(test, "There are %d records in the db\n", items[1]);

  rprintf(test, "Add %d congested records into the db", to_add);

  for (i = 0; i < to_add; i++) {
    if (i % (to_add / 25) == 0)
      fprintf(stderr, ".");

    IpEndpoint ip;
    ats_ip4_set(&ip, i + 255);

    CongestionEntry *tmp = gen_CongestionEntry(&ip.sa, 1);
    db->addRecord(tmp->m_key, tmp);
  }
  items[2] = get_congest_list();
  rprintf(test, "There are %d records in the db\n", items[2]);

  db->removeAllRecords();

  for (i = 0; i < 3; i++) {
    rprintf(test, "After test [%d] there are %d records in the db\n", i + 1, items[i]);
  }

  complete = true;
  if (complete) {
    test->status = final_status;
    delete this;
    return EVENT_DONE;
  }
  return EVENT_CONT;
}

EXCLUSIVE_REGRESSION_TEST(Congestion_CongestionDB)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  CCCongestionDBTestCont *test = new CCCongestionDBTestCont(make_ptr(new_ProxyMutex()), t);
  eventProcessor.schedule_in(test, HRTIME_SECONDS(1));
  *pstatus = REGRESSION_TEST_INPROGRESS;
}

//-------------------------------------------------------------
// Test the CongestionControl implementation
//-------------------------------------------------------------
/* test the whole thing
 * 1. Match rules
 * 2. Apply new rules
 */
void
init_CongestionRegressionTest()
{
  (void)regressionTest_Congestion_HashTable;
  (void)regressionTest_Congestion_FailHistory;
  (void)regressionTest_Congestion_CongestionDB;
}
