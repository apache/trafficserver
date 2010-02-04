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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "INKMgmtAPI.h"
//#include "APITestRemote.h"

#define TEST_STRING     1
#define TEST_FLOAT      1
#define TEST_INT        1
#define TEST_COUNTER    1
#define TEST_REC_SET    1
#define TEST_REC_GET    1
#define TEST_REC_GET_2  0
#define READ_FILE       0
#define WRITE_FILE      0
#define SET_INT         0
#define TEST_ERROR_REC  1
#define TEST_ACTION     0

void
print_err(INKError err)
{
  char *err_msg;

  err_msg = INKGetErrorMessage(err);
  printf("ERROR: %s\n", err_msg);

  //if (err_msg) free(err_msg);
}

/* ------------------------------------------------------------------------
 * test_action_need
 * ------------------------------------------------------------------------
 * tests if correct action need is returned when requested record is set
 */
void
test_action_need(void)
{
  INKActionNeedT action;

  // RU_NULL record
  INKRecordSetString("proxy.config.proxy_name", "proxy_dorky", &action);
  printf("[INKRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_UNDEFINED, action);

  // RU_REREAD record
  INKRecordSetInt("proxy.config.ldap.cache.size", 1000, &action);
  printf("[INKRecordSetInt] proxy.config.ldap.cache.size\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_RECONFIGURE, action);

  // RU_RESTART_TS record
  INKRecordSetInt("proxy.config.cluster.cluster_port", 6666, &action);
  printf("[INKRecordSetInt] proxy.config.cluster.cluster_port\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_RESTART, action);

  // RU_RESTART_TC record
  INKRecordSetInt("proxy.config.nntp.enabled", 1, &action);
  printf("[INKRecordSetInt] proxy.config.nntp.enabled\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_SHUTDOWN, action);

}

/* ------------------------------------------------------------------------
 * test_error_records
 * ------------------------------------------------------------------------
 * stress test error handling by trying to break things
 */
void
test_error_records()
{
  INKInt port1, port2, new_port = 8080;
  INKActionNeedT action;
  INKError ret;

  // try sending request for invalid record names

  printf("\n");

  // test get integer
  ret = INKRecordGetInt("proy.config.cop.core_signal", &port1);
  if (ret != INK_ERR_OKAY) {
    printf("INKRecordGetInt FAILED!\n");
    print_err(ret);
  } else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port1);

  // test set integer
  ret = INKRecordSetInt("proy.config.cop.core_signal", new_port, &action);
  if (ret != INK_ERR_OKAY) {
    printf("INKRecordSetInt FAILED!\n");
    print_err(ret);
  } else
    printf("[INKRecordSetInt] proxy.config.cop.core_signal=%lld \n", new_port);


  //get
  ret = INKRecordGetInt("proxy.config.cop.core_signal", &port2);
  if (ret != INK_ERR_OKAY) {
    printf("INKRecordGetInt FAILED!\n");
    print_err(ret);
  } else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port2);

  printf("\n");

}


/**********************************************************************
 * Main Program
 **********************************************************************/
int
main(int argc, char *argv[])
{
  INKActionNeedT action;
  INKRecordEle *rec_ele;
  char *rec_value;
  char new_str[] = "new_record_value";
  INKInt port1, port2, new_port = 52432;
  INKFloat flt1, flt2, new_flt = 1.444;
  INKCounter ctr1, ctr2, new_ctr = 666;

  char *f_text = NULL;
  int f_size = -1;
  int f_ver = -1;
  char new_f_text[] = "blah, blah blah\n I hope this works. please!!!   \n";
  int new_f_size = strlen(new_f_text);

  printf("START REMOTE API TEST\n");

  // initialize 
  if (INKInit("../../../../etc/trafficserver/mgmtapisocket") != INK_ERR_OKAY) {
    printf("INKInit failed!\n");
    return -1;
  }

  /********************* START TEST SECTION *****************/
  printf("\n\n");

#if SET_INT
  // test set integer
  if (INKRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != INK_ERR_OKAY)
    printf("INKRecordSetInt FAILED!\n");
  else
    printf("[INKRecordSetInt] proxy.config.cop.core_signal=%lld \n", new_port);
#endif


#if TEST_REC_GET
  // retrieve a string value record using generic RecordGet
  rec_ele = INKRecordEleCreate();
  if (INKRecordGet("proxy.config.http.cache.vary_default_other", rec_ele) != INK_ERR_OKAY)
    printf("INKRecordGet FAILED!\n");
  else
    printf("[INKRecordGet] proxy.config.http.cache.vary_default_other=%s\n", rec_ele->string_val);

  INKRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif


#if TEST_REC_GET_2
  // retrieve a string value record using generic RecordGet
  rec_ele = INKRecordEleCreate();
  if (INKRecordGet("proxy.config.proxy_name", rec_ele) != INK_ERR_OKAY)
    printf("INKRecordGet FAILED!\n");
  else
    printf("[INKRecordGet] proxy.config.proxy_name=%s\n", rec_ele->string_val);

  INKRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif

#if TEST_STRING
  // retrieve an string value record using GetString  
  if (INKRecordGetString("proxy.config.proxy_name", &rec_value) != INK_ERR_OKAY)
    printf("INKRecordGetString FAILED!\n");
  else
    printf("[INKRecordGetString] proxy.config.proxy_name=%s\n", rec_value);

  // test RecordSet
  if (INKRecordSetString("proxy.config.proxy_name", (INKString) new_str, &action) != INK_ERR_OKAY)
    printf("INKRecordSetString FAILED\n");
  else
    printf("[INKRecordSetString] proxy.config.proxy_name=%s\n", new_str);

  // get 
  if (INKRecordGetString("proxy.config.proxy_name", &rec_value) != INK_ERR_OKAY)
    printf("INKRecordGetString FAILED!\n");
  else
    printf("[INKRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  printf("\n");
#endif

#if TEST_INT
  printf("\n");
  // test get integer
  if (INKRecordGetInt("proxy.config.cop.core_signal", &port1) != INK_ERR_OKAY)
    printf("INKRecordGetInt FAILED!\n");
  else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port1);

  // test set integer
  if (INKRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != INK_ERR_OKAY)
    printf("INKRecordSetInt FAILED!\n");
  else
    printf("[INKRecordSetInt] proxy.config.cop.core_signal=%lld \n", new_port);

  if (INKRecordGetInt("proxy.config.cop.core_signal", &port2) != INK_ERR_OKAY)
    printf("INKRecordGetInt FAILED!\n");
  else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port2);
  printf("\n");
#endif

#if TEST_COUNTER
  printf("\n");

  if (INKRecordGetCounter("proxy.process.socks.connections_successful", &ctr1) != INK_ERR_OKAY)
    printf("INKRecordGetCounter FAILED!\n");
  else
    printf("[INKRecordGetCounter]proxy.process.socks.connections_successful=%lld \n", ctr1);

  if (INKRecordSetCounter("proxy.process.socks.connections_successful", new_ctr, &action) != INK_ERR_OKAY)
    printf("INKRecordSetCounter FAILED!\n");
  printf("[INKRecordSetCounter] proxy.process.socks.connections_successful=%lld \n", new_ctr);

  if (INKRecordGetCounter("proxy.process.socks.connections_successful", &ctr2) != INK_ERR_OKAY)
    printf("INKRecordGetCounter FAILED!\n");
  else
    printf("[INKRecordGetCounter]proxy.process.socks.connections_successful=%lld \n", ctr2);
  printf("\n");
#endif

#if TEST_FLOAT
  printf("\n");
  if (INKRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt1) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt1);

  if (INKRecordSetFloat("proxy.config.http.cache.fuzz.probability", new_flt, &action) != INK_ERR_OKAY)
    printf("INKRecordSetFloat FAILED!\n");
  else
    printf("[INKRecordSetFloat] proxy.config.http.cache.fuzz.probability=%f\n", new_flt);

  if (INKRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt2) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt2);
  printf("\n");
