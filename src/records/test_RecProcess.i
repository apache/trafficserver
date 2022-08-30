/** @file

  A small test and sample program for librecords_p.a

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

#include "ts/ink_hrtime.h"
#include "P_RecUtils.h"
#include "test_RecordsConfig.h"

Diags *diags = NULL;

void RecDumpRecordsHt(RecT rec_type);

//-------------------------------------------------------------------------
// Test01: Parse Tests
//
// The following test just verifies that we can parse the
// 'records.config' file format correctly (e.g can we handle weird
// spacing, malformed lines, etc).  The test also verifies some of the
// basic RecGetRecord functionality.
//
// Run this test with the 'test_records.config' config file.  Note
// that the configs used by this test are registered in the
// 'test_RecordsConfig.cc' file.
//-------------------------------------------------------------------------

#define PARSE_TEST_UNAVAILABLE(name, failures) \
  do { \
    RecString rec_string = 0; \
    if (RecGetRecordString_Xmalloc("proxy.config.parse_"name, &rec_string) != REC_ERR_FAIL) { \
      ats_free(rec_string); \
      printf("  parse_"name": FAIL\n"); \
      failures++; \
    } else { \
      printf("  parse_"name": PASS\n"); \
    } \
  } while (0);

#define PARSE_TEST_COMPARE(name, value, failures) \
  do { \
    RecString rec_string = 0; \
    if (RecGetRecordString_Xmalloc("proxy.config.parse_"name, &rec_string) == REC_ERR_OKAY) { \
      if (strcmp(rec_string, value) == 0) { \
	printf("  parse_"name": PASS\n"); \
      } else { \
	printf("  parse_"name": FAIL\n"); \
	failures++; \
      } \
      ats_free(rec_string); \
    } else { \
      printf("  parse_"name": FAIL\n"); \
      failures++; \
    } \
  } while (0);

void
Test01()
{
  printf("[Test01: Parse Tests]\n");
  int failures = 0;

  // test 1 and 1b
  PARSE_TEST_UNAVAILABLE("test_1a", failures);
  PARSE_TEST_UNAVAILABLE("test_1b", failures);

  // test 2, 2b, 3, 3b, 4, 4b
  PARSE_TEST_COMPARE("test_2a", "X", failures);
  PARSE_TEST_COMPARE("test_2b", "X", failures);
  PARSE_TEST_COMPARE("test_3b", "XXX", failures);
  PARSE_TEST_COMPARE("test_3b", "XXX", failures);
  PARSE_TEST_COMPARE("test_4a", "XXX XXX XXX", failures);
  PARSE_TEST_COMPARE("test_4b", "XXX XXX XXX", failures);

  if (failures == 0) {
    printf("  SUMMARY: PASS\n");
  } else {
    printf("  SUMMARY: FAIL\n");
  }
  return;
}

//-------------------------------------------------------------------------
// Test02: Config Tests
//
// The following test stresses some additional config features
// (e.g. registration of config update callbacks, config linking, and
// config setting).  As with Test01, config registration must be done
// in 'test_RecordsConfig.cc'.
//-------------------------------------------------------------------------

bool g_config_update_result = false;

RecInt g_link_test_1 = 0;
RecFloat g_link_test_2 = 0.0f;
RecCounter g_link_test_3 = 0;

int
cb_test_1(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  if ((cookie == (void *) 0x12345678) && (strcmp(data.rec_string, "cb_test_1__changed") == 0)) {
    printf("    - cb_test_1 (name: %s, data: %s, cookie: 0x%x\n", name, data.rec_string, cookie);
    g_config_update_result = true;
  } else {
    g_config_update_result = false;
  }
  return REC_ERR_OKAY;
}

int
cb_test_2(const char */* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
          RecData /* data ATS_UNUSED */, void * /* cookie ATS_UNUSED */)
{
  g_config_update_result = false;
  return REC_ERR_FAIL;
}

