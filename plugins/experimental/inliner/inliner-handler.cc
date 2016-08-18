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
#include <assert.h>
#include <iostream>
#include <sstream>

#include "inliner-handler.h"
#include "cache.h"
#include "cache-handler.h"

namespace ats
{
namespace inliner
{
  Handler::Handler(const TSIOBufferReader r, ats::io::IOSinkPointer &&i)
    : ioSink_(i), sink_(ioSink_->branch()), sink2_(sink_->branch()), reader_(TSIOBufferReaderClone(r)), counter_(0), abort_(false)
  {
    assert(ioSink_);
    assert(sink_);
    assert(sink_->data_);
    assert(sink2_);
    assert(sink2_->data_);
    assert(reader_ != NULL);
    *sink_ << "<script>"
              "var a=document,b=a.getElementsByTagName(\"img\"),c=b.length,w=window,d=function(){var "
              "m=w.addEventListener,n=w.attachEvent;return "
              "m?function(k){m(\"load\",k)}:n?function(k){n(\"onload\",k)}:function(k){k()}}(),e=function(){var "
              "m=window,n=a.documentElement,k=a.getElementsByTagName(\"body\")[0];return "
              "function(l){l=l.getBoundingClientRect();return "
              "0<=l.top&&0<=l.left&&l.bottom<=(m.innerHeight||n.clientHeight||k.clientHeight)&&l.right<=(m.innerWidth||n."
              "clientWidth||k.clientWidth)}}();function f(m,n){var k=new Image;k.onload=function(){k=null;n(m)};k.src=m}function "
              "g(m,n){var k,l;for(k=0;k<c;++k)l=b[k],0===l.className.indexOf(m+\" \")&&n(l)}function "
              "h(m,n){f(n,function(k){g(m,function(l){l.src=k})})}function i(m,n){function k(k){var "
              "l;for(l=0;l<q;l++)p[l].src=k}var "
              "l=!1,p=[],q;g(m,function(k){l|=e(k);p.push(k)});q=p.length;l?f(n,k):d(function(){f(n,k)})};"
              "</script>";
  }

  size_t
  Handler::bypass(const size_t s, const size_t o)
  {
    assert(s > 0);
    assert(sink2_);
    // TSDebug(PLUGIN_TAG, "size: %lu, offset: %lu, sum: %lu", s, o, (s + o));
    *sink2_ << ats::io::ReaderSize(reader_, s, o);
    return s;
  }

  void
  Handler::parse(void)
  {
    assert(reader_ != NULL);
    TSIOBufferBlock block = TSIOBufferReaderStart(reader_);
    int64_t offset        = 0;
    while (block != NULL) {
      int64_t length           = 0;
      const char *const buffer = TSIOBufferBlockReadStart(block, reader_, &length);
      assert(buffer != NULL);
      if (length > 0) {
        HtmlParser::parse(buffer, length, offset);
        offset += length;
      }
      block = TSIOBufferBlockNext(block);
    }
    assert(offset == TSIOBufferReaderAvail(reader_));
    if (offset > 0) {
      TSIOBufferReaderConsume(reader_, offset);
    }
    assert(TSIOBufferReaderAvail(reader_) == 0);
  }

  void
  Handler::handleImage(const Attributes &a)
  {
    std::string src;

    for (Attributes::const_iterator item = a.begin(); item != a.end(); ++item) {
      if (!item->first.empty()) {
        src = item->second;
      }
    }

    const bool isTagged =
      (src.find("http://") == 0 || src.find("https://") == 0) && src.find("inline", src.find("#")) != std::string::npos;

    if (isTagged) {
      std::string classes, original = " ";
      for (Attributes::const_iterator item = a.begin(); item != a.end(); ++item) {
        if (!item->first.empty()) {
          if (!item->second.empty()) {
            if (item->first == "class") {
              classes = item->second;
            } else if (item->first.find("src") == std::string::npos) {
              original += item->first + "=\"" + item->second += "\" ";
            }
          }
        } else {
          original += item->first + " ";
        }
      }

      assert(sink_ != NULL);
      assert(sink2_ != NULL);
      src.erase(src.find('#'));
      cache::fetch<CacheHandler>(src + VERSION, src, original, classes, generateId(), sink2_->branch(), sink_);
    } else {
      assert(sink2_ != NULL);
      *sink2_ << " " << static_cast<std::string>(a);
    }
  }

  std::string
  Handler::generateId(void)
  {
    std::stringstream ss;
    // TODO(dmorilha): stop using memory address here.
    ss << "ii-" << static_cast<void *>(this) << "-" << ++counter_;
    return ss.str();
  }

  void
  Handler::abort(void)
  {
    abort_ = true;
    assert(ioSink_);
    ioSink_->abort();
  }

} // end of inliner namespace
} // end of ats namespace
