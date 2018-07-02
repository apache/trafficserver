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

#include "ts.h"

namespace ats
{
namespace io
{
  namespace vconnection
  {
    template <class T> class Read
    {
      typedef Read<T> Self;
      TSVConn vconnection_;
      io::IO in_;
      T t_;

      Read(TSVConn v, T &&t, const int64_t s) : vconnection_(v), t_(std::forward<T>(t))
      {
        assert(vconnection_ != nullptr);
        TSCont continuation = TSContCreate(Self::handleRead, nullptr);
        assert(continuation != nullptr);
        TSContDataSet(continuation, this);
        in_.vio = TSVConnRead(vconnection_, continuation, in_.buffer, s);
      }

      static void
      close(Self *const s)
      {
        assert(s != nullptr);
        TSIOBufferReaderConsume(s->in_.reader, TSIOBufferReaderAvail(s->in_.reader));
        assert(s->vconnection_ != nullptr);
        TSVConnShutdown(s->vconnection_, 1, 1);
        TSVConnClose(s->vconnection_);
        delete s;
      }

      static int
      handleRead(TSCont c, TSEvent e, void *)
      {
        Self *const self = static_cast<Self *const>(TSContDataGet(c));
        assert(self != nullptr);
        switch (e) {
        case TS_EVENT_VCONN_EOS:
        case TS_EVENT_VCONN_READ_COMPLETE:
        case TS_EVENT_VCONN_READ_READY: {
          const int64_t available = TSIOBufferReaderAvail(self->in_.reader);
          if (available > 0) {
            self->t_.data(self->in_.reader);
            TSIOBufferReaderConsume(self->in_.reader, available);
          }
          if (e == TS_EVENT_VCONN_READ_COMPLETE || e == TS_EVENT_VCONN_EOS) {
            self->t_.done();
            close(self);
            TSContDataSet(c, nullptr);
            TSContDestroy(c);
          }
        } break;
        default:
          assert(false); // UNRECHEABLE.
          break;
        }
        return TS_SUCCESS;
      }

      template <class U> friend void read(TSVConn, U &&, const int64_t);
    };

    template <class C>
    void
    read(TSVConn v, C &&c, const int64_t s)
    {
      new Read<C>(v, std::forward<C>(c), s);
    }

  } // namespace vconnection
} // namespace io
} // namespace ats