void
Test02()
{
  printf("[Test02: Config Tests]\n");
  int failures = 0;

  printf("  [RecRegisterConfigUpdateCb]\n");

  // Initialize variables
  RecSetRecordString("proxy.config.cb_test_1", "cb_test_1__original");
  RecSetRecordString("proxy.config.cb_test_2", "cb_test_2__original");
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  ink_sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Register config update callbacks
  RecRegisterConfigUpdateCb("proxy.config.cb_test_1", cb_test_1, (void *) 0x12345678);
  RecRegisterConfigUpdateCb("proxy.config.cb_test_2", cb_test_2, (void *) 0x87654321);

  // Change proxy.config.cb_test_1
  RecSetRecordString("proxy.config.cb_test_1", "cb_test_1__changed");
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  ink_sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Check globals to make sure the right thing happened
  if (g_config_update_result == true) {
    printf("    SUMMARY: PASS\n");
  } else {
    printf("    SUMMARY: FAIL\n");
  }

  printf("  [RecLinkConfigXXX]\n");

  // Set configs
  RecSetRecordInt("proxy.config.link_test_1", 1);
  RecSetRecordFloat("proxy.config.link_test_2", 100.0f);
  RecSetRecordCounter("proxy.config.link_test_3", 5);
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  ink_sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Link configs
  RecLinkConfigInt("proxy.config.link_test_1", &g_link_test_1);
  RecLinkConfigFloat("proxy.config.link_test_2", &g_link_test_2);
  RecLinkConfigCounter("proxy.config.link_test_3", &g_link_test_3);

  // Initial check to make sure link worked
  printf("    - g_link_test_1 = %d:%d, expect: 1\n", g_link_test_1);
  printf("    - g_link_test_2 = %f, expect: %f\n", g_link_test_2, 100.0f);
  printf("    - g_link_test_3 = %d:%d, expect: 5\n", g_link_test_3);
  if (g_link_test_1 == 1 && g_link_test_2 == 100.0f && g_link_test_3 == 5) {
    printf("    SUMMARY: PASS\n");
  } else {
    printf("    SUMMARY: FAIL\n");
  }

  printf("  [RecGetRecordXXX]\n");

  RecString rec_string = 0;
  const int buf_len = 1024;
  char buf[buf_len];

  RecGetRecordString_Xmalloc("proxy.config.cb_test_2", &rec_string);
  if (!rec_string || (rec_string && strcmp(rec_string, "cb_test_2__original")) != 0) {
    printf("    RecGetRecordString_Xmalloc: FAIL (expected: 'cb_test_2__original', got: '%s')\n",
           rec_string ? rec_string : "<nothing>");
  } else {
    printf("    RecGetRecordString_Xmalloc: PASS (%s)\n", rec_string);
  }

  RecGetRecordString("proxy.config.cb_test_2", buf, buf_len);
  if (strcmp(buf, "cb_test_2__original") != 0) {
    printf("    RecGetRecordString:         FAIL (expected: 'cb_test_2__original', got: '%s')\n", buf);
  } else {
    printf("    RecGetRecordString:         PASS (%s)\n", buf);
  }

  // Testing with RecGetRecordInt, RecGetRecordFloat and RecGetRecordCounter
  RecInt rec_int = 0;
  RecGetRecordInt("proxy.config.link_test_1", &rec_int);
  if (rec_int != 1) {
    printf("    RecGetRecordInt:            FAIL (expected: 1, got %d:%d)\n", rec_int);
  } else {
    printf("    RecGetRecordInt:            PASS (%d:%d)\n", rec_int);
  }

  RecFloat rec_float = 0;
  RecGetRecordFloat("proxy.config.link_test_2", &rec_float);
  if (rec_float != 100.0f) {
    printf("    RecGetRecordFloat:          FAIL (expected: %f, got %f)\n", 100.0f, rec_float);
  } else {
    printf("    RecGetRecordFloat:          PASS (%f)\n", rec_float);
  }

  RecCounter rec_counter = 0;
  RecGetRecordCounter("proxy.config.link_test_3", &rec_counter);
  if (rec_counter != 5) {
    printf("    RecGetRecordCounter:        FAIL (expected: 5, got %d:%d)\n", rec_counter);
  } else {
    printf("    RecGetRecordCounter:        PASS (%d:%d)\n", rec_counter);
  }

  // Testing RecLinkConfigXXX, after calling RecLinkConfigXXX above, those
  // variable will automatically be atomically updated when record changes in
  // librecords.
  printf("  [RecLinkConfigXXX]\n");

  // Set the records
  printf("    - RecSetRecordXXX\n");
  RecSetRecordString("proxy.config.cb_test_1", "cb_test_1_changed");
  RecSetRecordInt("proxy.config.link_test_1", 2);
  RecSetRecordFloat("proxy.config.link_test_2", 200.0f);
  RecSetRecordCounter("proxy.config.link_test_3", 6);
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  ink_sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  printf("    - g_link_test_1 = %d:%d, expect: 2\n", g_link_test_1);
  printf("    - g_link_test_2 = %f, expect: %f\n", g_link_test_2, 200.0f);
  printf("    - g_link_test_3 = %d:%d, expect: 6\n", g_link_test_3);
  if (g_link_test_1 == 2 && g_link_test_2 == 200.0f && g_link_test_3 == 6) {
    printf("    SUMMARY: PASS\n");
  } else {
    printf("    SUMMARY: FAIL\n");
  }

  RecSetRecordInt("proxy.config.link_test_1", 1);
  RecSetRecordFloat("proxy.config.link_test_2", 100.0f);
  RecSetRecordCounter("proxy.config.link_test_3", 5);
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  ink_sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

}

