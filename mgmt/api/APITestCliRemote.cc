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
 * Filename: APITestCliRemote.cc
 * Purpose: An interactive cli to test remote mgmt API; UNIT TEST for mgmtAPI
 * Created: lant
 *
 ***************************************************************************/

/***************************************************************************
 * Possible Commands:
 ***************************************************************************
 * Control Operations:
 * -------------------
 * state:   returns ON (proxy is on) or OFF (proxy is off)
 * start:<tsArgs>  -   turns Proxy on, the tsArgs is optional;
 *                     it can either be  "hostdb" or "all",
 *                 eg. start, start:hostdb, start:all
 * stop:    turns Proxy off
 * restart: restarts Traffic Manager (Traffic Cop must be running)
 *
 * File operations:
 * ---------------
 * read_file:  reads hosting.config file
 * proxy.config.xxx (a records.config variable): returns value of that record
 * records: tests get/set/get a record of each different type
 *          (int, float, counter, string)
 * err_recs: stress test record get/set functions by purposely entering
 *              invalid record names and/or values
 * get_mlt: tests TSRecordGetMlt
 * set_mlt: tests TSRecordSetMlt
 *
 * read_url: tests TSReadFromUrl works by retrieving two valid urls
 * test_url: tests robustness of TSReadFromUrl using invalid urls
 *
 * Event Operations:
 * ----------------
 * active_events: lists the names of all currently active events
 * MGMT_ALARM_xxx (event_name specified in CoreAPIShared.h or Alarms.h):
 *                 resolves the specified event
 * register: registers a generic callback (=eventCallbackFn) which
 *           prints out the event name whenever an event is signalled
 * unregister: unregisters the generic callback function eventCallbackFn
 *
 * Diags
 * ----
 * diags - uses STATUS, NOTE, FATAL, ERROR diags
 *
 * Statistics
 * ----------
 * set_stats - sets dummy values for selected group of NODE, PROCESS
 *             records
 * print_stats - prints the values for the same selected group of records
 * reset_stats - resets all statistics to default values
 */

#include "ts/ink_config.h"
#include "ts/ink_defs.h"
#include "ts/ink_memory.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <strings.h>
#include "ts/ink_string.h"

#include "mgmtapi.h"

// refer to test_records() function
#define TEST_STRING 1
#define TEST_FLOAT 1
#define TEST_INT 1
#define TEST_COUNTER 1
#define TEST_REC_SET 1
#define TEST_REC_GET 0
#define TEST_REC_GET_2 0

#define SET_INT 0

// set to 1 if running as part of installation package
// set to 0 if being tested in developer environment
#define INSTALL_TEST 0

/***************************************************************************
 * Printing Helper Functions
 ***************************************************************************/

/* ------------------------------------------------------------------------
 * print_err
 * ------------------------------------------------------------------------
 * used to print the error description associated with the TSMgmtError err
 */
void
print_err(const char *module, TSMgmtError err)
{
  char *err_msg;

  err_msg = TSGetErrorMessage(err);
  printf("(%s) ERROR: %s\n", module, err_msg);

  if (err_msg) {
    TSfree(err_msg);
  }
}

/*-------------------------------------------------------------
 * print_string_list
 *-------------------------------------------------------------*/
void
print_string_list(TSStringList list)
{
  int i, count, buf_pos = 0;
  char *str;
  char buf[1000];

  if (!list) {
    return;
  }
  count = TSStringListLen(list);
  for (i = 0; i < count; i++) {
    str = TSStringListDequeue(list);
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s,", str);
    buf_pos = strlen(buf);
    TSStringListEnqueue(list, str);
  }
  printf("%s \n", buf);
}

/*-------------------------------------------------------------
 * print_int_list
 *-------------------------------------------------------------*/
void
print_int_list(TSIntList list)
{
  int i, count, buf_pos = 0;
  int *elem;
  char buf[1000];

  count = TSIntListLen(list);
  for (i = 0; i < count; i++) {
    elem = TSIntListDequeue(list);
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d:", *elem);
    buf_pos = strlen(buf);
    TSIntListEnqueue(list, elem);
  }
  printf("Int List: %s \n", buf);
}

/***************************************************************************
 * Control Testing
 ***************************************************************************/
