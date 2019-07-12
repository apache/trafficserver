/**
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

/**
 * @file utils_internal.cc
 */
#include <cassert>
#include <pthread.h>
#include "ts/ts.h"
#include "tscpp/api/GlobalPluginHooks.h"
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/SessionPluginHooks.h"
#include "tscpp/api/SessionPlugin.h"
#include "tscpp/api/Session.h"
#include "tscpp/api/TransactionPluginHooks.h"
#include "tscpp/api/TransactionPlugin.h"
#include "tscpp/api/Transaction.h"
#include "tscpp/api/TransformationPlugin.h"
#include "tscpp/api/utils.h"
#include "utils_internal.h"
#include "logging_internal.h"

using namespace atscppapi;

namespace
{
/// The index used to store required transaction based data.
int TRANSACTION_STORAGE_INDEX = -1;

/// The index used to store required session based data.
int SESSION_STORAGE_INDEX = -1;

void
resetTransactionHandles(Transaction &transaction, TSEvent event)
{
  utils::internal::resetTransactionHandles(transaction);
  return;
}

int
handleTransactionEvents(TSCont cont, TSEvent event, void *edata)
{
  // This function is only here to clean up Transaction objects
  TSHttpTxn ats_txn_handle     = static_cast<TSHttpTxn>(edata);
  Transaction *transaction_ptr = utils::internal::getTransaction(ats_txn_handle, false);
  if (transaction_ptr) {
    LOG_DEBUG("Got event %d on continuation %p for transaction (ats pointer %p, object %p)", event, cont, ats_txn_handle,
              transaction_ptr);

    Transaction &transaction = *transaction_ptr;
    utils::internal::setTransactionEvent(transaction, event);
    switch (event) {
    case TS_EVENT_HTTP_POST_REMAP:
      transaction.getClientRequest().getUrl().reset();
      // This is here to force a refresh of the cached client request url
      TSMBuffer hdr_buf;
      TSMLoc hdr_loc;
      (void)TSHttpTxnClientReqGet(static_cast<TSHttpTxn>(transaction.getAtsHandle()), &hdr_buf, &hdr_loc);
      break;
    case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    case TS_EVENT_HTTP_READ_CACHE_HDR:
      // the buffer handles may be destroyed in the core during redirect follow
      resetTransactionHandles(transaction, event);
      break;
    case TS_EVENT_HTTP_TXN_CLOSE: { // opening scope to declare plugins variable below
      resetTransactionHandles(transaction, event);
      const std::list<TransactionPlugin *> &plugins = utils::internal::getTransactionPlugins(transaction);
      for (auto plugin : plugins) {
        std::shared_ptr<Mutex> trans_mutex = utils::internal::getTransactionPluginMutex(*plugin);
        LOG_DEBUG("Locking TransactionPlugin mutex to delete transaction plugin at %p", plugin);
        trans_mutex->lock();
        LOG_DEBUG("Locked Mutex...Deleting transaction plugin at %p", plugin);
        delete plugin;
        trans_mutex->unlock();
      }
      delete &transaction;
    } break;
    default:
      assert(false); /* we should never get here */
      break;
    }
  }
  TSHttpTxnReenable(ats_txn_handle, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

int
handleSessionEvents(TSCont cont, TSEvent event, void *edata)
{
  // This function is only here to clean up Session objects
  TSHttpSsn ats_ssn_handle = static_cast<TSHttpSsn>(edata);
  Session *session_ptr     = utils::internal::getSession(ats_ssn_handle, false);
  if (session_ptr) {
    LOG_DEBUG("Got event %d on continuation %p for session (ats pointer %p, object %p)", event, cont, ats_ssn_handle, session_ptr);

    Session &session = *session_ptr;
    utils::internal::setSessionEvent(session, event);
    switch (event) {
    case TS_EVENT_HTTP_SSN_CLOSE: { // opening scope to declare plugins variable below
      const std::list<SessionPlugin *> &plugins = utils::internal::getSessionPlugins(session);
      for (auto plugin : plugins) {
        std::shared_ptr<Mutex> sess_mutex = utils::internal::getSessionPluginMutex(*plugin);
        LOG_DEBUG("Locking SessionPlugin mutex to delete session plugin at %p", plugin);
        sess_mutex->lock();
        LOG_DEBUG("Locked Mutex...Deleting session plugin at %p", plugin);
        delete plugin;
        sess_mutex->unlock();
      }
      delete &session;
    } break;
    default:
      assert(false); /* we should never get here */
      break;
    }
  }
  TSHttpSsnReenable(ats_ssn_handle, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
setupManagement()
{
  {
    // Reserve a transaction slot
    TSAssert(TS_SUCCESS == TSHttpTxnArgIndexReserve("atscppapi", "ATS CPP API", &TRANSACTION_STORAGE_INDEX));
    // We must always have a cleanup handler available
    TSMutex mutex = nullptr;
    TSCont cont   = TSContCreate(handleTransactionEvents, mutex);
    TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, cont);
    TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, cont);
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
    TSHttpHookAdd(TS_HTTP_READ_CACHE_HDR_HOOK, cont);
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, cont);
  }
  {
    // Reserve a session slot
    TSAssert(TS_SUCCESS == TSHttpSsnArgIndexReserve("atscppapi", "ATS CPP API", &SESSION_STORAGE_INDEX));
    // We must always have a cleanup handler available
    TSMutex mutex = nullptr;
    TSCont cont   = TSContCreate(handleSessionEvents, mutex);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, cont);
  }
}

} /* anonymous namespace */