//-------------------------------------------------------------------------
// Test03: RawStat Tests
//
// The following test illustrates how one might use the RawStat
// interface to librecprocess.a.  It also illustrates a custom RawStat
// sync function, 'raw_stat_sync_ticks_per_sec' that computes
// operations per second (used by AIO module).
//-------------------------------------------------------------------------

enum my_stat_enum
{
  MY_STAT_A,
  MY_STAT_B,
  MY_STAT_C,
  MY_STAT_D,
  MY_STAT_E,
  MY_STAT_F,
  MY_STAT_G,
  MY_STAT_COUNT
};

static RecRawStatBlock *g_rsb = NULL;
static int g_count = 0;

static int g_ticks = 0;
static int g_time = 0;

int
raw_stat_sync_ticks_per_sec(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id)
{
  ink64 ticks_old, time_old;
  ink64 ticks_new, time_new;

  RecRawStat *rrs = RecGetGlobalRawStatPtr(rsb, id);
  ink64 *rrs_sum = RecGetGlobalRawStatSumPtr(rsb, id);
  ink64 *rrs_count = RecGetGlobalRawStatCountPtr(rsb, id);

  RecGetGlobalRawStatSum(rsb, id, &ticks_old);
  RecGetGlobalRawStatCount(rsb, id, &time_old);

  if ((rrs->sum != ticks_old) && (*rrs_sum != ticks_old)) {
    printf("ERROR: (rrs->sum != ticks_old) && (*rrs_sum != ticks_old)\n");
  }
  /*else {
     printf("OKAY: GlobalRawStatSum == RecRawStat->sum == && GlobalRawStatSum == GlobalRawStatSumPtr, which is %d:%d\n", ticks_old);
     } */
  if ((rrs->count != time_old) && (*rrs_count != time_old)) {
    printf("ERROR: (rrs->count != time_old) && (*rrs_sum != ticks_old)\n");
  }
  /*else {
     printf("OKAY: GlobalRawStatCount == RecRawStat->count && GlobalRawStatCount == GlobalRawStatCountPtr, which is %d:%d\n", time_old);
     } */
  ticks_new = g_ticks;
  time_new = g_time;

  data->rec_float = (float) (ticks_new - ticks_old) / (float) (time_new - time_old);

  RecSetGlobalRawStatSum(rsb, id, ticks_new);
  RecSetGlobalRawStatCount(rsb, id, time_new);

  return REC_ERR_OKAY;

}

