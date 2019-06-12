/** @file

  Inlines base64 images from the ATS cache

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

#pragma once

#include <cassert>
#include <string>

#include "ts.h"
#include "util.h"

namespace ats
{
namespace cache
{
  struct Key {
    ~Key()
    {
      assert(key_ != nullptr);
      TSCacheKeyDestroy(key_);
    }

    Key() : key_(TSCacheKeyCreate()) { assert(key_ != nullptr); }
    Key(const Key &) = delete;
    Key &operator=(const Key &) = delete;

    explicit Key(const std::string &s) : key_(TSCacheKeyCreate())
    {
      assert(key_ != nullptr);
      CHECK(TSCacheKeyDigestSet(key_, s.c_str(), s.size()));
    }

    TSCacheKey
    key() const
    {
      return key_;
    }

    TSCacheKey key_;
  };

  template <class T> struct Read {
    typedef Read<T> Self;

    T t_;

    template <class... A> Read(A &&... a) : t_(std::forward<A>(a)...) {}
    static int
    handle(TSCont c, TSEvent e, void *d)
    {
      Self *const self = static_cast<Self *const>(TSContDataGet(c));
      assert(self != nullptr);
      switch (e) {
      case TS_EVENT_CACHE_OPEN_READ:
        assert(d != nullptr);
        self->t_.hit(static_cast<TSVConn>(d));
        break;
      case TS_EVENT_CACHE_OPEN_READ_FAILED:
        self->t_.miss();
        break;
      default:
        assert(false); // UNREACHABLE.
        break;
      }
      delete self;
      TSContDataSet(c, nullptr);
      TSContDestroy(c);
      return TS_SUCCESS;
    }
  };

  template <class T, class... A>
  void
  fetch(const std::string &k, A &&... a)
  {
    const Key key(k);
    const TSCont continuation = TSContCreate(Read<T>::handle, TSMutexCreate());
    assert(continuation != nullptr);
    TSContDataSet(continuation, new Read<T>(std::forward<A>(a)...));
    TSCacheRead(continuation, key.key());
  }

  struct Write {
    const std::string content_;
    io::IO *out_;
    TSVConn vconnection_;

    ~Write()
    {
      if (out_ != nullptr) {
        delete out_;
      }
    }

    Write(std::string &&s) : content_(std::move(s)), out_(nullptr), vconnection_(nullptr) {}
    static int handle(TSCont, TSEvent, void *);
  };

  void write(const std::string &, std::string &&);

} // namespace cache
} // namespace ats
