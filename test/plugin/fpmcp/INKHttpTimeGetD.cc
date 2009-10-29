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

/* 
 * Plugin passes if there are no interface errors
 * The plugin determines if the test test pass/fail. 
 * It sends back 500 to the client or logs an error error.log
*/

#include "InkAPI.h"
#include "InkAPIPrivate.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/time.h>

// Gloval variables because need to be compared accross different hooks
double PLUGIN_START_TIME = 0;

/* structure to store the TXN_START_TIME, specific to each transaction */
typedef struct
{
  double TXN_START_TIME;
} Txntimes;

void
startTransaction(INKHttpTxn txnp)
{
  Txntimes *txntimes;
  txntimes = (Txntimes *) INKmalloc(sizeof(Txntimes));
  txntimes->TXN_START_TIME = 0.0;
  INKContDataSet(txnp, txntimes);
}

void
Txntimes_destroy(INKHttpTxn txnp)
{
  Txntimes *txntimes;
  txntimes = (Txntimes *) INKContDataGet(txnp);
  INKfree(txntimes);
}

/* Returns 1 on success, i.e. if the |x-y|<10000, else 0 */
int
close_enough(double x, double y)
{
  double z;
  if (x < y)
    z = y - x;
  else
    z = x - y;

  INKDebug("INKHttpTimeGetD", "\n close_enough: difference = %0.2f", z);

  if (z > 10000.0)
    return 0;
  else
    return 1;

}

/* Return the average */
double
average(double x, double y)
{
  return ((x + y) / 2);
}


/* Returns 1 on success, 0 or value of "re" on failure */
static int
TxnStart(INKHttpTxn txnp)
{
  int re = 1;
  struct timeval time1, time2;
  double time1_value = 0, time2_value = 0, current_time = 0;

  Txntimes *txntimes;
  txntimes = (Txntimes *) INKContDataGet(txnp);

  // Make the 3 calls in a row
  gettimeofday(&time1, NULL);
  current_time = INKBasedTimeGetD();
  gettimeofday(&time2, NULL);

  // Get current time1
  time1_value = time1.tv_sec * 1000000.0 + time1.tv_usec;
  INKDebug("INKHttpTimeGetD", "\n TxnStart: gettimeofday1_value = %0.2f", time1_value);

  // Call INKBasedTimeGetD: need to convert nanoseconds to microseconds
  current_time = current_time / 1000.0;
  INKDebug("INKHttpTimeGetD", "\n TxnStart: INKBasedTimeGetD = %0.2f", current_time);

  // Get current time2
  time2_value = time2.tv_sec * 1000000.0 + time2.tv_usec;
  INKDebug("INKHttpTimeGetD", "\n TxnStart: gettimeofday2_value = %0.2f", time2_value);

  // Take the average gettimeofday
  time1_value = average(time1_value, time2_value);
  INKDebug("INKHttpTimeGetD", "\n TxnStart: average gettimeofday = %0.2f", time1_value);

  // Check |time1 - TXN_START_TIME| < 10 ms
  if (!(re = close_enough(time1_value, current_time))) {
    INKError("TxnStart: gettimeofday - INKBasedTimeGetD = %0.2f bigger then 10 ms", time1_value - current_time);
    INKDebug("INKHttpTimeGetD",
             "\n TxnStart: gettimeofday - INKBasedTimeGetD = %0.2f bigger then 10 ms", time1_value - current_time);
    goto done;
  }
  // Call INKHttpTxnStartTimeGetD
  if (!(re = INKHttpTxnStartTimeGetD(txnp, &(txntimes->TXN_START_TIME)))) {
    INKError("TxnStart: INKHttpTxnStartTimeGetD failed");
    INKDebug("INKHttpTimeGetD", "\n TxnStart: INKHttpTxnStartTimeGetD failed");
    goto done;
  }
  // Convert ns to us
  (txntimes->TXN_START_TIME) = (txntimes->TXN_START_TIME) / 1000.0;
  INKDebug("INKHttpTimeGetD", "\n TxnStart: TXN_START_TIME = %0.2f", (txntimes->TXN_START_TIME));

  // Check TXN_START_TIME > PLUGIN_START_TIME
  if (!((txntimes->TXN_START_TIME) > PLUGIN_START_TIME)) {
    INKError("TxnStart: TXN_START_TIME is not bigger then PLUGIN_START_TIME");
    INKDebug("INKHttpTimeGetD", "\n TxnStart: TXN_START_TIME is not bigger then PLUGIN_START_TIME");
    re = 0;
    goto done;
  }

done:
  return re;
}

