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

/****************************************************************************
 *
 *  WebHttpSession.cc - Manage session data
 *
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "ink_hash_table.h"
#include "ink_hash_table.h"

#include "mgmtapi.h"
#include "MgmtUtils.h"
#include "WebCompatibility.h"
#include "WebGlobals.h"
#include "WebHttpSession.h"

//#include "LocalManager.h"

#define SESSION_EXPIRES 600     // 10 minutes
#define CURRENT_SESSION_EXPIRES 100     // 10 minutes
#define SESSION_KEY_LEN 8

static InkHashTable *g_session_ht = 0;
static ink_mutex g_session_mutex;

struct session_ele
{
  time_t created;
  void *data;
  WebHttpSessionDeleter deleter_func;
};

//-------------------------------------------------------------------------
// deleter_main
//-------------------------------------------------------------------------

static void *
deleter_main(void *)
{
  time_t now;
  InkHashTableIteratorState htis;
  InkHashTableEntry *hte;
  char *key;
  session_ele *session;
  int session_count;
  while (1) {
    time(&now);
    ink_mutex_acquire(&wGlobals.submitLock);
    // mutex_try_acquire to avoid potential deadlocking; not so
    // critical that we don't delete these objects right now.
    session_count = 0;
    if (ink_mutex_try_acquire(&g_session_mutex)) {
      for (hte = ink_hash_table_iterator_first(g_session_ht, &htis);
           hte != NULL; hte = ink_hash_table_iterator_next(g_session_ht, &htis)) {
        key = (char *) ink_hash_table_entry_key(g_session_ht, hte);
        session = (session_ele *) ink_hash_table_entry_value(g_session_ht, hte);
        if (now - session->created > SESSION_EXPIRES) {
          ink_hash_table_delete(g_session_ht, key);
          session->deleter_func(session->data);
          ats_free(session);
        } else {
          session_count++;
        }
      }
      ink_mutex_release(&g_session_mutex);
    }
    ink_mutex_release(&wGlobals.submitLock);
    // random arbitrary heuristic
    mgmt_sleep_sec(SESSION_EXPIRES / 10);
  }
  return NULL;
}

//-------------------------------------------------------------------------
// InkMgmtApiCtxDeleter
//-------------------------------------------------------------------------

void
InkMgmtApiCtxDeleter(void *data)
{
  TSCfgContextDestroy((TSCfgContext) data);
}

//-------------------------------------------------------------------------
// WebHttpSessionInit
//-------------------------------------------------------------------------

void
WebHttpSessionInit()
{
  time_t now;
  time(&now);
  WebSeedRand((long) now);
  g_session_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_mutex_init(&g_session_mutex, "g_session_mutex");
  ink_thread_create(deleter_main, NULL);
}

//-------------------------------------------------------------------------
// WebHttpSessionStore
//-------------------------------------------------------------------------

int
WebHttpSessionStore(char *key, void *data, WebHttpSessionDeleter deleter_func)
{
  int err = WEB_HTTP_ERR_OKAY;
  session_ele *session;
  void *dummy;
  time_t now;

  if (!key || !data || !deleter_func) {
    return WEB_HTTP_ERR_FAIL;
  }
  ink_mutex_acquire(&g_session_mutex);
  if (ink_hash_table_lookup(g_session_ht, key, &dummy)) {
    err = WEB_HTTP_ERR_FAIL;
    goto Ldone;
  }
  time(&now);
  session = (session_ele *)ats_malloc(sizeof(session_ele));
  session->created = now;
  session->data = data;
  session->deleter_func = deleter_func;
  ink_hash_table_insert(g_session_ht, key, (void *) session);

Ldone:
  ink_mutex_release(&g_session_mutex);
  return err;
}

//-------------------------------------------------------------------------
// WebHttpSessionRetrieve
//-------------------------------------------------------------------------

int
WebHttpSessionRetrieve(char *key, void **data)
{
  int err;
  session_ele *session;
  if (!key) {
    return WEB_HTTP_ERR_FAIL;
  }
  ink_mutex_acquire(&g_session_mutex);
  if (!ink_hash_table_lookup(g_session_ht, key, (void **) &session)) {
    err = WEB_HTTP_ERR_FAIL;
  } else {
    *data = session->data;
    err = WEB_HTTP_ERR_OKAY;
  }
  ink_mutex_release(&g_session_mutex);
  return err;
}

//-------------------------------------------------------------------------
// WebHttpSessionDelete
//-------------------------------------------------------------------------

int
WebHttpSessionDelete(char *key)
{
  int err = WEB_HTTP_ERR_OKAY;
  session_ele *session;
  if (!key) {
    return WEB_HTTP_ERR_FAIL;
  }
  ink_mutex_acquire(&g_session_mutex);
  if (!ink_hash_table_lookup(g_session_ht, key, (void **) &session)) {
    err = WEB_HTTP_ERR_FAIL;
    goto Ldone;
  }
  ink_hash_table_delete(g_session_ht, key);
  session->deleter_func(session->data);
  ats_free(session);
Ldone:
  ink_mutex_release(&g_session_mutex);
  return err;
}

char *
WebHttpMakeSessionKey_Xmalloc()
{
  char *session_key_str = (char *)ats_malloc(SESSION_KEY_LEN + 2);
  long session_key = WebRand();
  // note: snprintf takes the buffer length, not the string
  // length? Add 2 to ats_malloc above to be safe.  ^_^
  snprintf(session_key_str, SESSION_KEY_LEN + 1, "%lx", session_key);
  return session_key_str;
}






