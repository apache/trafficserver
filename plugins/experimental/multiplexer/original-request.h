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
#ifndef ORIGINAL_REQUEST_H
#define ORIGINAL_REQUEST_H

#include <cassert>
#include <string>
#include <ts/ts.h>

/*
 * on dispatch we get one parsed request.
 * So we want to alter and modify it back the way it was originally.
 */
class OriginalRequest
{
  TSMBuffer buffer_;
  TSMLoc location_;
  TSMLoc url_;
  TSMLoc hostHeader_;
  TSMLoc xMultiplexerHeader_;

  OriginalRequest(const OriginalRequest &);
  OriginalRequest &operator=(const OriginalRequest &);

public:
  struct {
    const std::string hostHeader;
    const std::string urlHost;
    const std::string urlScheme;
    const std::string xMultiplexerHeader;
  } original;

  ~OriginalRequest();

  OriginalRequest(const TSMBuffer, const TSMLoc);

  void urlScheme(const std::string &);
  void urlHost(const std::string &);
  void hostHeader(const std::string &);
  bool xMultiplexerHeader(const std::string &);
};

#endif // ORIGINAL_REQUEST_H