Transaction *
utils::internal::getTransaction(TSHttpTxn ats_txn_handle, bool create)
{
  Transaction *transaction = static_cast<Transaction *>(TSHttpTxnArgGet(ats_txn_handle, TRANSACTION_STORAGE_INDEX));
  if (create && !transaction) {
    transaction = new Transaction(static_cast<void *>(ats_txn_handle));
    LOG_DEBUG("Created new transaction object at %p for ats pointer %p", transaction, ats_txn_handle);
    TSHttpTxnArgSet(ats_txn_handle, TRANSACTION_STORAGE_INDEX, transaction);
  }
  return transaction;
}

Session *
utils::internal::getSession(TSHttpSsn ats_ssn_handle, bool create)
{
  Session *session = static_cast<Session *>(TSHttpSsnArgGet(ats_ssn_handle, SESSION_STORAGE_INDEX));
  if (create && !session) {
    session = new Session(static_cast<void *>(ats_ssn_handle));
    LOG_DEBUG("Created new session object at %p for ats pointer %p", session, ats_ssn_handle);
    TSHttpSsnArgSet(ats_ssn_handle, SESSION_STORAGE_INDEX, session);
  }
  return session;
}

TSHttpHookID
utils::internal::convertInternalHookToTsHook(GlobalPluginHooks::HookType hooktype)
{
  using HookType = GlobalPluginHooks::HookType;

  switch (hooktype) {
  case HookType::HOOK_SELECT_ALT:
    return TS_HTTP_SELECT_ALT_HOOK;
  case HookType::HOOK_SSN_START:
    return TS_HTTP_SSN_START_HOOK;
  default:
    assert(false); // shouldn't happen, let's catch it early
    break;
  }
  return static_cast<TSHttpHookID>(-1);
}

TSHttpHookID
utils::internal::convertInternalHookToTsHook(SessionPluginHooks::HookType hooktype)
{
  using HookType = SessionPluginHooks::HookType;

  switch (hooktype) {
  case HookType::HOOK_TXN_START:
    return TS_HTTP_TXN_START_HOOK;
  default:
    assert(false); // shouldn't happen, let's catch it early
    break;
  }
  return static_cast<TSHttpHookID>(-1);
}

