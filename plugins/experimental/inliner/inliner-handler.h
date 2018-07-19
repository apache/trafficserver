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

#include <string>
#include <memory>

#include "html-parser.h"
#include "ts.h"

namespace ats
{
namespace inliner
{
  struct Handler : HtmlParser {
    ats::io::IOSinkPointer ioSink_;
    ats::io::SinkPointer sink_, sink2_;
    const TSIOBufferReader reader_;
    size_t counter_;
    bool abort_;

    ~Handler() override
    {
      assert(reader_ != nullptr);
      if (!abort_) {
        const int64_t available = TSIOBufferReaderAvail(reader_);
        if (available > 0) {
          TSIOBufferReaderConsume(reader_, available);
        }
      }
      TSIOBufferReaderFree(reader_);
    }

    Handler(const TSIOBufferReader, ats::io::IOSinkPointer &&);

    Handler(const Handler &) = delete;
    Handler &operator=(const Handler &) = delete;

    void parse(void);

    size_t bypass(const size_t, const size_t) override;
    void handleImage(const Attributes &) override;

    std::string generateId(void);

    void abort(void);
  };

} // namespace inliner
} // namespace ats
