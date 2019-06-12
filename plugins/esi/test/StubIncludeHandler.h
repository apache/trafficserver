/** @file

  A brief file description

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

#include <list>
#include "SpecialIncludeHandler.h"

class StubIncludeHandler : public EsiLib::SpecialIncludeHandler
{
public:
  StubIncludeHandler(EsiLib::Variables &esi_vars, EsiLib::Expression &esi_expr, HttpDataFetcher &http_fetcher)
    : EsiLib::SpecialIncludeHandler(esi_vars, esi_expr, http_fetcher), parseCompleteCalled(false), n_includes(0)
  {
  }

  int handleInclude(const char *data, int data_len);

  bool parseCompleteCalled;
  void handleParseComplete();

  bool getData(int include_id, const char *&data, int &data_len);

  void getFooter(const char *&footer, int &footer_len);

  ~StubIncludeHandler();

  static bool includeResult;
  static const char *const DATA_PREFIX;
  static const int DATA_PREFIX_SIZE;

  static const char *FOOTER;
  static int FOOTER_SIZE;

private:
  int n_includes;
  std::list<char *> heap_strings;
};
