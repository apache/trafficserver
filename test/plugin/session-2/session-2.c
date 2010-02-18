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
* -- INKHttpSsn*
* -- INKConfig*
* -- INKStat*
* -- INKThread*
*
* It does the following things:
* 1. Create a thread and destroy it in INKPluginInit()
* 2. Create three INKStat statistic variables transaction_count, session_count
*    and avg_transactions and update them at every new session or transaction.
* 3. Play with INKConfig family of functions to set and get config data.
******************************************************************************/
#include <stdio.h>
#include <pthread.h>
#include "ts.h"

#define DEBUG_TAG "session-2-dbg"
#define SLEEP_TIME 10

#define PLUGIN_NAME "session-2"
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
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
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


INKThread ink_tid;

typedef struct
{
  INK64 num_ssns;
} ConfigData;

static unsigned int my_id = 0;

static INKStat transaction_count;
static INKStat session_count;
static INKStat avg_transactions;

/*********************************************************************
 * constructor for plugin config data
 ********************************************************************/
static ConfigData *
plugin_config_constructor()
{
  ConfigData *data;

  data = (ConfigData *) INKmalloc(sizeof(ConfigData));
  data->num_ssns = 0;
  return data;
}

/*********************************************************************
 * destructor for plugin config data
 ********************************************************************/
static void
plugin_config_destructor(void *data)
{
  INKfree((ConfigData *) data);
}

/**********************************************************************
 * Update the statistic variables using the INKStat family of functions
 *********************************************************************/
