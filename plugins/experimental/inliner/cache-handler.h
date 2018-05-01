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
#ifndef CACHE_HANDLER_H
#define CACHE_HANDLER_H

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <cassert>

#include "cache.h"
#include "fetcher.h"
#include "gif.h"
#include "jpeg.h"
#include "png.h"
#include "ts.h"
#include "vconnection.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

#define ONE_PIXEL "data:image/gif;base64,R0lGODlhAQABAIAAAP///////yH5BAEKAAEALAAAAAABAAEAAAICTAEAOw=="

#define VERSION "&version=1"

namespace ats
{
bool
getHeader(TSMBuffer buffer, TSMLoc location, const std::string &name, std::string &value)
{
  bool result        = false;
  const TSMLoc field = TSMimeHdrFieldFind(buffer, location, name.c_str(), name.size());
  if (field != nullptr) {
    int length                = 0;
    const char *const content = TSMimeHdrFieldValueStringGet(buffer, location, field, -1, &length);
    if (content != nullptr && length > 0) {
      value  = std::string(content, length);
      result = true;
    }
    TSHandleMLocRelease(buffer, location, field);
  }
  return result;
}

namespace inliner
{
  struct AnotherClass {
    util::Buffer content_;
    std::string contentType_;
    const std::string url_;

    AnotherClass(const std::string &u) : url_(u) {}
    int64_t
    data(const TSIOBufferReader r, int64_t l)
    {
      assert(r != nullptr);
      TSIOBufferBlock block = TSIOBufferReaderStart(r);

      assert(l >= 0);
      if (l == 0) {
        l = TSIOBufferReaderAvail(r);
        assert(l >= 0);
      }

      int64_t length = 0;

      for (; block && l > 0; block = TSIOBufferBlockNext(block)) {
        int64_t size              = 0;
        const char *const pointer = TSIOBufferBlockReadStart(block, r, &size);
        if (pointer != nullptr && size > 0) {
          size = std::min(size, l);
          std::copy(pointer, pointer + size, std::back_inserter(content_));
          length += size;
          l -= size;
        }
      }

      return length;
    }

    void
    done(void)
    {
      if (GIF::verifySignature(content_)) {
        contentType_ = "image/gif";
      } else if (JPEG::verifySignature(content_)) {
        contentType_ = "image/jpeg";
      } else if (PNG::verifySignature(content_)) {
        contentType_ = "image/png";
      } else {
        // TODO(dmorilha): check png signature code.
        TSDebug(PLUGIN_TAG, "Invalid signature for: %s", url_.c_str());
      }

      if (contentType_ != "image/gif" && contentType_ != "image/jpeg" && contentType_ != "image/jpg" &&
          contentType_ != "image/png") {
        return;
      }

      if (!contentType_.empty() && !content_.empty()) {
        std::string output;
        output.reserve(content_.size() * 5);
        output += "data:";
        output += contentType_;
        output += ";base64,";

        {
          const int64_t s = output.size();
          size_t size     = 0;
          output.resize(content_.size() * 5);
          CHECK(TSBase64Encode(content_.data(), content_.size(), const_cast<char *>(output.data()) + s, output.size() - s, &size));
          output.resize(size + s);
        }

        TSDebug(PLUGIN_TAG, "%s (%s) %lu %lu", url_.c_str(), contentType_.c_str(), content_.size(), output.size());

        cache::write(url_ + VERSION, std::move(output));
      }
    }

    void
    header(TSMBuffer b, TSMLoc l)
    {
      if (!getHeader(b, l, "Content-Type", contentType_)) {
        getHeader(b, l, "content-type", contentType_);
      }

      {
        std::string contentLengthValue;

        if (!getHeader(b, l, "Content-Length", contentLengthValue)) {
          getHeader(b, l, "content-length", contentLengthValue);
        }

        if (!contentLengthValue.empty()) {
          std::stringstream ss(contentLengthValue);
          uint32_t contentLength = 0;
          ss >> contentLength;
          TSDebug(PLUGIN_TAG, "Content-Length: %i", contentLength);
          content_.reserve(contentLength);
        }
      }
    }

    void
    timeout(void) const
    {
      TSDebug(PLUGIN_TAG, "Fetch timeout");
    }

    void
    error(void) const
    {
      TSDebug(PLUGIN_TAG, "Fetch error");
    }
  };