struct RawStatCont:public Continuation
{
  RawStatCont(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&RawStatCont::dummy_function);
  }
  int dummy_function(int event, Event * e)
  {
    printf("------------Raw Stat dump-------------\n");
    ink64 hr_start, hr_finish;
    // comments out here. Why stat_a is int?
    RecInt stat_b, stat_c, stat_f, stat_g;
    RecFloat stat_a, stat_d, stat_e;
    // comments out here

    hr_start = ink_get_hrtime();

    // test_raw_stat_a should have around 16000 in it (avg of rand())
    RecIncrRawStat(g_rsb, mutex->thread_holding, (int) MY_STAT_A, rand());

    // test_raw_stat_b should have g_count plustorial in it
    RecIncrRawStatSum(g_rsb, mutex->thread_holding, (int) MY_STAT_B, g_count);

    // test_raw_stat_c should have g_count plustorial in it
    //RecSetRawStatCount(g_rsb, (int) MY_STAT_C, g_count);
    RecIncrRawStatCount(g_rsb, mutex->thread_holding, (int) MY_STAT_C, g_count);

    // test_raw_stat_f should have g_count in it
    // I have switched this with test_raw_stat_c
    RecSetRawStatCount(g_rsb, (int) MY_STAT_F, g_count);

    // test_raw_stat_g should have g_count in it
    RecSetRawStatSum(g_rsb, (int) MY_STAT_G, g_count);

    // test_raw_stat_d should have 4 it (e.g. we're run 4 times a second)
    ink_atomic_increment(&g_ticks, 1);
    g_time = time(0);

    // sleep for a bit to take some time
    struct timespec rgtp;
    rgtp.tv_sec = 0;
    rgtp.tv_nsec = 10000;
    nanosleep(&rgtp, NULL);

    // FIXME: Read values and compare against expected values rather
    // than just printing out

    // comments out here
    RecGetRecordFloat("proxy.process.test_raw_stat_a", &stat_a);
    RecGetRecordInt("proxy.process.test_raw_stat_b", &stat_b);
    RecGetRecordInt("proxy.process.test_raw_stat_c", &stat_c);
    RecGetRecordFloat("proxy.process.test_raw_stat_d", &stat_d);
    RecGetRecordFloat("proxy.process.test_raw_stat_e", &stat_e);
    RecGetRecordInt("proxy.process.test_raw_stat_f", &stat_f);
    RecGetRecordInt("proxy.process.test_raw_stat_g", &stat_g);

    /*
       printf("-> g_count: %d, thr: 0x%x, stat_a: %d%d, stat_b: %d:%d, stat_c: %d:%d, stat_d: %f\n",
       g_count, mutex->thread_holding, stat_a, stat_b, stat_c, stat_d);

       printf("-> g_link_test_1: %d:%d, g_link_test_2: %f\n",
       g_link_test_1, g_link_test_2);
     */

    // Compare read value stat_a and expected value test_raw_stat_a
    RecRawStat test_raw_stat_a;
    RecFloat avg = 0.0f;
    test_raw_stat_a.sum = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_A]->sum));
    test_raw_stat_a.count = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_A]->count));
    if (test_raw_stat_a.count != 0)
      avg = (float) ((double) test_raw_stat_a.sum / (double) test_raw_stat_a.count);

    if (stat_a != avg) {
      printf("ERROR: stat_a: %f, expect stat_a: %f\n", stat_a, avg);
    } else {
      printf("OKAY: stat_a: %f, expect stat_a: %f\n", stat_a, avg);
    }

    // Compare read value stat_b and expected value test_raw_stat_b
    RecRawStat test_raw_stat_b;
    test_raw_stat_b.sum = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_B]->sum));
    if (stat_b != test_raw_stat_b.sum) {
      printf("ERROR: After RecIncrRawStatSum, stat_b: %d:%d, expect stat_b: %d:%d\n", stat_b, test_raw_stat_b.sum);
    } else {
      printf("OKAY: After RecIncrRawStatSum, stat_b: %d:%d, expect stat_b: %d:%d\n", stat_b, test_raw_stat_b.sum);
    }

    // Compare read value stat_c and expected value test_raw_stat_c
    RecRawStat test_raw_stat_c;
    test_raw_stat_c.count = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_C]->count));
    if (stat_c != test_raw_stat_c.count) {
      printf("ERROR: After RecIncrRawStatCount, stat_c: %d:%d, expect stat_c: %d:%d\n", stat_c, test_raw_stat_c.count);
    } else {
      printf("OKAY: After RecIncrRawStatCount, stat_c: %d:%d, expect stat_c: %d:%d\n", stat_c, test_raw_stat_c.count);
    }

    // Compare read value stat_d and expected value test_raw_stat_d
    ink64 ticks_old, time_old;
    RecGetGlobalRawStatSum(g_rsb, MY_STAT_D, &ticks_old);
    RecGetGlobalRawStatCount(g_rsb, MY_STAT_D, &time_old);
    RecFloat data = (float) (g_ticks - ticks_old) / (float) (g_time - time_old);
    if (stat_d != 4.0f) {
      printf("ERROR: stat_d: %f, expect stat_d: %f or I got data: %f\n", stat_d, 4.0f, data);
    } else {
      printf("OKAY: stat_d: %f, expect stat_d: %f or I got data: %f\n", stat_d, 4.0f, data);
    }

    // Compare read value stat_e and expected value test_raw_stat_e
    RecRawStat test_raw_stat_e;
    RecFloat r;
    test_raw_stat_e.sum = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_E]->sum));
    test_raw_stat_e.count = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_E]->count));
    if (test_raw_stat_e.count == 0) {
      r = 0.0f;
    } else {
      r = (float) ((double) test_raw_stat_e.sum / (double) test_raw_stat_e.count);
      r = r / (float) (HRTIME_SECOND);
    }
    if (stat_e != r) {
      printf("ERROR: stat_e: %f, expect stat_e from avg: %f\n", stat_e, r);
    } else {
      printf("OKAY: stat_e: %f, expect stat_e from avg: %f\n", stat_e, r);
    }

    // Compare read value stat_f and expected value test_raw_stat_f
    // Since RecSet only set g_rsb->global[MY_STAT_F]->count to be g_count value.
    // It will not set data.rec_int for stat_f until the RecExecRawStatSyncCbs
    // is called. RecExecRawStatSyncCbs callback RecRawStatSyncCount which set
    // data.rec_int to be g_rsb->global[MY_STAT_F]->count. The schedule for
    // RecExecRawStatSyncCbs is REC_RAW_STAT_SYNC_INTERVAL_SEC = 3 secs.
    // The normal for this dummy_function is 1 sec. There is no way we can
    // get the right value for this. Let ask Eric for this :)
    // I have increase the ink_sleep time (about 3 secs) between RecSet and RecGet
    // for stat_c hoping that we got the RecExecRawStatSyncCbs at the middle of them
    // so we can get the right value for stat_c. However, this will screw up
    // stat_d badly as we get NaN for stat_d.
    RecRawStat test_raw_stat_f;
    test_raw_stat_f.count = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_F]->count));
    RecInt check_stat_f;
    RecGetRawStatCount(g_rsb, (int) MY_STAT_F, &check_stat_f);
    if (stat_f != test_raw_stat_f.count || stat_f != check_stat_f) {
      printf("ERROR: After RecSetRawStatCount, stat_f: %d:%d, stat_f by REC_ATOMIC_READ64: %d:%d\n", stat_f,
             test_raw_stat_f.count);
      printf("       stat_f by RecGetRawStatCount: %d:%d\n", check_stat_f);
    } else {
      printf("OKAY: After RecSetRawStatCount, stat_f: %d:%d, stat_f by REC_ATOMIC_READ64: %d:%d\n", stat_f,
             test_raw_stat_f.count);
      printf("      stat_f by RecGetRawStatCount: %d:%d\n", check_stat_f);
    }

    // Compare read value stat_g and expected value test_raw_stat_g
    RecRawStat test_raw_stat_g;
    test_raw_stat_g.sum = REC_ATOMIC_READ64(&(g_rsb->global[MY_STAT_G]->sum));
    RecInt check_stat_g;
    RecGetRawStatSum(g_rsb, (int) MY_STAT_G, &check_stat_g);
    if (stat_g != test_raw_stat_g.count || stat_g != check_stat_g) {
      printf("ERROR: After RecSetRawStatSum, stat_g: %d:%d, stat_g by REC_ATOMIC_READ64: %d:%d\n", stat_g,
             test_raw_stat_g.sum);
      printf("       stat_g by RecGetRawStatSum: %d:%d\n", check_stat_g);
    } else {
      printf("OKAY: After RecSetRawStatSum, stat_g: %d:%d, stat_g by REC_ATOMIC_READ64: %d:%d\n", stat_g,
             test_raw_stat_g.sum);
      printf("      stat_g by RecGetRawStatSum: %d:%d\n", check_stat_g);
    }
    ink_atomic_increment(&g_count, 1);

    // test_raw_stat_e should have the time it takes to run this function
    hr_finish = ink_get_hrtime();
    RecIncrRawStat(g_rsb, mutex->thread_holding, (int) MY_STAT_E, hr_finish - hr_start);

    return 0;
  }
};