static void
txn_handler(INKHttpTxn txnp, INKCont contp)
{
  LOG_SET_FUNCTION_NAME("txn_handler");
  INK64 num_txns;
  INK64 num_ssns;
  float old_avg_txns, new_avg_txns;

  /* negative test for INKStatIncrement */
#ifdef DEBUG
  if (INKStatIncrement(NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKStatIncrement");

#endif

  if (INKStatIncrement(transaction_count) == INK_ERROR)
    LOG_ERROR("INKStatIncrement");

  if (INKStatIntGet(transaction_count, &num_txns) == INK_ERROR)
    LOG_ERROR("INKStatIntGet");
  if (INKStatIntGet(session_count, &num_ssns) == INK_ERROR)
    LOG_ERROR("INKStatIntGet");
  if (INKStatFloatGet(avg_transactions, &old_avg_txns) == INK_ERROR)
    LOG_ERROR("INKStatFloatGet");

  if (num_ssns > 0) {
    new_avg_txns = (float) num_txns / num_ssns;
  } else {
    new_avg_txns = 0;
  }
  if (INKStatFloatSet(avg_transactions, new_avg_txns) == INK_ERROR)
    LOG_ERROR("INKStatFloatSet");

  /*negative test for INKStatFloatSet */
#ifdef DEBUG
  if (INKStatFloatSet(NULL, new_avg_txns) != INK_ERROR)
    LOG_ERROR_NEG("INKStatFloatSet");
#endif

  INKDebug(DEBUG_TAG, "The number of transactions is %ld\n", (long) num_txns);
  INKDebug(DEBUG_TAG, "The previous number of average transactions per session is %.2f\n", old_avg_txns);
  INKDebug(DEBUG_TAG, "The current number of average transactions per session is %.2f\n", new_avg_txns);

}

/**********************************************************************
 * Update session_count with both INKStat* and INKConfig* functions
 *********************************************************************/
static void
handle_session(INKHttpSsn ssnp, INKCont contp)
{
  LOG_SET_FUNCTION_NAME("handle_session");
  INK64 num_ssns;
  INKConfig config_ptr;
  ConfigData *config_data;

  /* negative test for INKStatIntAddTo */
#ifdef DEBUG
  if (INKStatIntAddTo(NULL, 1) != INK_ERROR)
    LOG_ERROR_NEG("INKStatIntAddTo");
#endif

  /* update the session_count with INKStat functions */
  if (INKStatIntAddTo(session_count, 1) == INK_ERROR)
    LOG_ERROR("INKStatIntAddTo");
  if (INKStatIntGet(session_count, &num_ssns) == INK_ERROR)
    LOG_ERROR("INKStatIntGet");
  INKDebug(DEBUG_TAG, "The number of sessions from INKStat is %ld\n", (long) num_ssns);


  /* get the config data and update it */
  config_ptr = INKConfigGet(my_id);
  config_data = (ConfigData *) INKConfigDataGet(config_ptr);
  config_data->num_ssns++;
  INKDebug(DEBUG_TAG, "The number of sessions from INKConfig is %ld\n", (long) config_data->num_ssns);
  INKConfigRelease(my_id, config_ptr);


  /* add the session hook */
  if (INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp) == INK_ERROR)
    LOG_ERROR("INKHttpSsnHookAdd");

  /* negative test for INKHttpSsnHookAdd */
#ifdef DEBUG
  if (INKHttpSsnHookAdd(NULL, INK_HTTP_TXN_START_HOOK, contp) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpSsnHookAdd");
  if (INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpSsnHookAdd");
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
  INKThread self;

  if ((ink_tid = INKThreadInit()) == INK_ERROR_PTR || ink_tid == NULL)
    LOG_ERROR("INKThreadInit");
  if ((self = INKThreadSelf()) == INK_ERROR_PTR || self == NULL)
    LOG_ERROR("INKThreadSelf");
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
  if (INKThreadDestroy(ink_tid) == INK_ERROR)
    LOG_ERROR("INKThreadDestroy");
}

static int
ssn_handler(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("ssn_handler");
  INKHttpSsn ssnp;
  INKHttpTxn txnp;

  switch (event) {
  case INK_EVENT_HTTP_SSN_START:
    ssnp = (INKHttpSsn) edata;
    handle_session(ssnp, contp);
    if (INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR)
      LOG_ERROR("INKHttpSsnReenable");

    /* negative test for INKHttpSsnReenable */
#ifdef DEBUG
    if (INKHttpSsnReenable(NULL, INK_EVENT_HTTP_CONTINUE) != INK_ERROR)
      LOG_ERROR_NEG("INKHttpSsnReenable");
#endif
    return 0;

  case INK_EVENT_HTTP_TXN_START:
    txnp = (INKHttpTxn) edata;
    txn_handler(txnp, contp);
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR)
      LOG_ERROR("INKHttpTxnReenable");
    return 0;

  default:
    INKDebug(DEBUG_TAG, "In the default case: event = %d\n", event);
    break;
  }
  return 0;
}

void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPluginInit");
  INKCont contp;
  ConfigData *config_data;

  thread_handler();

  /* create the statistic variables */
  transaction_count = INKStatCreate("transaction.count", INKSTAT_TYPE_INT64);
  session_count = INKStatCreate("session.count", INKSTAT_TYPE_INT64);
  avg_transactions = INKStatCreate("avg.transactions", INKSTAT_TYPE_FLOAT);
  if ((transaction_count == INK_ERROR_PTR) || (session_count == INK_ERROR_PTR) || (avg_transactions == INK_ERROR_PTR)) {
    LOG_ERROR("INKStatCreate");
    return;
  }

  /* negative test for INKStatCreate */
#ifdef DEBUG
  if (INKStatCreate(NULL, INKSTAT_TYPE_INT64) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCreate");
  }
  if (INKStatCreate("transaction.negtest", -1) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCreate");
  }
#endif

  /* create config data for plugin and assign it an identifier with INKConfigSet */
  config_data = plugin_config_constructor();

  my_id = INKConfigSet(my_id, config_data, plugin_config_destructor);

  /* create the continuation */
  if ((contp = INKContCreate(ssn_handler, NULL)) == INK_ERROR_PTR)
    LOG_ERROR("INKContCreate")
      else
    INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, contp);
}
