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
 * @file Cleanup.h
 * @brief Easy-to-use utilities to avoid resource leaks or double-releases of resources.  Independent of the rest
 *        of the CPPAPI.
 */

#pragma once

#include <type_traits>
#include <memory>

#include <ts/ts.h>

namespace atscppapi
{
// For TS API types TSXxx with a TSXxxDestroy function, define standard deleter TSXxxDeleter, and use it to
// define TSXxxUniqPtr (specialization of std::unique_ptr).  X() is used when the destroy function returns void,
// Y() is used when the destroy function returns TSReturnCode.

#if defined(X)
#error "X defined as preprocessor symbol"
#endif

#define X(NAME_SEGMENT)               \
  struct TS##NAME_SEGMENT##Deleter {  \
    void                              \
    operator()(TS##NAME_SEGMENT ptr)  \
    {                                 \
      TS##NAME_SEGMENT##Destroy(ptr); \
    }                                 \
  };                                  \
  using TS##NAME_SEGMENT##UniqPtr = std::unique_ptr<std::remove_pointer_t<TS##NAME_SEGMENT>, TS##NAME_SEGMENT##Deleter>;

#if defined(Y)
#error "Y defined as preprocessor symbol"
#endif

#define Y(NAME_SEGMENT)                                       \
  struct TS##NAME_SEGMENT##Deleter {                          \
    void                                                      \
    operator()(TS##NAME_SEGMENT ptr)                          \
    {                                                         \
      TSAssert(TS##NAME_SEGMENT##Destroy(ptr) == TS_SUCCESS); \
    }                                                         \
  };                                                          \
  using TS##NAME_SEGMENT##UniqPtr = std::unique_ptr<std::remove_pointer_t<TS##NAME_SEGMENT>, TS##NAME_SEGMENT##Deleter>;

Y(MBuffer)       // Defines TSMBufferDeleter and TSMBufferUniqPtr.
X(MimeParser)    // Defines TSMimeParserDeleter and TSMimeParserUniqPtr.
X(Thread)        // Defines TSThreadDeleter and TSThreadUniqPtr.
X(Mutex)         // Defines TSMutexDeleter and TSMutexUniqPtr.
Y(CacheKey)      // Defines TSCacheKeyDeleter and TSCacheKeyUniqPtr.
X(Cont)          // Defines TSContDeleter and TSContUniqPtr.
X(SslContext)    // Defines TSSslContextDeleter and TSSslContextUniqPtr.
X(IOBuffer)      // Defines TSIOBufferDeleter and TSIOBufferUniqPtr.
Y(TextLogObject) // Defines TSTextLogObjectDeleter and TSTextLogObjectUniqPtr.
X(Uuid)          // Defines TSUuidDeleter and TSUuidUniqPtr.

#undef X
#undef Y

// Deleter and unique pointer for memory buffer returned by TSalloc(), TSrealloc(), Tstrdup(), TSsrtndup().
//
struct TSMemDeleter {
  void
  operator()(void *ptr)
  {
    TSfree(ptr);
  }
};
using TSMemUniqPtr = std::unique_ptr<void, TSMemDeleter>;

// Deleter and unique pointer for TSIOBufferReader.  Care must be taken that the reader is deleted before the
// TSIOBuffer to which it refers is deleted.
//
struct TSIOBufferReaderDeleter {
  void
  operator()(TSIOBufferReader ptr)
  {
    TSIOBufferReaderFree(ptr);
  }
};
using TSIOBufferReaderUniqPtr = std::unique_ptr<std::remove_pointer_t<TSIOBufferReader>, TSIOBufferReaderDeleter>;

class TxnAuxDataMgrBase
{
protected:
  struct MgrData_ {
    TSCont txnContp = nullptr;
    int txnArgIndex = -1;
  };

public:
  class MgrData : private MgrData_
  {
    friend class TxnAuxDataMgrBase;
  };

protected:
  static MgrData_ &
  access(MgrData &md)
  {
    return md;
  }
};

using TxnAuxMgrData = TxnAuxDataMgrBase::MgrData;

// Class to manage auxiliary data for a transaction.  If an instance is created for the transaction, the instance
// will be deleted on the TXN_CLOSE transaction hook (which is always triggered for all transactions).
// The TxnAuxData class must have a public default constructor.  Each instance of TxnAuxMgrData should be
// used in only one instantiation of this template.  The last template parameter optionally allows the continuation
// that handles TXN_CLOSE to also be used to handle other transaction hooks.  If TxnAuxDataMgr::handle_hook() is
// called for a transaction hook, and the hook is triggered for a transaction, the function (if any) specified as
// the Txn_event_func parameter will be called, with the hook's corresponding event as the second parameter to the
// function.
//
// Txn_event_func should return true if TSHttpTxnReenable() should be called with TS_EVENT_HTTP_CONTINUE.
// Txn_event_func should return false if TSHttpTxnReenable() should be called with TS_EVENT_HTTP_ERROR.
// Txn_event_func should not call TSHttpTxnReenable() itself.
//
template <class TxnAuxData, TxnAuxMgrData &MDRef, bool (*Txn_event_func)(TSHttpTxn txn, TSEvent) = nullptr>
class TxnAuxDataMgr : private TxnAuxDataMgrBase
{
public:
  using Data = TxnAuxData;

  // This must be called from the plugin init function, before any other member function.  arg_name is the name for
  // the transaction argument used to store the pointer to the auxiliary data class instance.  Repeated calls are ignored.
  //
  static void
  init(char const *arg_name, char const *arg_desc = "per-transaction auxiliary data")
  {
    MgrData_ &md = access(MDRef);

    if (md.txnArgIndex >= 0) {
      return;
    }

    TSReleaseAssert(TSUserArgIndexReserve(TS_USER_ARGS_TXN, arg_name, arg_desc, &md.txnArgIndex) == TS_SUCCESS);
    TSReleaseAssert(md.txnContp = TSContCreate(_cont_func, nullptr));
  }

  // If Txn_event_func is not null, use these functions to specify transaction hooks that Txn_event_func function
  // should handle.

  static typename std::enable_if<Txn_event_func != nullptr>::type
  handle_global_hook(TSHttpHookID hid)
  {
    MgrData_ &md = access(MDRef);

    TSAssert(md.txnArgIndex >= 0);

    check_valid_hook(hid);

    TSHttpHookAdd(hid, md.txnContp);
  }

  static typename std::enable_if<Txn_event_func != nullptr>::type
  handle_ssn_hook(TSHttpSsn ssn, TSHttpHookID hid)
  {
    MgrData_ &md = access(MDRef);

    TSAssert(md.txnArgIndex >= 0);

    check_valid_hook(hid);

    TSHttpSsnHookAdd(ssn, hid, md.txnContp);
  }

  static typename std::enable_if<Txn_event_func != nullptr>::type
  handle_txn_hook(TSHttpTxn txn, TSHttpHookID hid)
  {
    MgrData_ &md = access(MDRef);

    TSAssert(md.txnArgIndex >= 0);

    check_valid_hook(hid);

    TSAssert(TS_HTTP_TXN_START_HOOK != hid);

    TSHttpTxnHookAdd(txn, hid, md.txnContp);
  }

  // Get a reference to the auxiliary data for a transaction.
  //
  static TxnAuxData &
  data(TSHttpTxn txn)
  {
    MgrData_ &md = access(MDRef);

    TSAssert(md.txnArgIndex >= 0);

    auto d = static_cast<TxnAuxData *>(TSUserArgGet(txn, md.txnArgIndex));
    if (!d) {
      d = new TxnAuxData;

      TSUserArgSet(txn, md.txnArgIndex, d);

      TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, md.txnContp);
    }
    return *d;
  }

private:
  static int
  _cont_func(TSCont, TSEvent event, void *edata)
  {
    TSAssert((TS_EVENT_HTTP_READ_REQUEST_HDR <= event) && (event <= TS_EVENT_HTTP_REQUEST_BUFFER_COMPLETE));

    MgrData_ &md = access(MDRef);

    auto txn    = static_cast<TSHttpTxn>(edata);
    bool result = true;
    if (TS_EVENT_HTTP_TXN_CLOSE == event) {
      auto data = static_cast<TxnAuxData *>(TSUserArgGet(txn, md.txnArgIndex));
      delete data;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
      // GCC complains about this being a constexpr even when it's explicitly a constexpr.
      //
    } else if constexpr (Txn_event_func != nullptr) {
#pragma GCC diagnostic pop
      result = Txn_event_func(txn, event);
    } else {
      // Event must be TXN_CLOSE.
      //
      TSReleaseAssert(false);
    }
    TSHttpTxnReenable(txn, result ? TS_EVENT_HTTP_CONTINUE : TS_EVENT_HTTP_ERROR);
    return 0;
  };

  static void
  check_valid_hook(TSHttpHookID hid)
  {
    TSAssert((TS_HTTP_TXN_START_HOOK == hid) || (TS_HTTP_PRE_REMAP_HOOK == hid) || (TS_HTTP_POST_REMAP_HOOK == hid) ||
             (TS_HTTP_READ_REQUEST_HDR_HOOK == hid) || (TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK == hid) ||
             (TS_HTTP_OS_DNS_HOOK == hid) || (TS_HTTP_SEND_REQUEST_HDR_HOOK == hid) || (TS_HTTP_READ_CACHE_HDR_HOOK == hid) ||
             (TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK == hid) || (TS_HTTP_READ_RESPONSE_HDR_HOOK == hid) ||
             (TS_HTTP_SEND_RESPONSE_HDR_HOOK == hid));
  }
};

} // end namespace atscppapi
