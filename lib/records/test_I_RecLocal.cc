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

#include "I_RecLocal.h"
#include "P_RecUtils.h"
#include "test_RecordsConfig.h"

#include "P_RecCore.h"

Diags *diags = NULL;
void RecDumpRecordsHt(RecT rec_type);

//-------------------------------------------------------------------------
// Test01: Callback Test
//
// The following test verifies that the callbacks are executed.
//-------------------------------------------------------------------------
int g_config_update_result = 0;

int
cb_test_1a(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  if ((cookie == (void *)0x12345678) && (strcmp(data.rec_string, "cb_test_1__changed") == 0)) {
    g_config_update_result++;
    printf("    - cb_test_1(%d) name: %s, data: %s, cookie: 0x%x\n", g_config_update_result, name, data.rec_string, cookie);
  } else {
    g_config_update_result = 0;
  }
  return REC_ERR_OKAY;
}

int
cb_test_1b(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  return cb_test_1a(name, data_type, data, cookie);
}

int
cb_test_2a(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
           void * /* cookie ATS_UNUSED */)
{
  g_config_update_result = -1;
  return REC_ERR_FAIL;
}

int
cb_test_2b(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  return cb_test_2a(name, data_type, data, cookie);
}

void
Test01()
{
  g_config_update_result = 0;
  printf("\n[Test01: Callback Tests]\n");
  int failures = 0;

  printf("  [RecRegisterConfigUpdateCb]\n");

  // Initialize variables
  RecSetRecordString("proxy.config.local.cb_test_1", "cb_test_1__original");
  RecSetRecordString("proxy.config.local.cb_test_2", "cb_test_2__original");
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Register config update callbacks
  RecRegisterConfigUpdateCb("proxy.config.local.cb_test_1", cb_test_1a, (void *)0x12345678);
  RecRegisterConfigUpdateCb("proxy.config.local.cb_test_1", cb_test_1b, (void *)0x12345678);
  RecRegisterConfigUpdateCb("proxy.config.local.cb_test_2", cb_test_2a, (void *)0x87654321);
  RecRegisterConfigUpdateCb("proxy.config.local.cb_test_2", cb_test_2b, (void *)0x87654321);

  // Change proxy.config.cb_test_1
  RecSetRecordString("proxy.config.local.cb_test_1", "cb_test_1__changed");
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Check globals to make sure the right thing happened
  if (g_config_update_result == 2) {
    printf("    SUMMARY: PASS (%d)\n", g_config_update_result);
  } else {
    printf("    SUMMARY: FAIL (%d)\n", g_config_update_result);
  }
}

//-------------------------------------------------------------------------
// Test02: Callback (Multi-lock) Test
//
// The following test verifies we can access the variables within its own
// callbacks. When a callback is invoked, the record's mutex lock has
// already taken. If RecMutex works properly, we can still access the
// variable within its own callback.
//-------------------------------------------------------------------------
int
cb_test_3a(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  RecString rec_result;
  int rec_status = RecGetRecordString_Xmalloc(name, &rec_result);

  if ((rec_status == REC_ERR_OKAY) && (cookie == (void *)0x12344321) && (strcmp(rec_result, "cb_test_3__changed") == 0)) {
    ink_assert(strcmp(rec_result, data.rec_string) == 0);

    g_config_update_result++;
    printf("    - cb_test_3(%d) name: %s, data: %s, cookie: 0x%x\n", g_config_update_result, name, rec_result, cookie);
  } else {
    g_config_update_result = 0;
  }
  return REC_ERR_OKAY;
}

int
cb_test_3b(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  return cb_test_3a(name, data_type, data, cookie);
}

void
Test02()
{
  g_config_update_result = 0;
  printf("\n[Test02: Callback (Multi-lock) Test]\n");
  int failures = 0;

  printf("  [RecRegisterConfigUpdateCb]\n");

  // Initialize variables
  RecSetRecordString("proxy.config.local.cb_test_3", "cb_test_3__original");
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Register config update callbacks
  RecRegisterConfigUpdateCb("proxy.config.local.cb_test_3", cb_test_3a, (void *)0x12344321);
  RecRegisterConfigUpdateCb("proxy.config.local.cb_test_3", cb_test_3b, (void *)0x12344321);

  // Change proxy.config.cb_test_1
  RecSetRecordString("proxy.config.local.cb_test_3", "cb_test_3__changed");
  printf("    - sleep(2*REC_CONFIG_UPDATE_INTERVAL_SEC)\n");
  sleep(2 * REC_CONFIG_UPDATE_INTERVAL_SEC);

  // Check globals to make sure the right thing happened
  if (g_config_update_result == 2) {
    printf("    SUMMARY: PASS (%d)\n", g_config_update_result);
  } else {
    printf("    SUMMARY: FAIL (%d)\n", g_config_update_result);
  }
}

//-------------------------------------------------------------------------
// main
//-------------------------------------------------------------------------

int
main(int argc, char **argv)
{
  // start diags logging
  FILE *log_fp;
  if ((log_fp = fopen("reclocal.log", "a+")) != NULL) {
    int status = setvbuf(log_fp, NULL, _IOLBF, 512);
    if (status != 0) {
      fclose(log_fp);
      log_fp = NULL;
    }
  }
  diags = new Diags("rec", NULL, log_fp);
  diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  diags->print(NULL, DTA(DL_Note), "Starting '%s'", argv[0]);

  // system initialization
  RecLocalInit(diags);
  RecLocalInitMessage();
  RecordsConfigRegister();
  RecLocalStart();

  // test
  Test01(); // Local callbacks
  Test02(); // Local callbacks -- mulit-lock
  Test03(); // RecTree

  while (true) {
    RecDumpRecordsHt(RECT_NULL);
    sleep(10);
  }

  return 0;
}
