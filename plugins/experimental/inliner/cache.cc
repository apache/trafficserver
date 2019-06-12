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
#include "cache.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

namespace ats
{
namespace cache
{
  void
  write(const std::string &k, std::string &&s)
  {
    Key key(k);
    TSCont continuation = TSContCreate(Write::handle, nullptr);
    assert(continuation != nullptr);
    TSContDataSet(continuation, new Write(std::move(s)));
    TSCacheWrite(continuation, key.key());
  }

  int
  Write::handle(TSCont c, TSEvent e, void *v)
  {
    assert(c != nullptr);
    Write *const self = static_cast<Write *>(TSContDataGet(c));
    assert(self != nullptr);
    switch (e) {
    case TS_EVENT_CACHE_OPEN_WRITE:
      assert(v != nullptr);
      self->vconnection_ = static_cast<TSVConn>(v);
      assert(self->out_ == nullptr);
      self->out_ = io::IO::write(self->vconnection_, c, self->content_.size());
      break;
    case TS_EVENT_CACHE_OPEN_WRITE_FAILED:
      TSDebug(PLUGIN_TAG, "write failed");
      delete self;
      TSContDataSet(c, nullptr);
      TSContDestroy(c);
      break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSDebug(PLUGIN_TAG, "write completed");
      assert(self->vconnection_ != nullptr);
      TSVConnClose(self->vconnection_);
      delete self;
      TSContDataSet(c, nullptr);
      TSContDestroy(c);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      TSIOBufferWrite(self->out_->buffer, self->content_.data(), self->content_.size());
      break;
    default:
      assert(false); // UNREACHABLE.
      break;
    }
    return 0;
  }
} // namespace cache
} // namespace ats
