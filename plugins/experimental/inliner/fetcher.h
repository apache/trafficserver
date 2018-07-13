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
/** @file

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

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <limits>
#include <netinet/in.h>

#include <cinttypes>

#include "chunk-decoder.h"
#include "ts.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

#define unlikely(x) __builtin_expect((x), 0)

namespace ats
{
struct HttpParser {
  bool parsed_;
  TSHttpParser parser_;
  TSMBuffer buffer_;
  TSMLoc location_;

  void destroyParser(void);

  ~HttpParser()
  {
    TSHandleMLocRelease(buffer_, TS_NULL_MLOC, location_);
    TSMBufferDestroy(buffer_);
    destroyParser();
  }

  HttpParser(void) : parsed_(false), parser_(TSHttpParserCreate()), buffer_(TSMBufferCreate()), location_(TSHttpHdrCreate(buffer_))
  {
    TSHttpHdrTypeSet(buffer_, location_, TS_HTTP_TYPE_RESPONSE);
  }

  bool parse(io::IO &);

  int
  statusCode(void) const
  {
    return static_cast<int>(TSHttpHdrStatusGet(buffer_, location_));
  }
};

template <class T> struct HttpTransaction {
  typedef HttpTransaction<T> Self;

  bool parsingHeaders_;
  bool abort_;
  bool timeout_;
  io::IO *in_;
  io::IO *out_;
  TSVConn vconnection_;
  TSCont continuation_;
  T t_;
  HttpParser parser_;
  ChunkDecoder *chunkDecoder_;

  ~HttpTransaction()
  {
    if (in_ != nullptr) {
      delete in_;
      in_ = nullptr;
    }
    if (out_ != nullptr) {
      delete out_;
      out_ = nullptr;
    }
    timeout(0);
    assert(vconnection_ != nullptr);
    if (abort_) {
      TSVConnAbort(vconnection_, TS_VC_CLOSE_ABORT);
    } else {
      TSVConnClose(vconnection_);
    }
    assert(continuation_ != nullptr);
    TSContDestroy(continuation_);
    if (chunkDecoder_ != nullptr) {
      delete chunkDecoder_;
    }
  }

  HttpTransaction(TSVConn v, TSCont c, io::IO *const i, const uint64_t l, const T &t)
    : parsingHeaders_(false),
      abort_(false),
      timeout_(false),
      in_(nullptr),
      out_(i),
      vconnection_(v),
      continuation_(c),
      t_(t),
      chunkDecoder_(nullptr)
  {
    assert(vconnection_ != nullptr);
    assert(continuation_ != nullptr);
    assert(out_ != nullptr);
    assert(l > 0);
    out_->vio = TSVConnWrite(vconnection_, continuation_, out_->reader, l);
  }

  inline void
  abort(const bool b = true)
  {
    abort_ = b;
  }

  void
  timeout(const int64_t t)
  {
    assert(t >= 0);
    assert(vconnection_ != nullptr);
    if (timeout_) {
      TSVConnActiveTimeoutCancel(vconnection_);
      timeout_ = false;
    } else {
      TSVConnActiveTimeoutSet(vconnection_, t);
      timeout_ = true;
    }
  }

  static void
  close(Self *const s)
  {
    assert(s != nullptr);
    TSVConnShutdown(s->vconnection_, 1, 0);
    delete s;
  }

  static bool
  isChunkEncoding(const TSMBuffer b, const TSMLoc l)
  {
    assert(b != nullptr);
    assert(l != nullptr);
    bool result        = false;
    const TSMLoc field = TSMimeHdrFieldFind(b, l, TS_MIME_FIELD_TRANSFER_ENCODING, TS_MIME_LEN_TRANSFER_ENCODING);
    if (field != nullptr) {
      int length;
      const char *const value = TSMimeHdrFieldValueStringGet(b, l, field, -1, &length);
      if (value != nullptr && length == TS_HTTP_LEN_CHUNKED) {
        result = strncasecmp(value, TS_HTTP_VALUE_CHUNKED, TS_HTTP_LEN_CHUNKED) == 0;
      }
      TSHandleMLocRelease(b, l, field);
    }
    return result;
  }

  static int
  handle(TSCont c, TSEvent e, void *d)
  {
    Self *const self = static_cast<Self *const>(TSContDataGet(c));
    assert(self != nullptr);
    switch (e) {
    case TS_EVENT_ERROR:
      TSDebug(PLUGIN_TAG, "HttpTransaction: ERROR");
      self->t_.error();
      self->abort();
      close(self);
      TSContDataSet(c, nullptr);
      break;
    case TS_EVENT_VCONN_EOS:
      TSDebug(PLUGIN_TAG, "HttpTransaction: EOS");
      goto here;

    case TS_EVENT_VCONN_READ_COMPLETE:
      TSDebug(PLUGIN_TAG, "HttpTransaction: Read Complete");
      goto here;

    case TS_EVENT_VCONN_READ_READY:
      TSDebug(PLUGIN_TAG, "HttpTransaction: Read");
    here : {
      assert(self->in_ != nullptr);
      assert(self->in_->reader != nullptr);
      assert(self->in_->vio != nullptr);
      const int64_t available = TSIOBufferReaderAvail(self->in_->reader);
      if (available > 0) {
        if (self->parsingHeaders_) {
          if (self->parser_.parse(*self->in_)) {
            if (isChunkEncoding(self->parser_.buffer_, self->parser_.location_)) {
              assert(self->chunkDecoder_ == nullptr);
              self->chunkDecoder_ = new ChunkDecoder();
            }
            self->t_.header(self->parser_.buffer_, self->parser_.location_);
            self->parsingHeaders_ = false;
          }
        }
        if (!self->parsingHeaders_) {
          if (self->chunkDecoder_ != nullptr) {
            int64_t i = self->chunkDecoder_->decode(self->in_->reader);
            do {
              assert(i <= available);
              self->t_.data(self->in_->reader, i);
              TSIOBufferReaderConsume(self->in_->reader, i);
              i = self->chunkDecoder_->decode(self->in_->reader);
            } while (i > 0);
          } else {
            const int64_t l = self->t_.data(self->in_->reader, available);
            TSIOBufferReaderConsume(self->in_->reader, l);
          }
        }
      }
      if (e == TS_EVENT_VCONN_READ_COMPLETE || e == TS_EVENT_VCONN_EOS) {
        self->t_.done();
        close(self);
        TSContDataSet(c, nullptr);
      } else if (self->chunkDecoder_ != nullptr && self->chunkDecoder_->isEnd()) {
        assert(self->parsingHeaders_ == false);
        assert(isChunkEncoding(self->parser_.buffer_, self->parser_.location_));
        self->abort();
        self->t_.done();
        close(self);
        TSContDataSet(c, nullptr);
      } else {
        TSVIOReenable(self->in_->vio);
      }
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSDebug(PLUGIN_TAG, "HttpTransaction: Write Complete");
      self->parsingHeaders_ = true;
      assert(self->in_ == nullptr);
      self->in_ = io::IO::read(self->vconnection_, c);
      assert(self->in_ != nullptr);
      assert(self->vconnection_);
      TSVConnShutdown(self->vconnection_, 0, 1);
      assert(self->out_ != nullptr);
      delete self->out_;
      self->out_ = nullptr;
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      TSDebug(PLUGIN_TAG, "HttpTransaction: Write Ready (Done: %" PRId64 " Todo: %" PRId64 ")", TSVIONDoneGet(self->out_->vio),
              TSVIONTodoGet(self->out_->vio));
      assert(self->out_ != nullptr);
      TSVIOReenable(self->out_->vio);
      break;
    case 106:
    case TS_EVENT_TIMEOUT:
    case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
      TSDebug(PLUGIN_TAG, "HttpTransaction: Timeout");
      self->t_.timeout();
      self->abort();
      close(self);
      TSContDataSet(c, nullptr);
      break;

    default:
      assert(false); // UNRECHEABLE.
    }
    return 0;
  }
};

template <class T>
bool
get(const std::string &a, io::IO *const i, const int64_t l, const T &t, const int64_t ti = 0)
{
  typedef HttpTransaction<T> Transaction;
  struct sockaddr_in socket;
  socket.sin_family = AF_INET;
  socket.sin_port   = 80;
  if (!inet_pton(AF_INET, a.c_str(), &socket.sin_addr)) {
    TSDebug(PLUGIN_TAG, "ats::get Invalid address provided \"%s\".", a.c_str());
    return false;
  }
  TSVConn vconn = TSHttpConnect(reinterpret_cast<sockaddr *>(&socket));
  assert(vconn != nullptr);
  TSCont contp = TSContCreate(Transaction::handle, nullptr);
  assert(contp != nullptr);
  Transaction *transaction = new Transaction(vconn, contp, i, l, t);
  TSContDataSet(contp, transaction);
  if (ti > 0) {
    TSDebug(PLUGIN_TAG, "ats::get Setting active timeout to: %" PRId64, ti);
    transaction->timeout(ti);
  }
  return true;
}

template <class T>
bool
get(io::IO *const i, const int64_t l, const T &t, const int64_t ti = 0)
{
  return get("127.0.0.1", i, l, t, ti);
}
} // namespace ats
