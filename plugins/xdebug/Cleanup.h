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
    TSCont txnCloseContp = nullptr;
    int txnArgIndex      = -1;
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

// Class to manage auxilliary data for a transaction.  If an instance is created for the transaction, the instance
// will be deleted on the TXN_CLOSE transaction hook (which is always triggered for all transactions).
// The TxnAuxData class must have a public default constructor.
//
template <class TxnAuxData, TxnAuxMgrData &MDRef> class TxnAuxDataMgr : private TxnAuxDataMgrBase
{
public:
  using Data = TxnAuxData;

  // This must be called from the plugin init function.  arg_name is the name for the transaction argument used
  // to store the pointer to the auxiliary data class instance.  Repeated calls are ignored.
  //
  static void
  init(char const *arg_name, char const *arg_desc = "per-transaction auxiliary data")
  {
    MgrData_ &md = access(MDRef);

    if (md.txnArgIndex >= 0) {
      return;
    }

    TSReleaseAssert(TSUserArgIndexReserve(TS_USER_ARGS_TXN, arg_name, arg_desc, &md.txnArgIndex) == TS_SUCCESS);
    TSReleaseAssert(md.txnCloseContp = TSContCreate(_deleteAuxData, nullptr));
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

      TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, md.txnCloseContp);
    }
    return *d;
  }

private:
  static int
  _deleteAuxData(TSCont, TSEvent, void *edata)
  {
    MgrData_ &md = access(MDRef);

    auto txn  = static_cast<TSHttpTxn>(edata);
    auto data = static_cast<TxnAuxData *>(TSUserArgGet(txn, md.txnArgIndex));
    delete data;
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  };
};

} // end namespace atscppapi