  uint64_t
  read(const TSIOBufferReader &r, std::string &o, int64_t l = 0)
  {
    assert(r != nullptr);
    TSIOBufferBlock block = TSIOBufferReaderStart(r);

    assert(l >= 0);
    if (l == 0) {
      l = TSIOBufferReaderAvail(r);
      assert(l >= 0);
    }

    uint64_t length = 0;

    for (; block && l > 0; block = TSIOBufferBlockNext(block)) {
      int64_t size              = 0;
      const char *const pointer = TSIOBufferBlockReadStart(block, r, &size);
      if (pointer != nullptr && size > 0) {
        size = std::min(size, l);
        o.append(pointer, size);
        length += size;
        l -= size;
      }
    }

    return length;
  }

  struct CacheHandler {
    std::string src_;
    std::string original_;
    std::string classes_;
    std::string id_;
    io::SinkPointer sink_;
    io::SinkPointer sink2_;
    TSIOBufferReader reader_;

    ~CacheHandler()
    {
      if (reader_ != nullptr) {
        TSIOBufferReaderConsume(reader_, TSIOBufferReaderAvail(reader_));
        assert(TSIOBufferReaderAvail(reader_) == 0);
        TSIOBufferReaderFree(reader_);
        reader_ = nullptr;
      }
    }

    template <class T1, class T2>
    CacheHandler(const std::string &s, const std::string &o, const std::string c, const std::string &i, T1 &&si, T2 &&si2)
      : src_(s), original_(o), classes_(c), id_(i), sink_(std::forward<T1>(si)), sink2_(std::forward<T2>(si2)), reader_(nullptr)
    {
      assert(sink_ != nullptr);
      assert(sink2_ != nullptr);
    }

    CacheHandler(CacheHandler &&h)
      : src_(std::move(h.src_)),
        original_(std::move(h.original_)),
        classes_(std::move(h.classes_)),
        id_(std::move(h.id_)),
        sink_(std::move(h.sink_)),
        sink2_(std::move(h.sink2_)),
        reader_(h.reader_)
    {
      h.reader_ = nullptr;
    }

    CacheHandler(const CacheHandler &) = delete;
    CacheHandler &operator=(const CacheHandler &) = delete;

    void
    done(void)
    {
      assert(reader_ != nullptr);
      assert(sink2_ != nullptr);
      std::string o;
      read(reader_, o);
      o = "<script>h(\"" + id_ + "\",\"" + o + "\");</script>";
      *sink2_ << o;
    }

    void
    data(TSIOBufferReader r)
    {
      // TODO(dmorilha): API maybe address as a different single event?
      if (reader_ == nullptr) {
        reader_ = TSIOBufferReaderClone(r);
      }
    }

    void
    hit(TSVConn v)
    {
      assert(v != nullptr);

      TSDebug(PLUGIN_TAG, "cache hit for %s (%" PRId64 " bytes)", src_.c_str(), TSVConnCacheObjectSizeGet(v));

      assert(sink_);

      *sink_ << original_;
      *sink_ << "src=\"" ONE_PIXEL "\" ";
      assert(!id_.empty());
      *sink_ << "class=\"" << id_;
      if (!classes_.empty()) {
        *sink_ << " " << classes_;
      }
      *sink_ << "\" ";

      sink_.reset();

      io::vconnection::read(v, std::move(*this), TSVConnCacheObjectSizeGet(v));
    }

    void
    miss(void)
    {
      assert(sink_ != nullptr);
      *sink_ << original_;
      if (!src_.empty()) {
        *sink_ << "src=\"" << src_ << "\" ";
      }
      if (!classes_.empty()) {
        *sink_ << "class=\"" << classes_ << "\" ";
      }
      sink_.reset();

      assert(sink2_ != nullptr);
      sink2_.reset();

      const std::string b;

      {
        const size_t DOUBLE_SLASH = src_.find("//");
        if (DOUBLE_SLASH != std::string::npos) {
          const_cast<std::string &>(b) = src_.substr(DOUBLE_SLASH + 2);
        } else {
          const_cast<std::string &>(b) = src_;
        }
      }

      const std::string::const_iterator b1 = b.begin(), b2 = b.end(), i = std::find(b1, b2, '/');

      std::string request = "GET ";
      request += std::string(i, b2);
      request += " HTTP/1.1\r\n";
      request += "Host: ";
      request += std::string(b1, i);
      request += "\r\n\r\n";

      ats::io::IO *const io = new io::IO();

      TSDebug(PLUGIN_TAG, "request:\n%s", request.c_str());

      ats::get(io, io->copy(request), AnotherClass(src_));
    }
  };

} // namespace inliner
} // namespace ats

#undef ONE_PIXEL

#endif // CACHE_HANDLER_H