void
print_proxy_state()
{
  TSProxyStateT state = TSProxyStateGet();

  switch (state) {
  case TS_PROXY_ON:
    printf("Proxy State = ON\n");
    break;
  case TS_PROXY_OFF:
    printf("Proxy State = OFF\n");
    break;
  default:
    printf("ERROR: Proxy State Undefined!\n");
    break;
  }
}

// starts Traffic Server (turns proxy on)
void
start_TS(char *tsArgs)
{
  TSMgmtError ret;
  TSCacheClearT clear = TS_CACHE_CLEAR_NONE;
  char *args;

  strtok(tsArgs, ":");
  args = strtok(nullptr, ":");
  if (args) {
    if (strcmp(args, "all\n") == 0) {
      clear = TS_CACHE_CLEAR_CACHE;
    } else if (strcmp(args, "hostdb\n") == 0) {
      clear = TS_CACHE_CLEAR_HOSTDB;
    }
  } else {
    clear = TS_CACHE_CLEAR_NONE;
  }

  printf("STARTING PROXY with cache: %d\n", clear);
  if ((ret = TSProxyStateSet(TS_PROXY_ON, clear)) != TS_ERR_OKAY) {
    printf("[TSProxyStateSet] turn on FAILED\n");
  }
  print_err("start_TS", ret);
}

// stops Traffic Server (turns proxy off)
void
stop_TS()
{
  TSMgmtError ret;

  printf("STOPPING PROXY\n");
  if ((ret = TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_NONE)) != TS_ERR_OKAY) {
    printf("[TSProxyStateSet] turn off FAILED\n");
  }
  print_err("stop_TS", ret);
}

// restarts Traffic Manager (Traffic Cop must be running)
void
restart()
{
  TSMgmtError ret;

  printf("RESTART\n");
  if ((ret = TSRestart(true)) != TS_ERR_OKAY) {
    printf("[TSRestart] FAILED\n");
  }

  print_err("restart", ret);
}

// rereads all the configuration files
void
reconfigure()
{
  TSMgmtError ret;

  printf("RECONFIGURE\n");
  if ((ret = TSReconfigure()) != TS_ERR_OKAY) {
    printf("[TSReconfigure] FAILED\n");
  }

  print_err("reconfigure", ret);
}

/* ------------------------------------------------------------------------
 * test_action_need
 * ------------------------------------------------------------------------
 * tests if correct action need is returned when requested record is set
 */
void
test_action_need()
{
  TSActionNeedT action;

  // RU_NULL record
  TSRecordSetString("proxy.config.proxy_name", "proxy_dorky", &action);
  printf("[TSRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n", TS_ACTION_UNDEFINED,
         action);
}

/* Bouncer the traffic_server process(es) */
void
bounce()
{
  TSMgmtError ret;

  printf("BOUNCER\n");
  if ((ret = TSBounce(true)) != TS_ERR_OKAY) {
    printf("[TSBounce] FAILED\n");
  }

  print_err("bounce", ret);
}

/***************************************************************************
 * Record Testing
 ***************************************************************************/

/* ------------------------------------------------------------------------
 * test_error_records
 * ------------------------------------------------------------------------
 * stress test error handling by purposely being dumb; send requests to
 * get invalid record names
 */
void
test_error_records()
{
  TSInt port1, new_port = 8080;
  TSActionNeedT action;
  TSMgmtError ret;
  TSCounter ctr1;

  printf("\n");
  // test get integer
  fprintf(stderr, "Test invalid record names\n");

  ret = TSRecordGetInt("proy.config.cop.core_signal", &port1);
  if (ret != TS_ERR_OKAY) {
    print_err("TSRecordGetInt", ret);
  } else {
    printf("[TSRecordGetInt] proxy.config.cop.core_signal=%" PRId64 " \n", port1);
  }

  // test set integer
  ret = TSRecordSetInt("proy.config.cop.core_signal", new_port, &action);
  print_err("TSRecordSetInt", ret);

  printf("\n");
  if (TSRecordGetCounter("proxy.press.socks.connections_successful", &ctr1) != TS_ERR_OKAY) {
    printf("TSRecordGetCounter FAILED!\n");
  } else {
    printf("[TSRecordGetCounter]proxy.process.socks.connections_successful=%" PRId64 " \n", ctr1);
  }
}

/* ------------------------------------------------------------------------
 * test_records
 * ------------------------------------------------------------------------
 * stress test record functionality by getting and setting different
 * records types; use the #defines defined above to determine which
 * type of tests you'd like turned on/off
 */
