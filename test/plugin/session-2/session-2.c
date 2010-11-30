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

/********************************************************************************
* This plugin tries to cover the APIs in the following categories:
* -- TSHttpSsn*
* -- TSConfig*
* -- TSStat*
* -- TSThread*
*
* It does the following things:
* 1. Create a thread and destroy it in TSPluginInit()
* 2. Create three TSStat statistic variables transaction_count, session_count
*    and avg_transactions and update them at every new session or transaction.
* 3. Play with TSConfig family of functions to set and get config data.
******************************************************************************/
#include <stdio.h>
#include <pthread.h>
#include "ts.h"

#define DEBUG_TAG "session-2-dbg"
#define SLEEP_TIME 10

#define PLUGIN_NAME "session-2"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


TSThread ink_tid;

typedef struct
{
  TS64 num_ssns;
} ConfigData;

static unsigned int my_id = 0;

static TSStat transaction_count;
static TSStat session_count;
static TSStat avg_transactions;

/*********************************************************************
 * constructor for plugin config data
 ********************************************************************/
static ConfigData *
plugin_config_constructor()
{
  ConfigData *data;

  data = (ConfigData *) TSmalloc(sizeof(ConfigData));
  data->num_ssns = 0;
  return data;
}

/*********************************************************************
 * destructor for plugin config data
 ********************************************************************/
static void
plugin_config_destructor(void *data)
{
  TSfree((ConfigData *) data);
}

/**********************************************************************
 * Update the statistic variables using the TSStat family of functions
 *********************************************************************/