void
Test03()
{
  printf("[Test03: RawStat Test]\n");

  // Register raw statistics
  g_rsb = RecAllocateRawStatBlock((int) MY_STAT_COUNT);

  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_a",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) MY_STAT_A, RecRawStatSyncAvg);

  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_b",
                     RECD_INT, RECP_PERSISTENT, (int) MY_STAT_B, RecRawStatSyncSum);

  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_c",
                     RECD_INT, RECP_NON_PERSISTENT, (int) MY_STAT_C, RecRawStatSyncCount);

  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_d",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) MY_STAT_D, raw_stat_sync_ticks_per_sec);

  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_e",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) MY_STAT_E, RecRawStatSyncHrTimeAvg);
  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_f",
                     RECD_INT, RECP_NON_PERSISTENT, (int) MY_STAT_F, RecRawStatSyncCount);
  // If forget to Register this RawStat, we will have SEGV when checking
  // g_rsb->global[MY_STAT_G]
  RecRegisterRawStat(g_rsb, RECT_PROCESS, "proxy.process.test_raw_stat_g",
                     RECD_INT, RECP_NON_PERSISTENT, (int) MY_STAT_G, RecRawStatSyncSum);

  // Schedule a bunch of continuations that will use the stats registered above
  RawStatCont *sc = new RawStatCont(new_ProxyMutex());
  eventProcessor.schedule_every(sc, HRTIME_SECONDS(1), ET_CALL, EVENT_INTERVAL, NULL);
  eventProcessor.schedule_every(sc, HRTIME_SECONDS(1), ET_CALL, EVENT_INTERVAL, NULL);
  eventProcessor.schedule_every(sc, HRTIME_SECONDS(1), ET_CALL, EVENT_INTERVAL, NULL);
  eventProcessor.schedule_every(sc, HRTIME_SECONDS(1), ET_CALL, EVENT_INTERVAL, NULL);

}