#endif

#if TEST_REC_SET
  printf("\n");
  if (INKRecordSet("proxy.config.http.cache.fuzz.probability", "-0.3456", &action) != INK_ERR_OKAY)
    printf("INKRecordSet FAILED!\n");
  else
    printf("[INKRecordSet] proxy.config.http.cache.fuzz.probability=-0.3456\n");

  if (INKRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt2) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt2);
#endif


#if READ_FILE
  printf("\n");
  if (INKConfigFileRead(INK_FNAME_FILTER, &f_text, &f_size, &f_ver) != INK_ERR_OKAY)
    printf("[INKConfigFileRead] FAILED!\n");
  else
    printf("[INKConfigFileRead]\n\tFile Size=%d, Version=%d\n%s\n", f_size, f_ver, f_text);
#endif

#if WRITE_FILE
  printf("\n");
  if (INKConfigFileWrite(INK_FNAME_FILTER, new_f_text, new_f_size, f_ver) != INK_ERR_OKAY)
    printf("[INKConfigFileWrite] FAILED!\n");
  else
    printf("[INKConfigFileWrite] SUCCESS!\n");
  printf("\n");

  // should free f_text???
  if (INKConfigFileRead(INK_FNAME_FILTER, &f_text, &f_size, &f_ver) != INK_ERR_OKAY)
    printf("[INKConfigFileRead] FAILED!\n");
  else
    printf("[INKConfigFileRead]\n\tFile Size=%d, Version=%d\n%s\n", f_size, f_ver, f_text);
#endif

#if TEST_ERROR_REC
  test_error_records();
#endif

#if TEST_ACTION
  test_action_need();
#endif

  printf("\n\n");

  /********************* END TEST SECTION *********************/
  INKTerminate();               //ERROR:Causes infinite!! 

  printf("END REMOTE API TEST\n");
}