TSHttpHookID
utils::internal::convertInternalHookToTsHook(TransactionPluginHooks::HookType hooktype)
{
  using HookType = TransactionPluginHooks::HookType;

  switch (hooktype) {
  case HookType::HOOK_READ_REQUEST_HEADERS_POST_REMAP:
    return TS_HTTP_POST_REMAP_HOOK;
  case HookType::HOOK_READ_REQUEST_HEADERS_PRE_REMAP:
    return TS_HTTP_PRE_REMAP_HOOK;
  case HookType::HOOK_READ_RESPONSE_HEADERS:
    return TS_HTTP_READ_RESPONSE_HDR_HOOK;
  case HookType::HOOK_SEND_REQUEST_HEADERS:
    return TS_HTTP_SEND_REQUEST_HDR_HOOK;
  case HookType::HOOK_SEND_RESPONSE_HEADERS:
    return TS_HTTP_SEND_RESPONSE_HDR_HOOK;
  case HookType::HOOK_OS_DNS:
    return TS_HTTP_OS_DNS_HOOK;
  case HookType::HOOK_READ_REQUEST_HEADERS:
    return TS_HTTP_READ_REQUEST_HDR_HOOK;
  case HookType::HOOK_READ_CACHE_HEADERS:
    return TS_HTTP_READ_CACHE_HDR_HOOK;
  case HookType::HOOK_CACHE_LOOKUP_COMPLETE:
    return TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK;
  default:
    assert(false); // shouldn't happen, let's catch it early
    break;
  }
  return static_cast<TSHttpHookID>(-1);
}

TSHttpHookID
utils::internal::convertInternalTransformationTypeToTsHook(TransformationPlugin::Type type)
{
  switch (type) {
  case TransformationPlugin::RESPONSE_TRANSFORMATION:
    return TS_HTTP_RESPONSE_TRANSFORM_HOOK;
  case TransformationPlugin::REQUEST_TRANSFORMATION:
    return TS_HTTP_REQUEST_TRANSFORM_HOOK;
  case TransformationPlugin::SINK_TRANSFORMATION:
    return TS_HTTP_RESPONSE_CLIENT_HOOK;
  default:
    assert(false); // shouldn't happen, let's catch it early
    break;
  }
  return static_cast<TSHttpHookID>(-1);
}

std::string
utils::internal::consumeFromTSIOBufferReader(TSIOBufferReader reader)
{
  std::string str;
  int avail = TSIOBufferReaderAvail(reader);

  if (avail != TS_ERROR) {
    int consumed = 0;
    if (avail > 0) {
      str.reserve(avail + 1);

      int64_t data_len;
      const char *char_data;
      TSIOBufferBlock block = TSIOBufferReaderStart(reader);
      while (block != nullptr) {
        char_data = TSIOBufferBlockReadStart(block, reader, &data_len);
        str.append(char_data, data_len);
        consumed += data_len;
        block = TSIOBufferBlockNext(block);
      }
    }
    TSIOBufferReaderConsume(reader, consumed);
  } else {
    LOG_ERROR("TSIOBufferReaderAvail returned error code %d for reader %p", avail, reader);
  }

  return str;
}

HttpVersion
utils::internal::getHttpVersion(TSMBuffer hdr_buf, TSMLoc hdr_loc)
{
  int version = TSHttpHdrVersionGet(hdr_buf, hdr_loc);
  if (version != TS_ERROR) {
    if ((TS_HTTP_MAJOR(version) == 0) && (TS_HTTP_MINOR(version) == 0)) {
      return HTTP_VERSION_0_9;
    }
    if ((TS_HTTP_MAJOR(version) == 1) && (TS_HTTP_MINOR(version) == 0)) {
      return HTTP_VERSION_1_0;
    }
    if ((TS_HTTP_MAJOR(version) == 1) && (TS_HTTP_MINOR(version) == 1)) {
      return HTTP_VERSION_1_1;
    } else {
      LOG_ERROR("Unrecognized version %d", version);
    }
  } else {
    LOG_ERROR("Could not get version; hdr_buf %p, hdr_loc %p", hdr_buf, hdr_loc);
  }
  return HTTP_VERSION_UNKNOWN;
}

void
utils::internal::initManagement()
{
  static pthread_once_t setup_pthread_once_control = PTHREAD_ONCE_INIT;
  pthread_once(&setup_pthread_once_control, setupManagement);
}

std::shared_ptr<Mutex>
utils::internal::getTransactionPluginMutex(TransactionPlugin &transaction_plugin)
{
  return transaction_plugin.getMutex();
}

std::shared_ptr<Mutex>
utils::internal::getSessionPluginMutex(SessionPlugin &session_plugin)
{
  return session_plugin.getMutex();
}