//-------------------------------------------------------------------------
// DumpRecordHtCont
//-------------------------------------------------------------------------

struct DumpRecordsHtCont:public Continuation
{
  DumpRecordsHtCont(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&DumpRecordsHtCont::dummy_function);
  }
  int dummy_function(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    RecDumpRecordsHt(RECT_NULL);
    return 0;
  }
};

//-------------------------------------------------------------------------
// main
//-------------------------------------------------------------------------

int
main(int argc, char **argv)
{

  RecModeT mode_type = RECM_STAND_ALONE;
  if ((argc == 2) && (strcmp(argv[1], "-M") == 0)) {
    mode_type = RECM_CLIENT;
  }
  // Start diags logging
  FILE *log_fp;
  if ((log_fp = fopen("recprocess.log", "a+")) != NULL) {
    int status = setvbuf(log_fp, NULL, _IOLBF, 512);
    if (status != 0) {
      fclose(log_fp);
      log_fp = NULL;
    }
  }
  diags = new Diags("rec", NULL, log_fp);
  diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  diags->print(NULL, DL_Note, NULL, NULL, "Starting '%s'", argv[0]);

  // System initialization.  Note that a pointer to the diags object
  // is passed into librecprocess.a.  If manager isn't running, we
  // need to register our own configs
  RecProcessInit(mode_type, diags);
  RecProcessInitMessage(mode_type);
  if (mode_type == RECM_STAND_ALONE) {
    RecordsConfigRegister();
  }
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(4);
  RecProcessStart();

  RecSignalManager(1, "This is a signal, signaled by RecSignalManager");

  // See if we're sync'd okay
  RecDumpRecordsHt(RECT_NULL);


  // Run tests
  TreeTest01();
  Test01();
  Test02();
  Test03();
  TreeTest02();


  // Schedule dump continuation so that we can see what's going on
  DumpRecordsHtCont *drhc = new DumpRecordsHtCont(new_ProxyMutex());
  eventProcessor.schedule_every(drhc, HRTIME_SECONDS(10), ET_CALL, EVENT_INTERVAL, NULL);

  this_thread()->execute();
  return 0;
}
