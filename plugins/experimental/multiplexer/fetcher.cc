/** @file

  Multiplexes request to other origins.

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
#include "fetcher.h"

namespace ats
{
void
HttpParser::destroyParser()
{
  if (parser_ != nullptr) {
    TSHttpParserClear(parser_);
    TSHttpParserDestroy(parser_);
    parser_ = nullptr;
  }
}

bool
HttpParser::parse(io::IO &io)
{
  if (parsed_) {
    return true;
  }
  TSIOBufferBlock block = TSIOBufferReaderStart(io.reader);
  while (block != nullptr) {
    int64_t size            = 0;
    const char *const begin = TSIOBufferBlockReadStart(block, io.reader, &size);
    const char *iterator    = begin;

    parsed_ = (TSHttpHdrParseResp(parser_, buffer_, location_, &iterator, iterator + size) == TS_PARSE_DONE);
    TSIOBufferReaderConsume(io.reader, iterator - begin);

    if (parsed_) {
      TSDebug(PLUGIN_TAG, "HttpParser: response parsing is complete (%u response status code)", statusCode());
      assert(parser_ != nullptr);
      destroyParser();
      return true;
    }

    block = TSIOBufferBlockNext(block);
  }
  return false;
}

} // namespace ats