void
test_records()
{
  TSActionNeedT action;
  char *rec_value;
  char new_str[] = "new_record_value";
  TSInt port1, port2, new_port  = 52432;
  TSCounter ctr1, ctr2, new_ctr = 6666;
  TSMgmtError err;

  /********************* START TEST SECTION *****************/
  printf("\n\n");

#if SET_INT
  // test set integer
  if (TSRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != TS_ERR_OKAY)
    printf("TSRecordSetInt FAILED!\n");
  else
    printf("[TSRecordSetInt] proxy.config.cop.core_signal=%" PRId64 " \n", new_port);
#endif

#if TEST_REC_GET
  TSRecordEle *rec_ele;
  // retrieve a string value record using generic RecordGet
  rec_ele = TSRecordEleCreate();
  if (TSRecordGet("proxy.config.http.cache.vary_default_other", rec_ele) != TS_ERR_OKAY)
    printf("TSRecordGet FAILED!\n");
  else
    printf("[TSRecordGet] proxy.config.http.cache.vary_default_other=%s\n", rec_ele->string_val);

  TSRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif

#if TEST_REC_GET_2
  // retrieve a string value record using generic RecordGet
  rec_ele = TSRecordEleCreate();
  if (TSRecordGet("proxy.config.proxy_name", rec_ele) != TS_ERR_OKAY)
    printf("TSRecordGet FAILED!\n");
  else
    printf("[TSRecordGet] proxy.config.proxy_name=%s\n", rec_ele->string_val);

  TSRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif

#if TEST_STRING
  // retrieve an string value record using GetString
  err = TSRecordGetString("proxy.config.proxy_name", &rec_value);
  if (err != TS_ERR_OKAY) {
    print_err("TSRecordGetString", err);
  } else {
    printf("[TSRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  }
  TSfree(rec_value);
  rec_value = nullptr;

  // test RecordSet
  err = TSRecordSetString("proxy.config.proxy_name", (TSString)new_str, &action);
  if (err != TS_ERR_OKAY) {
    print_err("TSRecordSetString", err);
  } else {
    printf("[TSRecordSetString] proxy.config.proxy_name=%s\n", new_str);
  }

  // get
  err = TSRecordGetString("proxy.config.proxy_name", &rec_value);
  if (err != TS_ERR_OKAY) {
    print_err("TSRecordGetString", err);
  } else {
    printf("[TSRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  }
  printf("\n");
  TSfree(rec_value);
#endif

#if TEST_INT
  printf("\n");
  // test get integer
  if (TSRecordGetInt("proxy.config.cop.core_signal", &port1) != TS_ERR_OKAY) {
    printf("TSRecordGetInt FAILED!\n");
  } else {
    printf("[TSRecordGetInt] proxy.config.cop.core_signal=%" PRId64 " \n", port1);
  }

  // test set integer
  if (TSRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != TS_ERR_OKAY) {
    printf("TSRecordSetInt FAILED!\n");
  } else {
    printf("[TSRecordSetInt] proxy.config.cop.core_signal=%" PRId64 " \n", new_port);
  }

  if (TSRecordGetInt("proxy.config.cop.core_signal", &port2) != TS_ERR_OKAY) {
    printf("TSRecordGetInt FAILED!\n");
  } else {
    printf("[TSRecordGetInt] proxy.config.cop.core_signal=%" PRId64 " \n", port2);
  }
  printf("\n");
#endif

#if TEST_COUNTER
  printf("\n");

  if (TSRecordGetCounter("proxy.process.socks.connections_successful", &ctr1) != TS_ERR_OKAY) {
    printf("TSRecordGetCounter FAILED!\n");
  } else {
    printf("[TSRecordGetCounter]proxy.process.socks.connections_successful=%" PRId64 " \n", ctr1);
  }

  if (TSRecordSetCounter("proxy.process.socks.connections_successful", new_ctr, &action) != TS_ERR_OKAY) {
    printf("TSRecordSetCounter FAILED!\n");
  } else {
    printf("[TSRecordSetCounter] proxy.process.socks.connections_successful=%" PRId64 " \n", new_ctr);
  }

  if (TSRecordGetCounter("proxy.process.socks.connections_successful", &ctr2) != TS_ERR_OKAY) {
    printf("TSRecordGetCounter FAILED!\n");
  } else {
    printf("[TSRecordGetCounter]proxy.process.socks.connections_successful=%" PRId64 " \n", ctr2);
  }
  printf("\n");
#endif
}

// retrieves the value of the "proxy.config.xxx" record requested at input
void
test_rec_get(char *rec_name)
{
  TSRecordEle *rec_ele;
  TSMgmtError ret;
  char *name;

  name = ats_strdup(rec_name);
  printf("[test_rec_get] Get Record: %s\n", name);

  // retrieve a string value record using generic RecordGet
  rec_ele = TSRecordEleCreate();
  if ((ret = TSRecordGet(name, rec_ele)) != TS_ERR_OKAY) {
    printf("TSRecordGet FAILED!\n");
  } else {
    switch (rec_ele->rec_type) {
    case TS_REC_INT:
      printf("[TSRecordGet] %s=%" PRId64 "\n", name, rec_ele->valueT.int_val);
      break;
    case TS_REC_COUNTER:
      printf("[TSRecordGet] %s=%" PRId64 "\n", name, rec_ele->valueT.counter_val);
      break;
    case TS_REC_FLOAT:
      printf("[TSRecordGet] %s=%f\n", name, rec_ele->valueT.float_val);
      break;
    case TS_REC_STRING:
      printf("[TSRecordGet] %s=%s\n", name, rec_ele->valueT.string_val);
      break;
    default:
      // Handled here:
      // TS_REC_UNDEFINED
      break;
    }
  }

  print_err("TSRecordGet", ret);

  TSRecordEleDestroy(rec_ele);
  TSfree(name);
}

/* ------------------------------------------------------------------------
 * test_record_get_mlt
 * ------------------------------------------------------------------------
 * Creates a list of record names to retrieve, and then batch request to
 * get list of records
 */
void
test_record_get_mlt()
{
  TSRecordEle *rec_ele;
  TSStringList name_list;
  TSList rec_list;
  int i, num;
  char *v1, *v2, *v3, *v6, *v7;
  TSMgmtError ret;

  name_list = TSStringListCreate();
  rec_list  = TSListCreate();

  const size_t v1_size = (sizeof(char) * (strlen("proxy.config.proxy_name") + 1));
  v1                   = (char *)TSmalloc(v1_size);
  ink_strlcpy(v1, "proxy.config.proxy_name", v1_size);
  const size_t v2_size = (sizeof(char) * (strlen("proxy.config.bin_path") + 1));
  v2                   = (char *)TSmalloc(v2_size);
  ink_strlcpy(v2, "proxy.config.bin_path", v2_size);
  const size_t v3_size = (sizeof(char) * (strlen("proxy.config.manager_binary") + 1));
  v3                   = (char *)TSmalloc(v3_size);
  ink_strlcpy(v3, "proxy.config.manager_binary", v3_size);
  const size_t v6_size = (sizeof(char) * (strlen("proxy.config.env_prep") + 1));
  v6                   = (char *)TSmalloc(v6_size);
  ink_strlcpy(v6, "proxy.config.env_prep", v6_size);
  const size_t v7_size = (sizeof(char) * (strlen("proxy.config.cop.core_signal") + 1));
  v7                   = (char *)TSmalloc(v7_size);
  ink_strlcpy(v7, "proxy.config.cop.core_signal", v7_size);

  // add the names to the get_list
  TSStringListEnqueue(name_list, v1);
  TSStringListEnqueue(name_list, v2);
  TSStringListEnqueue(name_list, v3);
  TSStringListEnqueue(name_list, v6);
  TSStringListEnqueue(name_list, v7);

  num = TSStringListLen(name_list);
  printf("Num Records to Get: %d\n", num);
  ret = TSRecordGetMlt(name_list, rec_list);
  // free the string list
  TSStringListDestroy(name_list);
  if (ret != TS_ERR_OKAY) {
    print_err("TSStringListDestroy", ret);
  }

  for (i = 0; i < num; i++) {
    rec_ele = (TSRecordEle *)TSListDequeue(rec_list);
    if (!rec_ele) {
      printf("ERROR\n");
      break;
    }
    printf("Record: %s = ", rec_ele->rec_name);
    switch (rec_ele->rec_type) {
    case TS_REC_INT:
      printf("%" PRId64 "\n", rec_ele->valueT.int_val);
      break;
    case TS_REC_COUNTER:
      printf("%" PRId64 "\n", rec_ele->valueT.counter_val);
      break;
    case TS_REC_FLOAT:
      printf("%f\n", rec_ele->valueT.float_val);
      break;
    case TS_REC_STRING:
      printf("%s\n", rec_ele->valueT.string_val);
      break;
    default:
      // Handled here:
      // TS_REC_UNDEFINED
      break;
    }
    TSRecordEleDestroy(rec_ele);
  }

  TSListDestroy(rec_list); // must dequeue and free each string individually

  return;
}

/* ------------------------------------------------------------------------
 * test_record_set_mlt
 * ------------------------------------------------------------------------
 * Creates a list of TSRecordEle's, and then batch request to set records
 * Also checks to make sure correct action_need type is set.
 */
void
test_record_set_mlt()
{
  TSList list;
  TSRecordEle *ele1, *ele2;
  TSActionNeedT action = TS_ACTION_UNDEFINED;
  TSMgmtError err;

  list = TSListCreate();

  ele1                    = TSRecordEleCreate(); // TS_TYPE_UNDEFINED action
  ele1->rec_name          = TSstrdup("proxy.config.cli_binary");
  ele1->rec_type          = TS_REC_STRING;
  ele1->valueT.string_val = TSstrdup(ele1->rec_name);

  ele2                 = TSRecordEleCreate(); // undefined action
  ele2->rec_name       = TSstrdup("proxy.config.cop.core_signal");
  ele2->rec_type       = TS_REC_INT;
  ele2->valueT.int_val = -4;

  TSListEnqueue(list, ele1);
  TSListEnqueue(list, ele2);

  err = TSRecordSetMlt(list, &action);
  print_err("TSRecordSetMlt", err);
  fprintf(stderr, "[TSRecordSetMlt] Action Required: %d\n", action);

  // cleanup: need to iterate through list and delete each ele
  int count = TSListLen(list);
  for (int i = 0; i < count; i++) {
    TSRecordEle *ele = (TSRecordEle *)TSListDequeue(list);
    TSRecordEleDestroy(ele);
  }
  TSListDestroy(list);
}

/***************************************************************************
 * File I/O Testing
 ***************************************************************************/

// if valid==true, then use a valid url to read
void
test_read_url(bool valid)
{
  char *header = nullptr;
  int headerSize;
  char *body = nullptr;
  int bodySize;
  TSMgmtError err;

  if (!valid) {
    // first try

    err = TSReadFromUrlEx("hsdfasdf.com:80/index.html", &header, &headerSize, &body, &bodySize, 50000);
    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("--------------------------------------------------------------\n");
      //  printf("The header...\n%s\n%d\n", *header, *headerSize);
      printf("--------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (body) {
      TSfree(body);
    }
    if (header) {
      TSfree(header);
    }

    err = TSReadFromUrlEx("http://sadfasdfi.com:80/", &header, &headerSize, &body, &bodySize, 50000);
    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header) {
      TSfree(header);
    }
    if (body) {
      TSfree(body);
    }

  } else { // use valid urls
    err = TSReadFromUrlEx("lakota.example.com:80/", &header, &headerSize, &body, &bodySize, 50000);

    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header) {
      TSfree(header);
    }
    if (body) {
      TSfree(body);
    }

    // read second url
    err = TSReadFromUrlEx("http://www.apache.org:80/index.html", &header, &headerSize, &body, &bodySize, 50000);
    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header) {
      TSfree(header);
    }
    if (body) {
      TSfree(body);
    }
  }
}

/***************************************************************************
 * Events Testing
 ***************************************************************************/
/* ------------------------------------------------------------------------
 * print_active_events
 * ------------------------------------------------------------------------
 * retrieves a list of all active events and prints out each event name,
 * one event per line
 */
void
print_active_events()
{
  TSList events;
  TSMgmtError ret;
  int count, i;
  char *name;

  printf("[print_active_events]\n");

  events = TSListCreate();
  ret    = TSActiveEventGetMlt(events);
  if (ret != TS_ERR_OKAY) {
    print_err("TSActiveEventGetMlt", ret);
    goto END;
  } else { // successful get
    count = TSListLen(events);
    for (i = 0; i < count; i++) {
      name = (char *)TSListDequeue(events);
      printf("\t%s\n", name);
      TSfree(name);
    }
  }

END:
  TSListDestroy(events);
  return;
}

/* ------------------------------------------------------------------------
 * check_active
 * ------------------------------------------------------------------------
 * returns true if the event named event_name is currently active (unresolved)
 * returns false otherwise
 */
bool
check_active(char *event_name)
{
  bool active;
  TSMgmtError ret;

  ret = TSEventIsActive(event_name, &active);
  print_err("TSEventIsActive", ret);

  if (active) {
    printf("%s is ACTIVE\n", event_name);
  } else {
    printf("%s is NOT-ACTIVE\n", event_name);
  }

  return active;
}

/* ------------------------------------------------------------------------
 * try_resolve
 * ------------------------------------------------------------------------
 * checks if the event_name is still unresolved; if it is, it then
 * resolves it, and checks the status of the event again to make sure
 * the event was actually resolved
 *
 * NOTE: all the special string manipulation is needed because the CLI
 * appends extra newline character to end of the user input; normally
 * do not have to do all this special string manipulation
 */
void
try_resolve(char *event_name)
{
  TSMgmtError ret;
  char *name;

  name = TSstrdup(event_name);
  printf("[try_resolve] Resolving event: %s\n", name);

  if (check_active(name)) { // resolve events
    ret = TSEventResolve(name);
    print_err("TSEventResolve", ret);
    check_active(name); // should be non-active now
  }

  TSfree(name);
}

/* ------------------------------------------------------------------------
 * eventCallbackFn
 * ------------------------------------------------------------------------
 * the callback function; when called, it just prints out the name
 * of the event that was signalled
 */
void
eventCallbackFn(char *name, char *msg, int /* pri ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  printf("[eventCallbackFn] EVENT: %s, %s\n", name, msg);
  return;
}

/* ------------------------------------------------------------------------
 * register_event_callback
 * ------------------------------------------------------------------------
 * registers the eventCallbackFn above for all events; this just means
 * that for any event that's signalled, the callback fn will also be called
 */
void
register_event_callback()
{
  TSMgmtError err;

  printf("\n[register_event_callback] \n");
  err = TSEventSignalCbRegister(nullptr, eventCallbackFn, nullptr);
  print_err("TSEventSignalCbRegister", err);
}

/* ------------------------------------------------------------------------
 * unregister_event_callback
 * ------------------------------------------------------------------------
 * unregisters the eventCallbackFn above for all events; this just means
 * that it will remove this eventCallbackFn entirely so that for any
 * event called, the eventCallbackFn will NOT be called
 */
void
unregister_event_callback()
{
  TSMgmtError err;

  printf("\n[unregister_event_callback]\n");
  err = TSEventSignalCbUnregister(nullptr, eventCallbackFn);
  print_err("TSEventSignalCbUnregister", err);
}

/***************************************************************************
 * Statistics
 ***************************************************************************/

// generate dummy values for statistics
void
set_stats()
{
  TSActionNeedT action;

  fprintf(stderr, "[set_stats] Set Dummy Stat Values\n");

  TSRecordSetInt("proxy.process.http.user_agent_response_document_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.user_agent_response_header_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.current_client_connections", 100, &action);
  TSRecordSetInt("proxy.process.http.current_client_transactions", 100, &action);
  TSRecordSetInt("proxy.process.http.origin_server_response_document_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.origin_server_response_header_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.current_server_connections", 100, &action);
  TSRecordSetInt("proxy.process.http.current_server_transactions", 100, &action);

  TSRecordSetFloat("proxy.node.bandwidth_hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.hostdb.hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.cache_hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.cache_hit_mem_ratio", 110, &action);

  TSRecordSetInt("proxy.node.proxy_running", 110, &action);
  TSRecordSetInt("proxy.node.proxy_running", 110, &action);
}

void
print_stats()
{
  TSInt i1, i2, i3, i4, i5, i6, i7, i8;

  fprintf(stderr, "[print_stats]\n");

  TSRecordGetInt("proxy.process.http.user_agent_response_document_total_size", &i1);
  TSRecordGetInt("proxy.process.http.user_agent_response_header_total_size", &i2);
  TSRecordGetInt("proxy.process.http.current_client_connections", &i3);
  TSRecordGetInt("proxy.process.http.current_client_transactions", &i4);
  TSRecordGetInt("proxy.process.http.origin_server_response_document_total_size", &i5);
  TSRecordGetInt("proxy.process.http.origin_server_response_header_total_size", &i6);
  TSRecordGetInt("proxy.process.http.current_server_connections", &i7);
  TSRecordGetInt("proxy.process.http.current_server_transactions", &i8);

  fprintf(stderr, "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n", i1,
          i2, i3, i4, i5, i6, i7, i8);

  TSRecordGetInt("proxy.node.proxy_running", &i4);
  TSRecordGetInt("proxy.node.proxy_running", &i6);

  fprintf(stderr, "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n", i1, i7, i2, i3, i4,
          i5, i6);

  fprintf(stderr, "PROCESS stats: \n");
  fprintf(stderr, "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n", i1, i2, i3, i4);
}

void
reset_stats()
{
  TSMgmtError err = TSStatsReset(nullptr);
  print_err("TSStatsReset", err);
  return;
}

void
sync_test()
{
  TSActionNeedT action;

  TSRecordSetString("proxy.config.proxy_name", "dorkface", &action);
  printf("[TSRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n", TS_ACTION_UNDEFINED,
         action);

  TSMgmtError ret;
  if ((ret = TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_NONE)) != TS_ERR_OKAY) {
    printf("[TSProxyStateSet] turn off FAILED\n");
  }
  print_err("stop_TS", ret);
}

/* ########################################################################*/
/* ------------------------------------------------------------------------
 * runInteractive
 * ------------------------------------------------------------------------
 * the loop that processes the commands inputted by user
 */
static void
runInteractive()
{
  char buf[512]; // holds request from interactive prompt

  // process input from command line
  while (true) {
    // Display a prompt
    printf("api_cli-> ");

    // get input from command line
    ATS_UNUSED_RETURN(fgets(buf, 512, stdin));

    // check status of 'stdin' after reading
    if (feof(stdin) != 0) {
      printf("EXIT api_cli_remote\n");
      return;
    } else if (ferror(stdin) != 0) {
      printf("EXIT api_cli_remote\n");
      return;
    }
    // continue on newline
    if (strcmp(buf, "\n") == 0) {
      continue;
    }
    // exiting/quitting?
    if (strcasecmp("quit\n", buf) == 0 || strcasecmp("exit\n", buf) == 0) {
      // Don't wait for response LM
      // exit(0);
      return;
    }
    // check what operation user typed in
    if (strstr(buf, "state")) {
      print_proxy_state();
    } else if (strncmp(buf, "start", 5) == 0) {
      start_TS(buf);
    } else if (strstr(buf, "stop")) {
      stop_TS();
    } else if (strstr(buf, "restart")) {
      restart();
    } else if (strstr(buf, "reconfig")) {
      reconfigure();
    } else if (strstr(buf, "records")) {
      test_records();
    } else if (strstr(buf, "err_recs")) {
      test_error_records();
    } else if (strstr(buf, "get_mlt")) {
      test_record_get_mlt();
    } else if (strstr(buf, "set_mlt")) {
      test_record_set_mlt();
    } else if (strstr(buf, "proxy.")) {
      test_rec_get(buf);
    } else if (strstr(buf, "active_events")) {
      print_active_events();
    } else if (strstr(buf, "MGMT_ALARM_")) {
      try_resolve(buf);
    } else if (strncmp(buf, "register", 8) == 0) {
      register_event_callback();
    } else if (strstr(buf, "unregister")) {
      unregister_event_callback();
    } else if (strstr(buf, "read_url")) {
      test_read_url(true);
    } else if (strstr(buf, "test_url")) {
      test_read_url(false);
    } else if (strstr(buf, "reset_stats")) {
      reset_stats();
    } else if (strstr(buf, "set_stats")) {
      set_stats();
    } else if (strstr(buf, "print_stats")) {
      print_stats();
    } else {
      sync_test();
    }

  } // end while(1)

} // end runInteractive

/* ------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------
 * Main entry point which connects the client to the API, does any
 * clean up on exit, and gets the interactive command-line running
 */
int
main(int /* argc ATS_UNUSED */, char ** /* argv ATS_UNUSED */)
{
  TSMgmtError ret;

  if ((ret = TSInit(nullptr, TS_MGMT_OPT_DEFAULTS)) == TS_ERR_OKAY) {
    runInteractive();
    TSTerminate();
    printf("END REMOTE API TEST\n");
  } else {
    print_err("main", ret);
  }

  return 0;
} // end main()
