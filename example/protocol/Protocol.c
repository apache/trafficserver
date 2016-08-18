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

#include "Protocol.h"
#include "TxnSM.h"
#include "ts/ink_defs.h"
#include <math.h>

/* global variable */
TSTextLogObject protocol_plugin_log;

/* static variable */
static TSAction pending_action;
static int accept_port;
static int server_port;

/* Functions only seen in this file, should be static. */
static void protocol_init(int accept_port, int server_port);
static int accept_handler(TSCont contp, TSEvent event, void *edata);

/* When the handle is called, the net_vc is returned. */
static int
accept_handler(TSCont contp, TSEvent event, void *edata)
{
  TSCont txn_sm;
  TSMutex pmutex;

  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    /* Create a new mutex for the TxnSM, which is going
       to handle the incoming request. */
    pmutex = (TSMutex)TSMutexCreate();
    txn_sm = (TSCont)TxnSMCreate(pmutex, (TSVConn)edata, server_port);

    /* This is no reason for not grabbing the lock.
       So skip the routine which handle LockTry failure case. */
    TSMutexLockTry(pmutex); // TODO: why should it not check if we got the lock??
    TSContCall(txn_sm, 0, NULL);
    TSMutexUnlock(pmutex);
    break;

  default:
    /* Something wrong with the network, if there are any
       pending NetAccept, cancel them. */
    if (pending_action && !TSActionDone(pending_action))
      TSActionCancel(pending_action);

    TSContDestroy(contp);
    break;
  }

  return TS_EVENT_NONE;
}

static void
protocol_init(int accept_port, int server_port ATS_UNUSED)
{
  TSCont contp;
  int ret_val;

  /* create customized log */
  ret_val = TSTextLogObjectCreate("protocol", TS_LOG_MODE_ADD_TIMESTAMP, &protocol_plugin_log);
  if (ret_val != TS_SUCCESS) {
    TSError("[protocol] Failed to create log");
  }

  /* format of the log entries, for caching_status, 1 for HIT and 0 for MISS */
  ret_val = TSTextLogObjectWrite(protocol_plugin_log, "timestamp filename servername caching_status\n\n");
  if (ret_val != TS_SUCCESS) {
    TSError("[protocol] Failed to write into log");
  }

  contp = TSContCreate(accept_handler, TSMutexCreate());

  /* Accept network traffic from the accept_port.
     When there are requests coming in, contp's handler
     should be called, in this case, contp's handler
     is accept_event, see AcceptSM.c */
  pending_action = TSNetAccept(contp, accept_port, -1, 1);
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  char *end;
  int tmp;

  info.plugin_name   = "output-header";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[protocol] Plugin registration failed.");

    goto error;
  }

  /* default value */
  accept_port = 4666;
  server_port = 4666;

  if (argc < 3) {
    TSDebug("protocol", "Usage: protocol.so accept_port server_port");
    printf("[protocol_plugin] Usage: protocol.so accept_port server_port\n");
    printf("[protocol_plugin] Wrong arguments. Using deafult ports.\n");
  } else {
    tmp = strtol(argv[1], &end, 10);
    if (*end == '\0') {
      accept_port = tmp;
      TSDebug("protocol", "using accept_port %d", accept_port);
      printf("[protocol_plugin] using accept_port %d\n", accept_port);
    } else {
      printf("[protocol_plugin] Wrong argument for accept_port.");
      printf("Using deafult port %d\n", accept_port);
    }

    tmp = strtol(argv[2], &end, 10);
    if (*end == '\0') {
      server_port = tmp;
      TSDebug("protocol", "using server_port %d", server_port);
      printf("[protocol_plugin] using server_port %d\n", server_port);
    } else {
      printf("[protocol_plugin] Wrong argument for server_port.");
      printf("Using deafult port %d\n", server_port);
    }
  }

  protocol_init(accept_port, server_port);

error:
  TSError("[protocol] Plugin not initialized");
}