/* Returns 1 on success, 0 or value of "re" on failure */
static int
TxnEnd(INKHttpTxn txnp)
{
  int re = 1;
  double TXN_END_TIME = 0;
  Txntimes *txntimes;
  txntimes = (Txntimes *) INKContDataGet(txnp);

  struct timeval time1, time2;
  double time1_value = 0, time2_value = 0, current_time = 0;

  // Make the 3 calls in a row
  gettimeofday(&time1, NULL);
  current_time = INKBasedTimeGetD();
  gettimeofday(&time2, NULL);

  // Get current time1
  time1_value = time1.tv_sec * 1000000.0 + time1.tv_usec;
  INKDebug("INKHttpTimeGetD", "\n TxnEnd: gettimeofday1_value = %0.2f", time1_value);

  // Call INKBasedTimeGetD: need to convert nanoseconds to microseconds
  current_time = current_time / 1000.0;
  INKDebug("INKHttpTimeGetD", "\n TxnEnd: INKBasedTimeGetD = %0.2f", current_time);

  // Get current time2
  time2_value = time2.tv_sec * 1000000.0 + time2.tv_usec;
  INKDebug("INKHttpTimeGetD", "\n TxnEnd: gettimeofday2_value = %0.2f", time2_value);

  // Take the average gettimeofday
  time1_value = average(time1_value, time2_value);
  INKDebug("INKHttpTimeGetD", "\n TxnEnd: average gettimeofday = %0.2f", time1_value);

  // Check |time1 - TXN_START_TIME| < 10 ms
  if (!(re = close_enough(time1_value, current_time))) {
    INKError("TxnEnd: gettimeofday - INKBasedTimeGetD = %0.2f bigger then 10 ms", time1_value - current_time);
    INKDebug("INKHttpTimeGetD", "\n TxnEnd: gettimeofday - INKBasedTimeGetD is bigger then 10 ms");
    goto done;
  }
  // Call INKHttpTxnEndTimeGetD
  if (!(re = INKHttpTxnEndTimeGetD(txnp, &TXN_END_TIME))) {
    INKError("TxnEnd: INKHttpTxnEndTimeGetD failed");
    INKDebug("INKHttpTimeGetD", "\n TxnEnd: INKHttpTxnEndTimeGetD failed");
    goto done;
  }
  // Convert ns to us
  TXN_END_TIME = TXN_END_TIME / 1000.0;
  INKDebug("INKHttpTimeGetD", "\n TxnEnd: TXN_END_TIME = %0.2f", TXN_END_TIME);

  // Check TXN_END_TIME > TXN_START_TIME
  if (!(TXN_END_TIME >= (txntimes->TXN_START_TIME))) {
    INKError("TxnEnd: TXN_END_TIME is not bigger then TXN_START_TIME");
    INKDebug("INKHttpTimeGetD", "\n TxnEnd: TXN_END_TIME is not bigger then TXN_START_TIME");
    re = 0;
    goto done;
  }

done:
  // Free the txn structure allocated in the begining of the transaction
  INKfree(txntimes);
  return re;
}

static int
handle_event_TimeGetD(INKCont contp, INKEvent event, void *edata)
{
  int re = 1;
  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKDebug("INKHttpTimeGetD", "\n handle_event(txn=0x%08x, event=%d)", txnp, event);
  switch (event) {

  case INK_EVENT_HTTP_TXN_START:
    startTransaction(txnp);
    if (!(re = TxnStart(txnp))) {
      INKDebug("INKHttpTimeGetD", "\n TxnStart failed");
      INKError("TxnStart failed");
    }
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    if (!(re = TxnEnd(txnp))) {
      INKDebug("INKHttpTimeGetD", "\n TxnEnd failed");
      INKError("TxnEnd failed");
    }
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    INKDebug("INKHttpTimeGetD", "undefined event %d", event);
    break;
  }
  return re;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKCont contp;
  struct timeval Begin;

  // Set plugin init time
  gettimeofday(&Begin, NULL);
  PLUGIN_START_TIME = Begin.tv_sec * 1000000.0 + Begin.tv_usec;
  INKDebug("INKHttpTimeGetD", "PLUGIN_START_TIME = %0.2f", PLUGIN_START_TIME);

  contp = INKContCreate(handle_event_TimeGetD, NULL);

  // hook to get the txn start time
  INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, contp);

  // hook to get the txn end time
  INKHttpHookAdd(INK_HTTP_TXN_CLOSE_HOOK, contp);
}