static void
txn_handler(TSHttpTxn txnp, TSCont contp)
{
  LOG_SET_FUNCTION_NAME("txn_handler");
  TS64 num_txns;
  TS64 num_ssns;
  float old_avg_txns, new_avg_txns;

  /* negative test for TSStatIncrement */
#ifdef DEBUG
  if (TSStatIncrement(NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSStatIncrement");

#endif

  if (TSStatIncrement(transaction_count) == TS_ERROR)
    LOG_ERROR("TSStatIncrement");

  if (TSStatIntGet(transaction_count, &num_txns) == TS_ERROR)
    LOG_ERROR("TSStatIntGet");
  if (TSStatIntGet(session_count, &num_ssns) == TS_ERROR)
    LOG_ERROR("TSStatIntGet");
  if (TSStatFloatGet(avg_transactions, &old_avg_txns) == TS_ERROR)
    LOG_ERROR("TSStatFloatGet");

  if (num_ssns > 0) {
    new_avg_txns = (float) num_txns / num_ssns;
  } else {
    new_avg_txns = 0;
  }
  if (TSStatFloatSet(avg_transactions, new_avg_txns) == TS_ERROR)
    LOG_ERROR("TSStatFloatSet");

  /*negative test for TSStatFloatSet */
#ifdef DEBUG
  if (TSStatFloatSet(NULL, new_avg_txns) != TS_ERROR)
    LOG_ERROR_NEG("TSStatFloatSet");
#endif

  TSDebug(DEBUG_TAG, "The number of transactions is %ld\n", (long) num_txns);
  TSDebug(DEBUG_TAG, "The previous number of average transactions per session is %.2f\n", old_avg_txns);
  TSDebug(DEBUG_TAG, "The current number of average transactions per session is %.2f\n", new_avg_txns);

}

/**********************************************************************
 * Update session_count with both TSStat* and TSConfig* functions
 *********************************************************************/
static void
handle_session(TSHttpSsn ssnp, TSCont contp)
{
  LOG_SET_FUNCTION_NAME("handle_session");
  TS64 num_ssns;
  TSConfig config_ptr;
  ConfigData *config_data;

  /* negative test for TSStatIntAddTo */
#ifdef DEBUG
  if (TSStatIntAddTo(NULL, 1) != TS_ERROR)
    LOG_ERROR_NEG("TSStatIntAddTo");
#endif

  /* update the session_count with TSStat functions */
  if (TSStatIntAddTo(session_count, 1) == TS_ERROR)
    LOG_ERROR("TSStatIntAddTo");
  if (TSStatIntGet(session_count, &num_ssns) == TS_ERROR)
    LOG_ERROR("TSStatIntGet");
  TSDebug(DEBUG_TAG, "The number of sessions from TSStat is %ld\n", (long) num_ssns);


  /* get the config data and update it */
  config_ptr = TSConfigGet(my_id);
  config_data = (ConfigData *) TSConfigDataGet(config_ptr);
  config_data->num_ssns++;
  TSDebug(DEBUG_TAG, "The number of sessions from TSConfig is %ld\n", (long) config_data->num_ssns);
  TSConfigRelease(my_id, config_ptr);


  /* add the session hook */
  if (TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, contp) == TS_ERROR)
    LOG_ERROR("TSHttpSsnHookAdd");

  /* negative test for TSHttpSsnHookAdd */
#ifdef DEBUG
  if (TSHttpSsnHookAdd(NULL, TS_HTTP_TXN_START_HOOK, contp) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpSsnHookAdd");
  if (TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpSsnHookAdd");
#endif
}

/************************************************************************
 * Thread function. Return itself and sleep for a while
 ***********************************************************************/
static void *
thread_func(void *arg)
{
  LOG_SET_FUNCTION_NAME("thread_func");
  int sleep_time = (int) arg;
  TSThread self;

  if ((ink_tid = TSThreadInit()) == TS_ERROR_PTR || ink_tid == NULL)
    LOG_ERROR("TSThreadInit");
  if ((self = TSThreadSelf()) == TS_ERROR_PTR || self == NULL)
    LOG_ERROR("TSThreadSelf");
  sleep(sleep_time);
  return NULL;
}

/************************************************************************
 * Create a thread then destroy it
 ***********************************************************************/
static void
thread_handler()
{
  LOG_SET_FUNCTION_NAME("thread_handler");
  pthread_t tid;

  pthread_create(&tid, NULL, thread_func, (void *) SLEEP_TIME);
  sleep(5);
  if (TSThreadDestroy(ink_tid) == TS_ERROR)
    LOG_ERROR("TSThreadDestroy");
}

static int
ssn_handler(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("ssn_handler");
  TSHttpSsn ssnp;
  TSHttpTxn txnp;

  switch (event) {
  case TS_EVENT_HTTP_SSN_START:
    ssnp = (TSHttpSsn) edata;
    handle_session(ssnp, contp);
    if (TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR)
      LOG_ERROR("TSHttpSsnReenable");

    /* negative test for TSHttpSsnReenable */
#ifdef DEBUG
    if (TSHttpSsnReenable(NULL, TS_EVENT_HTTP_CONTINUE) != TS_ERROR)
      LOG_ERROR_NEG("TSHttpSsnReenable");
#endif
    return 0;

  case TS_EVENT_HTTP_TXN_START:
    txnp = (TSHttpTxn) edata;
    txn_handler(txnp, contp);
    if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR)
      LOG_ERROR("TSHttpTxnReenable");
    return 0;

  default:
    TSDebug(DEBUG_TAG, "In the default case: event = %d\n", event);
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");
  TSCont contp;
  ConfigData *config_data;

  thread_handler();

  /* create the statistic variables */
  transaction_count = TSStatCreate("transaction.count", TSSTAT_TYPE_INT64);
  session_count = TSStatCreate("session.count", TSSTAT_TYPE_INT64);
  avg_transactions = TSStatCreate("avg.transactions", TSSTAT_TYPE_FLOAT);
  if ((transaction_count == TS_ERROR_PTR) || (session_count == TS_ERROR_PTR) || (avg_transactions == TS_ERROR_PTR)) {
    LOG_ERROR("TSStatCreate");
    return;
  }

  /* negative test for TSStatCreate */
#ifdef DEBUG
  if (TSStatCreate(NULL, TSSTAT_TYPE_INT64) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCreate");
  }
  if (TSStatCreate("transaction.negtest", -1) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCreate");
  }
#endif

  /* create config data for plugin and assign it an identifier with TSConfigSet */
  config_data = plugin_config_constructor();

  my_id = TSConfigSet(my_id, config_data, plugin_config_destructor);

  /* create the continuation */
  if ((contp = TSContCreate(ssn_handler, NULL)) == TS_ERROR_PTR)
    LOG_ERROR("TSContCreate")
      else
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);
}
